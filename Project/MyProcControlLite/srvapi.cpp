#include "srvapi.hpp"
#include "../out/generated/midl/service_h.h"
#include "processhelper.h"
#include "injectusinghelper.hpp"
#include <w32use.hpp>
#include <stdlib.h>
#include <userenv.h>
#include <memory>
#pragma comment(lib, "RpcRT4.lib")

using namespace std;

int MyProcControlLite_LaunchWithControl_Impl2(handle_t IDL_handle, PCWSTR application, PCWSTR cmdline, int* bSuccess, unsigned long* error);
int MyProcControlLite_Consent_CreateProcess_Impl2(
	/* [in] */ handle_t IDL_handle,
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
	unsigned long* custom_err_code
);
int MyProcControlLite_RequestAddControl_Impl2(handle_t IDL_handle, unsigned long dwProcessId, unsigned long* error);

//////////////////////////////////////////////////////////////////////////////
// MIDL user callbacks — required by service_c.c and service_s_wrapper.c
//

extern "C" {

void __RPC_USER MIDL_user_free(_Pre_maybenull_ _Post_invalid_ void* p)
{
	free(p);
}

_Must_inspect_result_
_Ret_maybenull_ _Post_writable_byte_size_(size)
void* __RPC_USER MIDL_user_allocate(_In_ size_t size)
{
	return malloc(size);
}

//////////////////////////////////////////////////////////////////////////////
// RPC Server: IServiceRpc — implementation
//

int MyProcControlLite_LaunchWithControl_Impl(
		/* [in] */ handle_t IDL_handle,
		/* [string][in] */ const wchar_t* application,
		/* [string][in] */ const wchar_t* cmdline,
		/* [out] */ int* bSuccess,
		/* [out] */ unsigned long* error)
	{
		return MyProcControlLite_LaunchWithControl_Impl2(IDL_handle, application, cmdline, bSuccess, error);
	}

int MyProcControlLite_Consent_CreateProcess_Impl(
	/* [in] */ handle_t IDL_handle,
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
	unsigned long* custom_err_code
) {
	return MyProcControlLite_Consent_CreateProcess_Impl2(
		IDL_handle,
		application,
		cmdline,
		inherithandles,
		flags,
		cd,
		sisize,
		custom_err_code
	);
}

int MyProcControlLite_RequestAddControl_Impl(handle_t IDL_handle, unsigned long dwProcessId, unsigned long* error) {
	return MyProcControlLite_RequestAddControl_Impl2(IDL_handle, dwProcessId, error);
}

} // extern "C"

//////////////////////////////////////////////////////////////////////////////
// RpcServer
//

MyProcControl_Lite::RpcServer::~RpcServer()
{
	Stop();
}

bool MyProcControl_Lite::RpcServer::Start(const std::wstring& serviceName)
{
	if (m_running.load()) return true;

	m_endpoint = L"MyProcControlLiteRpc_" + serviceName;

	RPC_STATUS status = RpcServerUseProtseqEpW(
		(RPC_WSTR)L"ncalrpc",
		RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
		(RPC_WSTR)m_endpoint.c_str(),
		NULL);
	if (status != RPC_S_OK) return false;

	status = RpcServerRegisterIfEx(
		IServiceRpc_v1_0_s_ifspec,
		NULL,
		NULL,
		RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH,
		RPC_C_LISTEN_MAX_CALLS_DEFAULT,
		NULL);
	if (status != RPC_S_OK) return false;

	status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE);
	if (status != RPC_S_OK && status != RPC_S_ALREADY_LISTENING) return false;

	m_running.store(true);
	return true;
}

bool MyProcControl_Lite::RpcServer::Stop()
{
	if (!m_running.load()) return true;

	//RpcMgmtStopServerListening(nullptr);// FIXME: 
	RPC_STATUS status = RpcServerUnregisterIfEx(
		IServiceRpc_v1_0_s_ifspec,
		NULL,
		TRUE);
	m_running.store(false);
	return (status == RPC_S_OK);
}

// ---------------------

int MyProcControlLite_LaunchWithControl_Impl2(handle_t IDL_handle, PCWSTR application, PCWSTR cmdline, int* bSuccess, unsigned long* error) {
	if (!application || !cmdline || !bSuccess || !error) {
		if (bSuccess) *bSuccess = 0;
		if (error) *error = ERROR_INVALID_PARAMETER;
		return 0;
	}
	RPC_STATUS status;
	ULONG client_pid = 0;
	status = I_RpcBindingInqLocalClientPID(IDL_handle, &client_pid);
	if (status != RPC_S_OK) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}

	w32ProcessHandle hCaller;
	HANDLE hToken{}, hImpToken{};
	try {
		hCaller = OpenProcess(
			PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SUSPEND_RESUME | PROCESS_CREATE_PROCESS,
			FALSE, client_pid);
		OpenProcessToken(hCaller, TOKEN_DUPLICATE | TOKEN_QUERY, &hImpToken);
		if (!hImpToken) throw runtime_error("");
		DuplicateTokenEx(hImpToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &hToken);
		CloseHandle(hImpToken);
		if (!hToken) throw runtime_error("");
	}
	catch (...) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}

	if (!NT_SUCCESS(app::SuspendProcess(hCaller))) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}
	w32oop::util::RAIIHelper c([&] { CloseHandle(hToken); app::ResumeProcess(hCaller); });

	DWORD dwSessionId{};
	if (!ProcessIdToSessionId(client_pid, &dwSessionId)) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}
	if (!SetTokenInformation(hToken, TokenSessionId, (void*)&dwSessionId, sizeof(DWORD))) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}

	STARTUPINFOEXW si{ sizeof(si) };
	PROCESS_INFORMATION pi{};
	std::unique_ptr<uint8_t[]> attributeList;

	DWORD flags = CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB | EXTENDED_STARTUPINFO_PRESENT;
	
	SIZE_T need{};
	bool ok = false;
	HANDLE hRawHandle = hCaller;
	InitializeProcThreadAttributeList(0, 1, 0, &need);
	if (need && need < 32768) {
		attributeList = make_unique<uint8_t[]>(need);
		if (InitializeProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get(),
			1, 0, &need)) {
			if (UpdateProcThreadAttribute(
				(PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get(), 0,
				PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
				&hRawHandle,
				sizeof(HANDLE),
				NULL, NULL
			)) {
				ok = true;
			}
		}
	}
	if (!ok) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}

	si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
	si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
	si.StartupInfo.wShowWindow = SW_SHOWNORMAL;
	si.lpAttributeList = PPROC_THREAD_ATTRIBUTE_LIST(attributeList ? attributeList.get() : nullptr);

	// 创建用户环境块
	LPVOID pEnv = NULL;
	if (CreateEnvironmentBlock(&pEnv, hToken, TRUE)) {
		flags |= CREATE_UNICODE_ENVIRONMENT;
	}

	wstring cmd = cmdline;
	if (!CreateProcessAsUserW(hToken, application, cmd.data(),
		NULL, NULL, FALSE, flags, pEnv, NULL, (LPSTARTUPINFOW)&si, &pi)) {
		*error = GetLastError();
		if (attributeList) DeleteProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get());
		if (pEnv) DestroyEnvironmentBlock(pEnv);
		*bSuccess = 0;
		return 0;
	}
	if (attributeList) DeleteProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get());
	if (pEnv) DestroyEnvironmentBlock(pEnv);

	BOOL isWOW{};
	if (!IsWow64Process(pi.hProcess, &isWOW) || !MyProcControl_Lite::InjectCoreDllUsingHelper(pi.dwProcessId, isWOW)) {
		*error = GetLastError();
		TerminateProcess(pi.hProcess, ERROR_INTERNAL_ERROR);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		*bSuccess = 0;
		return 0;
	}

	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	*bSuccess = 1;
	*error = 0;
	return 0;
}



int MyProcControlLite_Consent_CreateProcess_Impl2(
	/* [in] */ handle_t IDL_handle,
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
	unsigned long* custom_err_code
) {
	static std::recursive_mutex onlyOneInst;
	// get caller info
	auto appPath = make_shared<WCHAR[]>(32768);
	RPC_STATUS status;
	ULONG client_pid = 0;
	status = I_RpcBindingInqLocalClientPID(IDL_handle, &client_pid);
	if (status != RPC_S_OK || !GetModuleFileNameW(NULL, appPath.get(), 32768)) {
		*custom_err_code = GetLastError();
		return 0;
	}

	// check params
	if (!application || !cmdline || !cd || !custom_err_code) {
		if (custom_err_code) *custom_err_code = ERROR_INVALID_PARAMETER;
		return 0;
	}

	w32ProcessHandle hCaller;
	try {
		hCaller = OpenProcess(
			PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SUSPEND_RESUME,
			FALSE, client_pid);
	}
	catch (...) {
		*custom_err_code = GetLastError();
		return 0;
	}

	if (!NT_SUCCESS(app::SuspendProcess(hCaller))) {
		*custom_err_code = GetLastError();
		return 0;
	}
	w32oop::util::RAIIHelper c([&] { app::ResumeProcess(hCaller); });

	wstring callerPath, callerName;
	{
		auto callerPathPtr = make_shared<WCHAR[]>(32768);DWORD size = 32768;
		if (!QueryFullProcessImageNameW(hCaller, 0, callerPathPtr.get(), &size)) {
			*custom_err_code = GetLastError();
			return 0;
		}
		callerPath = callerPathPtr.get();
		size_t pos = callerPath.find_last_of(L'\\');
		if (pos != std::wstring::npos) {
			callerName = callerPath.substr(pos + 1);
		}
		else {
			callerName = callerPath;
		}
	}

	DWORD dwSessionId{};
	if (!ProcessIdToSessionId(client_pid, &dwSessionId)) {
		*custom_err_code = GetLastError();
		return 0;
	}

	// spawn a consent dialog
	wstring detailedDetails = std::format(
		L"Process name: {}\nPID: {}\nProcess file: {}\nTarget application: {}\nInherit handles?: {}\n"
		L"Creation flags: {}\nCurrent directory: {}\nStartupInfo Structure Size: {}\nCommand line:\n{}",
		callerName, client_pid, callerPath, application, inherithandles ? L"Yes" : L"No",
		flags, cd, sisize, cmdline
	);
	wstring randomNonce = GenerateUUIDW();
	w32oop::util::str::operations::replace(detailedDetails, L"\n", randomNonce);
	w32oop::util::str::operations::replace(detailedDetails, L"\\", L"\\\\");
	wstring cmd = std::format(L"consent.exe --type=consent --extra1=\"{}\" --extra2=CreateProcess --extra3=Allow --extra4=Deny "
		L"--extra5=n --extra6=30 --extra7=1883 --extra8=\"{}\" --extra9={}", callerName,
		w32oop::util::str::operations::replace(detailedDetails, L"\"", L"\\\""), randomNonce
	); // TODO: add i18n; allow remember; allow customize timeout
	STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
	std::lock_guard _gg(onlyOneInst);
	if (!CreateProcessInSession(dwSessionId, appPath.get(), cmd.data(),
		NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi, true)) {
		*custom_err_code = GetLastError();
		return 0;
	}

	DWORD code{};
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	if (WAIT_TIMEOUT == WaitForSingleObject(pi.hProcess, 35000)) {
		TerminateProcess(pi.hProcess, 0);
	}
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);

	bool acc = code & 0xF0000000, remember = code & 0x0F000000;
	// TODO: implement remember
	if (acc) return 1;

	// TODO: allow user to customize error code
	*custom_err_code = ERROR_CHILD_PROCESS_BLOCKED;
	return 0;
}


int MyProcControlLite_RequestAddControl_Impl2(handle_t IDL_handle, unsigned long dwProcessId, unsigned long* error) {
	if (!error) {
		return 0;
	}
	RPC_STATUS status;
	ULONG client_pid = 0;
	status = I_RpcBindingInqLocalClientPID(IDL_handle, &client_pid);
	if (status != RPC_S_OK) {
		*error = GetLastError();
		return 0;
	}

	w32ProcessHandle hCaller;
	HANDLE hToken{};
	try {
		hCaller = OpenProcess(
			PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SUSPEND_RESUME | PROCESS_CREATE_PROCESS,
			FALSE, client_pid);
		HANDLE hImpToken{};
		OpenProcessToken(hCaller, TOKEN_DUPLICATE | TOKEN_QUERY, &hImpToken);
		if (!hImpToken) throw runtime_error("");
		DuplicateTokenEx(hImpToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenImpersonation, &hToken);
		CloseHandle(hImpToken);
		if (!hToken) throw runtime_error("");
	}
	catch (...) {
		*error = GetLastError();
		return 0;
	}

	if (!NT_SUCCESS(app::SuspendProcess(hCaller))) {
		*error = GetLastError();
		return 0;
	}
	w32oop::util::RAIIHelper c([&] { CloseHandle(hToken); app::ResumeProcess(hCaller); });

	// check whether the caller process has permission to control the target process
	bool permok = false; BOOL isWOW{};
	ImpersonateLoggedOnUser(hToken);
	do {
		HANDLE hProcess = OpenProcess(GENERIC_READ | GENERIC_WRITE | PROCESS_CREATE_THREAD, FALSE, dwProcessId);
		if (!hProcess) break;
		if (!IsWow64Process(hProcess, &isWOW)) {
			CloseHandle(hProcess);
			break;
		}
		HANDLE hThread = CreateRemoteThread(hProcess, NULL, NULL, NULL, NULL, CREATE_SUSPENDED, NULL);
		if (!hThread) break;
#pragma warning(push)
#pragma warning(disable: 6258)
		TerminateThread(hThread, 0);
#pragma warning(pop)
		CloseHandle(hThread);
		CloseHandle(hProcess);
		permok = true;
	} while (0);
	RevertToSelf();

	if (!permok) {
		*error = 0xC0000022;
		return 0;
	}

	if (!MyProcControl_Lite::InjectCoreDllUsingHelper(dwProcessId, isWOW)) {
		*error = GetLastError();
		return 0;
	}

	*error = 0;
	return 1;
}





