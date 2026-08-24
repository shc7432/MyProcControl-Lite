#include "srvapi.hpp"
#include "srv.hpp"
#include "../out/generated/midl/service_h.h"
#include "processhelper.h"
#include "injectusinghelper.hpp"
#include "../lib/sha256/sha256.h"
#include <w32use.hpp>
#include <stdlib.h>
#include <userenv.h>
#include <WtsApi32.h>
#include <memory>
#pragma comment(lib, "RpcRT4.lib")

using namespace std;

void MyProcControlLite_Server_OnEnumUserSession(DWORD count, PWTS_SESSION_INFOW pWtsSessionInfo);

int MyProcControlLite_ConsentUI_CheckAuthorization_Impl2(handle_t IDL_handle, const wchar_t* payload, const wchar_t* sig);
int MyProcControlLite_ScControl_Impl2(handle_t IDL_handle, unsigned long control_name, unsigned long long payload, unsigned long* result);
int MyProcControlLite_LaunchWithControl_Impl2(handle_t IDL_handle, PCWSTR application, PCWSTR cmdline, int* bSuccess, unsigned long* error);
int MyProcControlLite_Consent_CreateProcess_Impl2(
	/* [in] */ handle_t IDL_handle,
	int type,
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
	unsigned long long token_value,
	unsigned long logon_flags,
	const wchar_t* username,
	const wchar_t* domain,
	const wchar_t* password,
	unsigned long* custom_err_code
);
int MyProcControlLite_RequestAddControl_Impl2(handle_t IDL_handle, unsigned long dwProcessId, unsigned long* error);

using namespace MyProcControl_Lite::ServiceCore;
std::wstring MyProcControl_Lite::ServiceCore::consent_secret;
static std::map<DWORD, std::recursive_mutex> consentUI_onlyOneInst;
static std::recursive_mutex consentUI_onlyOneInst_accessLock;
std::map<DWORD, time_t> MyProcControl_Lite::ServiceCore::consentUI_BlockUntil;
std::recursive_mutex MyProcControl_Lite::ServiceCore::consentUI_BlockUntil_accessLock;
std::recursive_mutex MyProcControl_Lite::consentUI_HighPermOpGlobalLock;

auto MyProcControl_Lite::ServiceCore::AcquireSessionConsentUILock(DWORD sessionId) -> std::unique_lock<std::recursive_mutex> {
	if (consentUI_onlyOneInst.contains(sessionId) == false) {
		std::lock_guard gg(consentUI_onlyOneInst_accessLock);
		consentUI_onlyOneInst.emplace(std::piecewise_construct, std::forward_as_tuple(sessionId), std::forward_as_tuple());
	}
	return std::unique_lock(consentUI_onlyOneInst.at(sessionId));
}

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

int MyProcControlLite_ConsentUI_CheckAuthorization_Impl(handle_t IDL_handle, const wchar_t* payload, const wchar_t* sig) {
	return MyProcControlLite_ConsentUI_CheckAuthorization_Impl2(IDL_handle, payload, sig);
}

int MyProcControlLite_ScControl_Impl(handle_t IDL_handle, unsigned long control_name, unsigned long long payload, unsigned long* result) {
	return MyProcControlLite_ScControl_Impl2(IDL_handle, control_name, payload, result);
}

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
	int type,
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
	unsigned long long token_value,
	unsigned long logon_flags,
	const wchar_t* username,
	const wchar_t* domain,
	const wchar_t* password,
	unsigned long* custom_err_code
) {
	return MyProcControlLite_Consent_CreateProcess_Impl2(
		IDL_handle,
		type,
		application,
		cmdline,
		inherithandles,
		flags,
		cd,
		sisize,
		token_value,
		logon_flags,
		username,
		domain,
		password,
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
	consent_secret = GenerateUUIDW();

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

	consent_secret = L"";

	(void)RpcMgmtStopServerListening(nullptr);
	RPC_STATUS status = RpcServerUnregisterIfEx(
		IServiceRpc_v1_0_s_ifspec,
		NULL,
		TRUE);
	m_running.store(false);
	return (status == RPC_S_OK);
}

// ---------------------

void MyProcControlLite_Server_OnEnumUserSession(DWORD count, PWTS_SESSION_INFOW pWtsSessionInfo) {
	// collect alive sessions
	std::set<DWORD> alive_sessions;
	for (DWORD dwI = 0; dwI < count; ++dwI) {
		alive_sessions.insert((pWtsSessionInfo + dwI)->SessionId);
	}
	{
		std::lock_guard gg(consentUI_onlyOneInst_accessLock);
		std::erase_if(consentUI_onlyOneInst, [&](const auto& pair) {
			return !alive_sessions.contains(pair.first);
		});
	}
	{
		std::lock_guard gg(consentUI_BlockUntil_accessLock);
		std::erase_if(consentUI_BlockUntil, [&](const auto& pair) {
			return !alive_sessions.contains(pair.first);
		});
	}
}

wstring MyProcControl_Lite::ServiceCore::calculate_consent_sig(const wstring& payload, time_t r) {
	if (!r) r = time(0) / 10;
	wstring full_payload = to_wstring(payload.size()) + L"|" + payload + L"|" + consent_secret + L"|" + to_wstring(r);
	char buffer[256]{};
	sha256_easy_hash_hex(full_payload.data(), full_payload.size() * sizeof(typename decltype(full_payload)::value_type), buffer);

	return w32oop::util::str::encodings::utf8_utf16(buffer);
}

set<wstring> MyProcControl_Lite::ServiceCore::calculate_possible_consent_sig(const wstring& payload) {
	time_t t = time(0) / 10;
	return set<wstring>{
		calculate_consent_sig(payload, t - 1),
		calculate_consent_sig(payload, t),
		calculate_consent_sig(payload, t + 1),
	};
}

int MyProcControlLite_ConsentUI_CheckAuthorization_Impl2(handle_t IDL_handle, const wchar_t* payload, const wchar_t* sig) {
	if (!payload || !sig) return 0;
	return !!(MyProcControl_Lite::ServiceCore::calculate_possible_consent_sig(payload).contains(sig));
}

bool MyProcControl_Lite::ConsentVerifySignature(std::wstring payload, std::wstring sig, std::wstring endpoint) {
	return [] (PCWSTR p, PCWSTR s, PCWSTR e) -> bool {
		RPC_WSTR bindingStr = nullptr;
		RPC_STATUS status = RpcStringBindingComposeW(
			nullptr,
			(RPC_WSTR)L"ncalrpc",
			nullptr,
			(RPC_WSTR)e,
			nullptr,
			&bindingStr);
		if (status != RPC_S_OK) {
			SetLastError((DWORD)status);
			return false;
		}

		RPC_BINDING_HANDLE hBinding = nullptr;
		status = RpcBindingFromStringBindingW(bindingStr, &hBinding);
		RpcStringFreeW(&bindingStr);
		if (status != RPC_S_OK) {
			SetLastError((DWORD)status);
			return false;
		}

		int bSuccess = 0;
		unsigned long error = 0;
		int rpcRet = 0;
		RpcTryExcept{
			rpcRet = MyProcControlLite_ConsentUI_CheckAuthorization(hBinding, p, s);
		}
		RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
			RpcBindingFree(&hBinding);
			SetLastError((DWORD)RPC_S_CALL_FAILED);
			return false;
		}
		RpcEndExcept

		RpcBindingFree(&hBinding);

		return rpcRet;
	} (payload.c_str(), sig.c_str(), endpoint.c_str());
}

int MyProcControlLite_ScControl_Impl2(handle_t IDL_handle, unsigned long control_name, unsigned long long payload, unsigned long* result) {
	if (!result) return 0;
	RPC_STATUS status;
	ULONG client_pid = 0;
	status = I_RpcBindingInqLocalClientPID(IDL_handle, &client_pid);
	if (status != RPC_S_OK) {
		*result = GetLastError();
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
		*result = GetLastError();
		return 0;
	}

	if (!NT_SUCCESS(app::SuspendProcess(hCaller))) {
		*result = GetLastError();
		CloseHandle(hToken);
		return 0;
	}
	w32oop::util::RAIIHelper c([&] { CloseHandle(hToken); app::ResumeProcess(hCaller); });

	wstring callerPath, callerName;
	{
		auto callerPathPtr = make_shared<WCHAR[]>(32768);DWORD size = 32768;
		if (!QueryFullProcessImageNameW(hCaller, 0, callerPathPtr.get(), &size)) {
			*result = GetLastError();
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
	bool bWasNotInteractive = false;
	if (!ProcessIdToSessionId(client_pid, &dwSessionId)) {
		*result = GetLastError();
		return 0;
	}
	if (dwSessionId == 0) {
		// non-interactive session, make it interactive
		dwSessionId = WTSGetActiveConsoleSessionId();
		bWasNotInteractive = true;
	}

	auto ensurePerm = [hToken, result] {
		// check whether the caller process has permission to control the service
		if (!app::IsTokenAdministrators(hToken)) {
			*result = 0xC0000022;
			return 0;
		}
		return 1;
	};

	// control
	switch (control_name) {
	case 1:
		// 1: query protection status
		*result = ServiceCoreProcess::Instance->IsProtectionDisabled() ? 0 : 1;
		return 1;
	case 2:
		// 2: set protection status
		if (!ensurePerm()) return 0;
		if (bWasNotInteractive) {
			// "set prot status" must not be called from non-interactive sessions
			*result = STATUS_ASSERTION_FAILURE;
			return 0;
		}
		{
			bool prot = !ServiceCoreProcess::Instance->IsProtectionDisabled();
			bool newProt = payload;
			if (prot == newProt) {
				*result = ERROR_REQUEST_OUT_OF_SEQUENCE;
				return 0;
			}
			// update state
			return ServiceCoreProcess::Instance->RequestChangeProtection(client_pid, dwSessionId, callerName, callerPath, newProt, result);
		}
		break;
	}
	return 0;
}

bool MyProcControl_Lite::ServiceCore::_XxxxInternalPopSecondaryConsentDialog(
	DWORD client_pid, DWORD dwSessionId,
	std::wstring app, std::wstring req, std::wstring detailsText,
	std::wstring allowBtn, std::wstring denyBtn, bool* remember,
	DWORD timeout, bool showSplitMenu, bool wasNotInteractive, size_t _MaxRetries
) {
	DWORD prevErr = GetLastError();
	auto _gg = AcquireSessionConsentUILock(dwSessionId);
	if (consentUI_BlockUntil.contains(dwSessionId)) do {
		std::lock_guard gg(consentUI_BlockUntil_accessLock);
		if (!consentUI_BlockUntil.contains(dwSessionId)) break;
		if (time(0) < consentUI_BlockUntil.at(dwSessionId)) {
			SetLastError(prevErr);
			return 0;
		}
		consentUI_BlockUntil.erase(dwSessionId);
	} while (0);
	auto appPath = make_shared<WCHAR[]>(32768);
	if (!GetModuleFileNameW(NULL, appPath.get(), 32768)) return false;
	size_t nRetries = 0;
	STARTUPINFOW si{}; PROCESS_INFORMATION pi{};
pstart:
	wstring t = detailsText;
	wstring randomNonce = GenerateUUIDW();
	w32oop::util::str::operations::replace(t, L"\n", randomNonce);
	wstring sig = MyProcControl_Lite::ServiceCore::calculate_consent_sig(t);
	w32oop::util::str::operations::replace(t, L"\\", L"\\\\");
	wstring cmd = std::format(L"consent.exe --type=consent --action=secondary --name=\"{}\" "
		L"--extra1=\"{}\" --extra2=\"{}\" --extra3=\"{}\" --extra4=\"{}\" "
		L"--extra5={} --extra6={} --extra7=1883 --extra8=\"{}\" --extra9={} --extra10={} "
		L"--extra11={} --signature={}",
		ServiceCoreProcess::Instance->getName(), app, req, allowBtn, denyBtn, remember ? L"y" : L"n",
		timeout, w32oop::util::str::operations::replace(t, L"\"", L"\\\""), randomNonce,
		showSplitMenu ? L"y" : L"n", wasNotInteractive ? L"y" : L"n", sig
	); // TODO: add i18n; allow remember
	RtlZeroMemory(&si, sizeof(si));
	RtlZeroMemory(&pi, sizeof(pi));
	si.cb = sizeof(si);
	if (!CreateProcessInSession(dwSessionId, appPath.get(), cmd.data(),
		NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi, true)) {
		return false;
	}
	DWORD code{};
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	if (WAIT_OBJECT_0 != WaitForMultipleObjects(2, (array<decltype(pi.hProcess), 2>{
		pi.hProcess, ServiceCoreProcess::Instance->getStopEvent()
	}).data(), 0, ServiceCoreProcess::Instance->ConsentCalculateMaximiumWait())) {
		TerminateProcess(pi.hProcess, 0);
	}
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	if (code == ERROR_BUSY) {
		if (++nRetries > _MaxRetries) {
			SetLastError(prevErr ? prevErr : 0xC0000022);
			return false;
		}
		Sleep(500);
		goto pstart;
	}

	bool acc = code & 0x10000000, Myremember = code & 0x20000000, kill = code & 0x00100000, uninstall = code & 0x00200000,
		blockUntil = code & 0x00400000;
	// TODO: implement remember
	if (blockUntil) {
		time_t block_t = code & 0x000FFFFF;
		std::lock_guard gg(consentUI_BlockUntil_accessLock);
		consentUI_BlockUntil.emplace(dwSessionId, time(0) + block_t);
	}
	if (remember) *remember = Myremember;
	if (!acc) {
		if (kill || uninstall) {
			if (!app::KillOrUninstallApplication(client_pid, uninstall)) {
				WCHAR title[] = L"Error"; DWORD tmp{};
				wstring err = format(L"Cannot {} the application!\n\n{}",
					uninstall ? L"uninstall" : L"close", ErrorChecker().message());
				WTSSendMessageW(WTS_CURRENT_SERVER_HANDLE, dwSessionId, title, 10,
					err.data(), DWORD(err.size() * sizeof(decltype(err)::value_type)),
					MB_ICONERROR, 0, &tmp, FALSE);
			}
		}
		SetLastError(prevErr ? prevErr : 0xC0000022);
		return false;
	}
	return true;
}

int MyProcControlLite_LaunchWithControl_Impl2(handle_t IDL_handle, PCWSTR application, PCWSTR cmdline, int* bSuccess, unsigned long* error) {
	if (!application || !cmdline || !bSuccess || !error) {
		if (bSuccess) *bSuccess = 0;
		if (error) *error = ERROR_INVALID_PARAMETER;
		return 0;
	}
	auto appPath = make_shared<WCHAR[]>(32768);
	RPC_STATUS status;
	ULONG client_pid = 0;
	status = I_RpcBindingInqLocalClientPID(IDL_handle, &client_pid);
	if (status != RPC_S_OK || !GetModuleFileNameW(NULL, appPath.get(), 32768)) {
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
		CloseHandle(hToken);
		return 0;
	}
	w32oop::util::RAIIHelper c([&] { CloseHandle(hToken); app::ResumeProcess(hCaller); });

	wstring callerPath, callerName;
	{
		auto callerPathPtr = make_shared<WCHAR[]>(32768);DWORD size = 32768;
		if (!QueryFullProcessImageNameW(hCaller, 0, callerPathPtr.get(), &size)) {
			*bSuccess = 0;
			*error = GetLastError();
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

	DWORD dwSessionId{}, callerSession;
	bool bWasNotInteractive = false;
	if (!ProcessIdToSessionId(client_pid, &dwSessionId)) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}
	callerSession = dwSessionId;
	if (dwSessionId == 0) {
		// non-interactive session, make it interactive
		dwSessionId = WTSGetActiveConsoleSessionId();
		bWasNotInteractive = true;
	}
	if (!SetTokenInformation(hToken, TokenSessionId, (void*)&dwSessionId, sizeof(DWORD))) {
		*bSuccess = 0;
		*error = GetLastError();
		return 0;
	}

	// consent first
	if (!ServiceCoreProcess::Instance->IsProtectionDisabled()) {
		wstring detailedDetails = std::format(
			L"Process: ({}) {} [{}] | File: {}\nTarget application: {}\nCommand line:\n{}",
			client_pid, callerName, callerSession, callerPath, application, cmdline
		);
		SetLastError(0xC0000022);
		bool acc = _XxxxInternalPopSecondaryConsentDialog(client_pid, dwSessionId, callerName, L"LaunchWithControl",
			detailedDetails, L"Allow", L"Deny", NULL, ServiceCoreProcess::Instance->GetDefaultConsentTimeout(), true,
			bWasNotInteractive);
		if (!acc) {
			*bSuccess = 0;
			*error = GetLastError();
			return 0;
		}
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
	wstring realexe, cd;
	// create a virtual process first to get its real location.
	{
		STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
		if (!CreateProcessW(application[0] ? application : NULL, cmd.data(),
			NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
			*error = GetLastError();
			if (attributeList) DeleteProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get());
			if (pEnv) DestroyEnvironmentBlock(pEnv);
			*bSuccess = 0;
			return 0;
		}
		CloseHandle(pi.hThread);
		auto PathPtr = make_shared<WCHAR[]>(32768);DWORD size = 32768;
		if (!QueryFullProcessImageNameW(pi.hProcess, 0, PathPtr.get(), &size)) {
			*error = GetLastError();
			TerminateProcess(pi.hProcess, 0);
			CloseHandle(pi.hProcess);
			if (attributeList) DeleteProcThreadAttributeList((PPROC_THREAD_ATTRIBUTE_LIST)attributeList.get());
			if (pEnv) DestroyEnvironmentBlock(pEnv);
			*bSuccess = 0;
			return 0;
		}
		realexe = PathPtr.get();
		size_t pos = realexe.find_last_of(L'\\');
		if (pos != std::wstring::npos) {
			cd = realexe.substr(0, pos);
		}
		TerminateProcess(pi.hProcess, 0);
		CloseHandle(pi.hProcess);
	}
	if (!CreateProcessAsUserW(hToken, application[0] ? application : NULL, cmd.data(),
		NULL, NULL, FALSE, flags, pEnv, cd.empty() ? NULL : cd.c_str(), (LPSTARTUPINFOW)&si, &pi)) {
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
	int type,
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
	unsigned long long token_value,
	unsigned long logon_flags,
	const wchar_t* username,
	const wchar_t* domain,
	const wchar_t* password,
	unsigned long* custom_err_code
) {
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

	if (ServiceCoreProcess::Instance->IsProtectionDisabled()) {
		return 1;
	}

	wstring type_s;
	static const std::map<int, wstring> type_s_map{
		{0, L"CreateProcess"},
		{1, L"CreateProcessAsUser"},
		{2, L"CreateProcessWithToken"},
		{3, L"CreateProcessWithLogon"},
	};
	if (!type_s_map.contains(type)) {
		if (custom_err_code) *custom_err_code = ERROR_INVALID_PARAMETER;
		return 0;
	}
	type_s = type_s_map.at(type);

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

	DWORD dwSessionId{}, callerSession;
	bool bWasNotInteractive = false;
	if (!ProcessIdToSessionId(client_pid, &dwSessionId)) {
		*custom_err_code = GetLastError();
		return 0;
	}
	callerSession = dwSessionId;
	if (dwSessionId == 0) {
		// non-interactive session, make it interactive
		dwSessionId = WTSGetActiveConsoleSessionId();
		bWasNotInteractive = true;
	}

	// spawn a consent dialog
	wstring detailedDetails;
	if (type == 0 || type == 1) detailedDetails = std::format(
		L"Process: ({}) {} [{}] | File: {}\nTarget application: {}\nInherit handles?: {}\n"
		L"Creation flags: {}\nCurrent directory: {}\nStartupInfo Structure Size: {}{}\nCommand line:\n{}",
		client_pid, callerName, callerSession, callerPath, application, inherithandles ? L"Yes" : L"No",
		flags, cd, sisize, (type == 1) ? format(L"\nToken value: {}", token_value) : L"", cmdline
	);
	else if (type == 2) detailedDetails = std::format(
		L"Process: ({}) {} [{}] | File: {}\nTarget application: {}\nToken value: {}\nLogon flags: {}\n"
		L"Creation flags: {}\nCurrent directory: {}\nStartupInfo Structure Size: {}\nCommand line:\n{}",
		client_pid, callerName, callerSession, callerPath, application, token_value, logon_flags,
		flags, cd, sisize, cmdline
	);
	else if (type == 3) detailedDetails = std::format(
		L"Process: ({}) {} [{}] | File: {}\nTarget application: {}\n"
		L"Username: {}\nDomain: {}\nPassword: {}\nLogon flags: {}\n"
		L"Creation flags: {}\nCurrent directory: {}\nStartupInfo Structure Size: {}\nCommand line:\n{}",
		client_pid, callerName, callerSession, callerPath, application,
		username, domain, password, logon_flags,
		flags, cd, sisize, cmdline
	);
	SetLastError(ERROR_ACCESS_DENIED);
	bool acc = _XxxxInternalPopSecondaryConsentDialog(client_pid, dwSessionId, callerName, type_s,
		detailedDetails, L"Allow", L"Deny", NULL, ServiceCoreProcess::Instance->GetDefaultConsentTimeout(), true,
		bWasNotInteractive);
	if (acc) {
		return 1;
	}
	// TODO: implement remember

	// TODO: allow user to customize error code
	//*custom_err_code = ERROR_CHILD_PROCESS_BLOCKED;
	*custom_err_code = ERROR_ACCESS_DENIED;
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
		CloseHandle(hToken);
		return 0;
	}
	w32oop::util::RAIIHelper c([&] { CloseHandle(hToken); app::ResumeProcess(hCaller); });

	// check whether the caller process has permission to control the target process
	bool permok = false; BOOL isWOW{};
	if (!ImpersonateLoggedOnUser(hToken)) {
		*error = GetLastError();
		return 0;
	}
	do {
		HANDLE hProcess = OpenProcess(GENERIC_READ | GENERIC_WRITE | PROCESS_CREATE_THREAD, FALSE, dwProcessId);
		if (!hProcess) break;
		if (!IsWow64Process(hProcess, &isWOW)) {
			CloseHandle(hProcess);
			break;
		}
		HANDLE hThread = CreateRemoteThread(hProcess, NULL, NULL, (LPTHREAD_START_ROUTINE)(ULONG_PTR)1, NULL, CREATE_SUSPENDED, NULL);
		if (!hThread) {
			CloseHandle(hProcess);
			break;
		}
#pragma warning(push)
#pragma warning(disable: 6258)
		TerminateThread(hThread, 0);
#pragma warning(pop)
		CloseHandle(hThread);
		CloseHandle(hProcess);
		permok = true;
	} while (0);
	if (!RevertToSelf()) {
		*error = GetLastError();
		return 0;
	}

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




int MyProcControl_Lite::RpcClient::ScControl(unsigned long control_name, unsigned long long payload, unsigned long* result,
	PCWSTR endpoint) {
	RPC_WSTR bindingStr = nullptr;
	RPC_STATUS status = RpcStringBindingComposeW(
		nullptr,
		(RPC_WSTR)L"ncalrpc",
		nullptr,
		(RPC_WSTR)endpoint,
		nullptr,
		&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	RPC_BINDING_HANDLE hBinding = nullptr;
	status = RpcBindingFromStringBindingW(bindingStr, &hBinding);
	RpcStringFreeW(&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	int rpcRet = 0;
	RpcTryExcept{
		rpcRet = MyProcControlLite_ScControl(hBinding, control_name, payload, result);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		return GetExceptionCode();
	}
	RpcEndExcept

	RpcBindingFree(&hBinding);

	return rpcRet;
}





