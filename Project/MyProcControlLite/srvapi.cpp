#include "srvapi.hpp"
#include "service_h.h"
#include "processhelper.h"
#include <w32use.hpp>
#include <stdlib.h>
#include <userenv.h>
#include <memory>
#pragma comment(lib, "RpcRT4.lib")

using namespace std;

int MyProcControlLite_LaunchWithControl_Impl2(handle_t IDL_handle, PCWSTR application, PCWSTR cmdline, int* bSuccess, unsigned long* error);

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

	// TODO: inject

	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	*bSuccess = 1;
	*error = 0;
	return 0;
}




