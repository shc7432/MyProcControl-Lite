#include "srv.hpp"
#include <chrono>
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
}

WindowsService::~WindowsService() {
	if (m_coreThread.joinable()) {
		m_coreThread.join();
	}
	if (m_stopThread.joinable()) {
		m_stopThread.join();
	}
}

static VOID srv_main(
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

void WindowsService::ServiceCoreThread() {
	// 这里是服务核心逻辑
	while (!m_stopRequested) {
		if (m_pauseRequested) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		// 实际工作代码放在这里


		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

void WindowsService::ServiceStopThread() {
	// 执行停止服务的清理工作
	ResumeThread(m_coreThread.native_handle());

	Sleep(1000);
	m_stopRequested = true;
	ReportStatus(SERVICE_STOP_PENDING);

	// 
	if (m_coreThread.joinable()) {
		if (WAIT_TIMEOUT == WaitForSingleObject(m_coreThread.native_handle(), 2000)) {
#pragma warning(push)
#pragma warning(disable: 6258)
            TerminateThread(m_coreThread.native_handle(), 0);
#pragma warning(pop)
		}
	}

	// 报告服务已停止
	ReportStatus(SERVICE_STOPPED);
}

bool WindowsService::OnInitialize() {
	// 注册服务控制处理函数
	m_statusHandle = RegisterServiceCtrlHandlerExW(m_serviceName.c_str(), ServiceCtrlHandler, this);
	if (!m_statusHandle) {
		return false;
	}

	// 报告服务正在启动
	ReportStatus(SERVICE_START_PENDING);
	return true;
}

void WindowsService::OnStart() {
	if (!OnInitialize()) {
		return;
	}

	// 报告服务正在运行
	m_isRunning = true;
	ReportStatus(SERVICE_RUNNING);

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
		ReportStatus(SERVICE_RUNNING);
	}
}

void WindowsService::OnStop() {
	if (m_status.dwCurrentState == SERVICE_STOPPED) {
		return;
	}

	// 报告服务正在停止
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

// 计算机关机
void WindowsService::OnShutdown() {
	ReportStatus(SERVICE_STOPPED);
	m_isRunning = false;
	Sleep(500);
	ExitProcess(0);
}
