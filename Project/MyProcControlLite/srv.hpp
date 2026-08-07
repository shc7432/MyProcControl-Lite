#pragma once
#include "targetver.h"
#include "srvapi.hpp"
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>

class WindowsService {
public:
	WindowsService(const std::wstring& serviceName);
	virtual ~WindowsService();

	// 主服务入口点
	void Run();

	// 服务控制处理函数
	static DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext);

	// 服务启动
	virtual void OnStart();

protected:
	// 服务核心线程函数
	void ServiceCoreThread();

	// 服务停止处理线程函数
	void ServiceStopThread();

	// 服务初始化
	virtual bool OnInitialize();

	// 服务暂停
	virtual void OnPause();

	// 服务继续
	virtual void OnContinue();

	// 服务停止
	virtual void OnStop();

	// 计算机关机
	void OnShutdown();

	// 报告服务状态
	void ReportStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode = NO_ERROR, DWORD dwWaitHint = 8000);

	void InjectHelperDataEater(HANDLE hPipe);

private:
	std::wstring m_serviceName;
	SERVICE_STATUS_HANDLE m_statusHandle;
	SERVICE_STATUS m_status;
	std::thread m_coreThread;
	std::thread m_stopThread;
	HANDLE stopEvent;
	std::atomic<bool> m_stopRequested;
	std::atomic<bool> m_pauseRequested;
	std::atomic<bool> m_isRunning;
	MyProcControl_Lite::RpcServer m_rpcServer;
	HANDLE injector86_in, injector86_out, injector64_in, injector64_out;
};