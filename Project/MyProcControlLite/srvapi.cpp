#include "srvapi.hpp"
#include "service_h.h"
#include <cstdlib>

#pragma comment(lib, "RpcRT4.lib")

//////////////////////////////////////////////////////////////////////////////
// MIDL user callbacks — required by service_c.c and service_s_wrapper.c
//

extern "C" {

void __RPC_USER MIDL_user_free(void* p)
{
	free(p);
}

void* __RPC_USER MIDL_user_allocate(size_t size)
{
	return malloc(size);
}

//////////////////////////////////////////////////////////////////////////////
// RPC Server: IServiceRpc — implementation (C-linkage, called via
// IServiceRpc_ServerRoutineTable in service_s.c, renamed by wrapper)
//

int MyProcControlLite_LaunchWithControl_Impl(
		/* [in] */ handle_t IDL_handle,
		/* [string][in] */ const wchar_t* application,
		/* [string][in] */ const wchar_t* cmdline,
		/* [out] */ int* bSuccess)
	{
		(void)IDL_handle;
		(void)application;
		(void)cmdline;

		// Stub: always report success.
		// TODO: implement actual process launch with control (injection / hooking).
		*bSuccess = 1;
		return 0; // RPC_S_OK
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
	if (status != RPC_S_OK && status != RPC_S_DUPLICATE_ENDPOINT) return false;

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

	RPC_STATUS status = RpcServerUnregisterIfEx(
		IServiceRpc_v1_0_s_ifspec,
		NULL,
		TRUE);
	m_running.store(false);
	return (status == RPC_S_OK);
}
