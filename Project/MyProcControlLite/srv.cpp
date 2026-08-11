#include "srv.hpp"
#include "srvapi.hpp"
#include "processhelper.h"
#include "resource.h"
#include <w32use.hpp>
#include <Aclapi.h>
#include <Sddl.h>
#include <wtsapi32.h>
using namespace std;

WindowsService* gInstance = nullptr;

WindowsService::WindowsService(const std::wstring& serviceName)
	: m_serviceName(serviceName),
	m_statusHandle(nullptr),
	m_stopRequested(false),
	m_pauseRequested(false),
	m_isRunning(false) {
	// 初始化服务状态
	m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	m_status.dwCurrentState = SERVICE_STOPPED;
	m_status.dwControlsAccepted = SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_SHUTDOWN;
	m_status.dwWin32ExitCode = NO_ERROR;
	m_status.dwServiceSpecificExitCode = 0;
	m_status.dwCheckPoint = 0;
	m_status.dwWaitHint = 0;
	stopEvent = CreateEventW(NULL, TRUE, FALSE, FALSE);
	if (!stopEvent) throw exception("Init failed: Cannot create event");
	m_status.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
	injector86_in = injector86_out = injector64_in = injector64_out = NULL;
	appPath = make_shared<WCHAR[]>(32768);
	GetModuleFileNameW(NULL, appPath.get(), 32768);
}

WindowsService::~WindowsService() {
	if (m_coreThread.joinable()) {
		m_coreThread.join();
	}
	if (m_stopThread.joinable()) {
		m_stopThread.join();
	}
	CloseHandle(stopEvent);
}

static VOID WINAPI srv_main(
	DWORD   dwNumServicesArgs,
	LPWSTR* lpServiceArgVectors
) {
	gInstance->OnStart();
};
static bool MyStartAsServiceW(wstring svc_name, LPSERVICE_MAIN_FUNCTIONW svc_main) {
	SERVICE_TABLE_ENTRYW ServiceTable[2]{ 0 };

	const size_t _size = (svc_name.length() + 1) * (sizeof(WCHAR));
	LPWSTR sname = (LPWSTR)calloc(_size, 1);
	if (!sname) return false;
#ifdef _MSVC_LANG
	wcscpy_s(sname, _size, svc_name.c_str());
#else
	wcscpy(sname, svc_name.c_str());
#endif
	ServiceTable[0].lpServiceName = sname;
	ServiceTable[0].lpServiceProc = svc_main;

	// 启动服务的控制分派机线程
	BOOL ret = StartServiceCtrlDispatcherW(ServiceTable);
	free(sname);
	return ret;
};
void WindowsService::Run() {
	gInstance = this;
	MyStartAsServiceW(m_serviceName, srv_main);
}

void WindowsService::__crash(DWORD reason) {
	if (reason == (DWORD)-1) reason = GetLastError();
	EXCEPTION_RECORD rec{}; CONTEXT ctx{};
	RtlCaptureContext(&ctx);
	rec.ExceptionCode = reason;
	rec.ExceptionAddress = _ReturnAddress();
	rec.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
	RaiseFailFastException(&rec, &ctx, 0);
	__fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
	ExitProcess((UINT)reason);
	TerminateProcess(GetCurrentProcess(), (UINT)reason);
	RtlRaiseException(&rec);
	terminate();
	abort();
	exit((int)reason);
	while (1) { __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE); }
	return ((void(*)())0x0)();
}

DWORD WINAPI WindowsService::ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
	WindowsService* pService = reinterpret_cast<WindowsService*>(lpContext);
	if (!pService) {
		return ERROR_CALL_NOT_IMPLEMENTED;
	}

	switch (dwControl) {
	case SERVICE_CONTROL_STOP:
		pService->OnStop();
		break;

	case SERVICE_CONTROL_PAUSE:
		pService->OnPause();
		break;

	case SERVICE_CONTROL_CONTINUE:
		pService->OnContinue();
		break;

	case SERVICE_CONTROL_INTERROGATE:
		pService->ReportStatus(pService->m_status.dwCurrentState);
		break;

	case SERVICE_CONTROL_SHUTDOWN:
		pService->OnShutdown();
		break;

	default:
		return ERROR_CALL_NOT_IMPLEMENTED;
	}

	return NO_ERROR;
}

void WindowsService::ReportStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
	static DWORD dwCheckPoint = 1;

	m_status.dwCurrentState = dwCurrentState;
	m_status.dwWin32ExitCode = dwWin32ExitCode;
	m_status.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING ||
		dwCurrentState == SERVICE_STOP_PENDING ||
		dwCurrentState == SERVICE_PAUSE_PENDING ||
		dwCurrentState == SERVICE_CONTINUE_PENDING) {
		m_status.dwCheckPoint = dwCheckPoint++;
	}
	else {
		m_status.dwCheckPoint = 0;
	}

	SetServiceStatus(m_statusHandle, &m_status);
}

DWORD __stdcall WindowsService::_MyCrashpadHandler(PVOID pThat) {
	if (0) return 0;
	WindowsService* that = (WindowsService*)pThat;

	auto worker = [that](std::wstring cmd, DWORD extraFlags, HANDLE hParent = nullptr) {
		while (that->m_isRunning) {
			STARTUPINFOEXW si{ sizeof(si) };
			PROCESS_INFORMATION pi{};
			std::unique_ptr<uint8_t[]> attributeList;

			DWORD flags = CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB | CREATE_SUSPENDED |
				EXTENDED_STARTUPINFO_PRESENT | extraFlags;

			if (hParent) {
				SIZE_T need{};
				InitializeProcThreadAttributeList(0, 1, 0, &need);
				if (need && need < 32768) {
					attributeList = make_unique<uint8_t[]>(need);
					if (InitializeProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get(),
						1, 0, &need)) {
						UpdateProcThreadAttribute(
							(PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get(), 0,
							PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
							&hParent,
							sizeof(HANDLE),
							NULL, NULL
						);
					}
				}
			}

			si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
			si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
			si.StartupInfo.wShowWindow = SW_SHOWNORMAL;
			si.lpAttributeList = PPROC_THREAD_ATTRIBUTE_LIST(attributeList ? attributeList.get() : nullptr);

			if (!CreateProcessW(that->RunDLL_X64.wstring().c_str(), cmd.data(),
				NULL, NULL, FALSE, flags, NULL, NULL, (LPSTARTUPINFOW)&si, &pi)) {
				if (attributeList) DeleteProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get());
				that->m_status.dwControlsAccepted = 0;
				that->m_status.dwWin32ExitCode = GetLastError();
				that->ReportStatus(SERVICE_STOPPED);
				ExitProcess(that->m_status.dwWin32ExitCode);
			}

			if (attributeList) DeleteProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get());
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);

			HANDLE waits[] { that->stopEvent, pi.hProcess };
			DWORD ret = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

			if (ret == WAIT_OBJECT_0) {
				CloseHandle(pi.hProcess);
				break;
			}
			CloseHandle(pi.hProcess);
		}
	};
	
#if 0
	w32ProcessHandle DcomLaunch;
	try{
		ServiceManager scm;
		auto svc = scm.get(L"DcomLaunch", SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
		SERVICE_STATUS_PROCESS ssp{};
		DWORD bytesNeeded = 0;
		BOOL ok = QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded);
		if (ok) {
			DcomLaunch = OpenProcess(PROCESS_CREATE_PROCESS | PROCESS_QUERY_LIMITED_INFORMATION, 0, ssp.dwProcessId);
		}
	}
	catch (...) {}
#endif

	std::wstring cmd0 = L"RunDLL32 \"" + that->injector64 +
		L"\",RunDLL /=DbgServiceKeepAlive /password=0812 /ppid " +
		std::to_wstring(GetCurrentProcessId()) +
		L" \"" + that->m_serviceName + L"\"";
	std::wstring cmd1 = L"RunDLL32 \"" + that->injector64 +
		L"\",RunDLL /=crashpad_handler /password=0812 /attach " +
		std::to_wstring(GetCurrentProcessId()) +
		L" /reportDir \"" + (that->session_res / L"crashpad").wstring() + L"\"";
	//std::thread t1(worker, cmd0, IDLE_PRIORITY_CLASS, DcomLaunch.get());
	// the line above would be fucked by Kaspersky so we remove it
	std::thread t1(worker, cmd0, IDLE_PRIORITY_CLASS);
	std::thread t2(worker, cmd1, BELOW_NORMAL_PRIORITY_CLASS);

	t1.join();
	t2.join();
	return 0;
}

bool WindowsService::OnInitialize() {
	// 注册服务控制处理函数
	m_statusHandle = RegisterServiceCtrlHandlerExW(m_serviceName.c_str(), ServiceCtrlHandler, this);
	if (!m_statusHandle) {
		return false;
	}

	EnableAllPrivileges(NULL);

	// 报告服务正在启动
	ReportStatus(SERVICE_START_PENDING);
	return true;
}

void WindowsService::OnStart() {
	if (!OnInitialize()) {
		return;
	}

	// prepare environment
	PrepareEnvironment();

	// 报告服务正在运行
	m_isRunning = true;
	ReportStatus(SERVICE_START_PENDING);

	// crashpad handler
	HANDLE hCrashpadHandler = CreateThread(NULL, 0, _MyCrashpadHandler, this, 0, 0);
	if (!hCrashpadHandler) {
		m_status.dwControlsAccepted = 0;
		m_status.dwWin32ExitCode = GetLastError();
		ReportStatus(SERVICE_STOPPED);
		ExitProcess(m_status.dwWin32ExitCode);
	}
	CloseHandle(hCrashpadHandler);

	// 启动核心线程
	m_coreThread = std::thread(&WindowsService::ServiceCoreThread, this);
}

void WindowsService::OnPause() {
	if (m_status.dwCurrentState == SERVICE_RUNNING) {
		ReportStatus(SERVICE_PAUSE_PENDING);
		m_pauseRequested = true;
		SuspendThread(m_coreThread.native_handle());
		m_status.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
		ReportStatus(SERVICE_PAUSED);
	}
}

void WindowsService::OnContinue() {
	if (m_status.dwCurrentState == SERVICE_PAUSED) {
		ReportStatus(SERVICE_CONTINUE_PENDING);
		m_pauseRequested = false;
		ResumeThread(m_coreThread.native_handle());
		m_status.dwControlsAccepted &= ~SERVICE_ACCEPT_STOP;
		m_status.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
		ReportStatus(SERVICE_RUNNING);
	}
}

void WindowsService::OnStop() {
	if (m_status.dwCurrentState == SERVICE_STOPPED) {
		return;
	}

	// 报告服务正在停止
	m_status.dwControlsAccepted = 0;
	ReportStatus(SERVICE_STOP_PENDING);

	// 设置停止标志
	m_stopRequested = true;
	m_isRunning = false;

	// 启动单独的停止处理线程
	m_stopThread = std::thread(&WindowsService::ServiceStopThread, this);

	// 等待核心线程结束
	if (m_coreThread.joinable()) {
		m_coreThread.join();
	}
}

// 计算机关机
void WindowsService::OnShutdown() {
	ReportStatus(SERVICE_STOPPED);
	m_isRunning = false;
	SetEvent(stopEvent);
	Sleep(5000);
	ExitProcess(0);
}

void WindowsService::ServiceStopThread() {
	// 执行停止服务的清理工作
	ResumeThread(m_coreThread.native_handle());

	Sleep(1000);
	SetEvent(stopEvent);
	ReportStatus(SERVICE_STOP_PENDING);

	// 
	if (m_coreThread.joinable()) {
		if (WAIT_TIMEOUT == WaitForSingleObject(m_coreThread.native_handle(), 10000)) {
#pragma warning(push)
#pragma warning(disable: 6258)
			TerminateThread(m_coreThread.native_handle(), 0);
#pragma warning(pop)
		}
	}

	// 报告服务已停止
	ReportStatus(SERVICE_STOPPED);
}



// -------------------



void WindowsService::PrepareEnvironment() {
	if (!(GetSystemDirectoryW(system32, 260) && GetTempPathW(260, Temp))) __crash();
	HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
	if (!k32) __crash();
	auto GetProcAddress = reinterpret_cast<decltype(&::GetProcAddress)>(::GetProcAddress(k32, "GetProcAddress"));
	if (!GetProcAddress) __crash();
	typedef DWORD(WINAPI* GetTempPath2W_t)(_In_ DWORD BufferLength, _Out_ LPWSTR Buffer);
	auto GetTempPath2W = (GetTempPath2W_t)GetProcAddress(k32, "GetTempPath2W");
	if (GetTempPath2W) {
		memset(Temp, 0, sizeof(Temp));
		if (!GetTempPath2W(260, Temp)) {
			if (!GetTempPathW(260, Temp)) __crash();
		}
	}
	RunDLL_X64 = system32, RunDLL_X86 = system32;
	RunDLL_X64 = (RunDLL_X64 / L"rundll32.exe").lexically_normal().make_preferred();
	RunDLL_X86 = (RunDLL_X86.parent_path() / L"SysWOW64" / L"rundll32.exe").lexically_normal().make_preferred();

	// prepare resource file
	session_res = Temp;
	session_res = session_res / (L"{E3A082AB-4D74-49A9-9804-DA7C0570C1B4}."s + m_serviceName);
	{
		if (INVALID_FILE_ATTRIBUTES == GetFileAttributesW(session_res.wstring().c_str())) {
			PSECURITY_DESCRIPTOR pSD = NULL;
			SECURITY_ATTRIBUTES sa = { 0 };
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.bInheritHandle = FALSE;

			// SDDL 字符串解析：
				// D:                 -> 声明 DACL（未加 P，开启并保留父目录继承）
				// (A;OICI;GA;;;SY)   -> 允许 SYSTEM (SY) 完全控制 (GA)
				// (A;OICI;GA;;;BA)   -> 允许 Administrators (BA) 完全控制 (GA)
				// (A;OICI;GRGX;;;WD) -> 允许 Everyone (WD) 读取与执行 (GRGX)
			if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
				L"D:(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GRGX;;;WD)",
				SDDL_REVISION_1,
				&pSD,
				NULL)) {
				sa.lpSecurityDescriptor = pSD;
			}

			// 调用 CreateDirectoryW 创建文件夹
			BOOL bCreated = CreateDirectoryW(session_res.wstring().c_str(), sa.lpSecurityDescriptor ? &sa : NULL);

			// 无论创建成功与否，用完之后必须释放 pSD 占用的内存
			if (pSD) {
				LocalFree(pSD);
				pSD = NULL;
			}

			if (!bCreated) __crash();
		}
		if (INVALID_FILE_ATTRIBUTES == GetFileAttributesW((session_res / L"x86").wstring().c_str())) {
			if (!CreateDirectoryW((session_res / L"x86").wstring().c_str(), NULL)) __crash();
		}
		if (INVALID_FILE_ATTRIBUTES == GetFileAttributesW((session_res / L"crashpad").wstring().c_str())) {
			if (!CreateDirectoryW((session_res / L"crashpad").wstring().c_str(), NULL)) __crash();
		}
		coredll86 = session_res / L"x86" / L"core.dll";
		if (!FreeResFile(IDR_BIN_COREDLL86, L"BIN", coredll86)) __crash();
		coredll64 = session_res / L"core.dll";
		if (!FreeResFile(IDR_BIN_COREDLL64, L"BIN", coredll64)) __crash();
		injector86 = session_res / L"X86InjectHelper.dll";
		if (!FreeResFile(IDR_BIN_INJECTHELPER86, L"BIN", injector86)) __crash();
		injector64 = session_res / L"InjectHelper.dll";
		if (!FreeResFile(IDR_BIN_INJECTHELPER64, L"BIN", injector64)) __crash();
	}
}



void WindowsService::ServiceCoreThread() {
	// Local variables
	map<DWORD, HANDLE> sessionProcesses;
	std::array<HANDLE, 2> InjectHelperProcess{ NULL, NULL };
	vector<HANDLE> waitObjects;
	constexpr DWORD WORKER_SLEEPTIME = 2000;
	ReportStatus(SERVICE_RUNNING);

	std::thread Helperx86, Helperx64;
	HANDLE hCrashpadHandler{};

	// rpc server
	m_rpcServer.Start(m_serviceName);

	while (!m_stopRequested) {
		if (m_pauseRequested) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			continue;
		}

		// X86 ([0]) or X64 ([1]) inject helper
		for (size_t index = 0, size = InjectHelperProcess.size(); index < size; ++index) {
			auto& i = InjectHelperProcess.at(index);
			if (!(!i || WaitForSingleObject(i, 0) == WAIT_OBJECT_0) || i == INVALID_HANDLE_VALUE) continue;
			// died or not started
			if (i) CloseHandle(i);
			bool isX86 = index == 0;
			auto& rundll32 = isX86 ? RunDLL_X86 : RunDLL_X64;
			auto& dllfile = isX86 ? injector86 : injector64;
			auto& injector_in = isX86 ? injector86_in : injector64_in;
			auto& injector_out = isX86 ? injector86_out : injector64_out;
			STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
			si.dwFlags = STARTF_USESTDHANDLES;
			if (injector_in) { CloseHandle(injector_in); injector_in = NULL; }
			if (injector_out) { CloseHandle(injector_out); injector_out = NULL; }
			SECURITY_ATTRIBUTES caninherit{ .nLength = sizeof(caninherit), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };
			HANDLE subp_stdin{}, subp_stdout{};
			if (!(CreatePipe(&subp_stdin, &injector_in, &caninherit, 0) && subp_stdin && injector_in && 
				CreatePipe(&injector_out, &subp_stdout, &caninherit, 0) && subp_stdout && injector_out)) {
				__crash();
			}
			si.hStdInput = subp_stdin;
			si.hStdError = si.hStdOutput = subp_stdout;
			wstring cmd = L"RunDLL32 \"" + dllfile + L"\",RunDLL /=help[] /password=0812 " +
				to_wstring(GetCurrentProcessId()) + L" " + 
				to_wstring((ULONG_PTR)injector_out) + L" " + to_wstring((ULONG_PTR)injector_in);
			if (!CreateProcessW(rundll32.c_str(), cmd.data(), NULL, NULL, TRUE,
				CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
				// Cannot create helper!!! TODO: Log the event
				__crash();
			}
			CloseHandle(subp_stdin); CloseHandle(subp_stdout);
			InjectHelperProcess[index] = pi.hProcess;
			auto& helper = isX86 ? Helperx86 : Helperx64;
			if (helper.joinable()) helper.join();
			helper = std::thread([this](HANDLE hFile) { return this->InjectHelperDataEater(hFile); }, injector_out);
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);
		}
		for (auto& i : InjectHelperProcess) if (i) waitObjects.push_back(i);

		// user detector
		do {
			vector<DWORD> need_delete;
			for (auto& i : sessionProcesses) {
				if (i.second && WaitForSingleObject(i.second, 0) == WAIT_OBJECT_0) {
					// died
					CloseHandle(i.second);
					need_delete.push_back(i.first);
				}
			}
			for (auto& i : need_delete) sessionProcesses.erase(i);

			PWTS_SESSION_INFOW pWtsSessionInfo{}; DWORD c{};
			if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pWtsSessionInfo, &c)) {
				for (auto& i : sessionProcesses) if (i.second) waitObjects.push_back(i.second);
				break;
			}

			//DWORD activeUser = WTSGetActiveConsoleSessionId();
			//if (!sessionProcesses.contains(activeUser) && activeUser != 0 && activeUser != 0xFFFFFFFF) 
			for (DWORD dwI = 0; dwI < c; ++dwI) {
				PWSTR pWtsUserName{}; DWORD dwSize{};
				if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, pWtsSessionInfo[dwI].SessionId,
					WTSUserName, &pWtsUserName, &dwSize)) continue;
				wstring user = pWtsUserName;
				WTSFreeMemory(pWtsUserName);
				if (user.empty()) continue;
				if (sessionProcesses.contains(dwI)) continue;
				// launch a worker in this session
				wstring cmd = L"workerw --type=session-worker --name=\"" + m_serviceName + L"\" --ppid=" + to_wstring(GetCurrentProcessId());
				STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
				if (!CreateProcessInSession(pWtsSessionInfo[dwI].SessionId, appPath.get(), cmd.data(),
					NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi, true)) {
					sessionProcesses.insert(make_pair(pWtsSessionInfo[dwI].SessionId, nullptr));
					// TODO: log the error
					break;
				}
				sessionProcesses.insert(make_pair(pWtsSessionInfo[dwI].SessionId, pi.hProcess));
				ResumeThread(pi.hThread);
				CloseHandle(pi.hThread);
			}

			WTSFreeMemory(pWtsSessionInfo);

			for (auto& i : sessionProcesses) if (i.second) waitObjects.push_back(i.second);
		} while (0);

		// wait
		waitObjects.push_back(stopEvent);
		WaitForMultipleObjects((DWORD)waitObjects.size(), waitObjects.data(), FALSE, WORKER_SLEEPTIME);
		waitObjects.clear();
	}

	// cleanup
	m_rpcServer.Stop();
	if (injector86_in) CloseHandle(injector86_in);
	if (injector86_out) CloseHandle(injector86_out);
	if (injector64_in) CloseHandle(injector64_in);
	if (injector64_out) CloseHandle(injector64_out);
	if (hCrashpadHandler) CloseHandle(hCrashpadHandler);
	if (Helperx86.joinable()) { Helperx86.join(); }
	if (Helperx64.joinable()) { Helperx64.join(); }
	for (auto& i : InjectHelperProcess) CloseHandle(i);
	for (auto& i : sessionProcesses) CloseHandle(i.second);

}

void WindowsService::InjectHelperDataEater(HANDLE hPipe) {
	uint8_t buffer[4096]{};
	DWORD dwReaded{};
	while (ReadFile(hPipe, buffer, 4096, &dwReaded, NULL)) {
		// TODO: Post event to main thread
	}
}


