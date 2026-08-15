#include "dll.h"
#include "hook_process.h"
#include <string>
#include "util.h"
#include "../out/generated/midl/service_h.h"
using namespace std;

BOOL(WINAPI* TrueCreateProcessW)(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	) = CreateProcessW;

BOOL(WINAPI* TrueCreateProcessA)(
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	) = CreateProcessA;

BOOL(WINAPI* TrueCreateProcessAsUserW)(
	HANDLE hToken,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	) = CreateProcessAsUserW;

BOOL(WINAPI* TrueCreateProcessAsUserA)(
	HANDLE hToken,
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	) = CreateProcessAsUserA;

BOOL(WINAPI* TrueCreateProcessWithTokenW)(
	HANDLE hToken,
	DWORD dwLogonFlags,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	) = CreateProcessWithTokenW;

BOOL(WINAPI* TrueCreateProcessWithLogonW)(
	LPCWSTR lpUsername,
	LPCWSTR lpDomain,
	LPCWSTR lpPassword,
	DWORD dwLogonFlags,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	) = CreateProcessWithLogonW;

static int myaskconsent(
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
	RPC_WSTR bindingStr = nullptr;
	RPC_STATUS status = RpcStringBindingComposeW(
		nullptr,
		(RPC_WSTR)L"ncalrpc",
		nullptr,
		(RPC_WSTR)ENDPOINT,
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
		rpcRet = MyProcControlLite_Consent_CreateProcess(hBinding, type, application, cmdline, inherithandles, flags, cd, sisize, token_value, logon_flags, username, domain, password, custom_err_code);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		SetLastError((DWORD)RPC_S_CALL_FAILED);
		return false;
	}
	RpcEndExcept

	RpcBindingFree(&hBinding);

	return rpcRet;
}

static int reqcontrol(
	unsigned long pid,
	unsigned long* errorp
) {
	RPC_WSTR bindingStr = nullptr;
	RPC_STATUS status = RpcStringBindingComposeW(
		nullptr,
		(RPC_WSTR)L"ncalrpc",
		nullptr,
		(RPC_WSTR)ENDPOINT,
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
		rpcRet = MyProcControlLite_RequestAddControl(hBinding, pid, errorp);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		SetLastError((DWORD)RPC_S_CALL_FAILED);
		return false;
	}
	RpcEndExcept

	RpcBindingFree(&hBinding);

	return rpcRet;
}


BOOL WINAPI HookedCreateProcessW(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	// 通过RPC请求用户同意
	unsigned long customerr = 0;
	wstring app = lpApplicationName ? lpApplicationName : L"(null)";
	wstring cmd = lpCommandLine ? lpCommandLine : L"(null)";
	wstring cd = lpCurrentDirectory ? lpCurrentDirectory : L"(null)";
	unsigned long sisize = lpStartupInfo ? (lpStartupInfo->cb) : 0;
	if (!myaskconsent(0, app.c_str(), cmd.c_str(), bInheritHandles, dwCreationFlags, cd.c_str(), sisize, 0, 0, L"", L"", L"", &customerr)) {
		if (customerr) SetLastError(customerr);
		else SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	// 给新的进程添加控制
	DWORD newflag = dwCreationFlags | CREATE_SUSPENDED;
	PROCESS_INFORMATION pi{};
	BOOL s = TrueCreateProcessW(
		lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		newflag,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi
	);
	if (!s) return s;

	DWORD nowerr = GetLastError();
	if (!pi.hProcess || !pi.hThread) {
		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread) CloseHandle(pi.hThread);
		SetLastError(nowerr);
		return FALSE;
	}
	if (!reqcontrol(pi.dwProcessId, &customerr)) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		SetLastError(customerr);
		return FALSE;
	}
	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		// resume process
		ResumeThread(pi.hThread);
	}
	*lpProcessInformation = pi;

	SetLastError(nowerr);
	return s;
}

BOOL WINAPI HookedCreateProcessA(
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	// 通过RPC请求用户同意
	unsigned long customerr = 0;
	wstring app = lpApplicationName ? s2ws(lpApplicationName) : L"(null)";
	wstring cmd = lpCommandLine ? s2ws(lpCommandLine) : L"(null)";
	wstring cd = lpCurrentDirectory ? s2ws(lpCurrentDirectory) : L"(null)";
	unsigned long sisize = lpStartupInfo ? (lpStartupInfo->cb) : 0;
	if (!myaskconsent(0, app.c_str(), cmd.c_str(), bInheritHandles, dwCreationFlags, cd.c_str(), sisize, 0, 0, L"", L"", L"", &customerr)) {
		if (customerr) SetLastError(customerr);
		else SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	// 给新的进程添加控制
	DWORD newflag = dwCreationFlags | CREATE_SUSPENDED;
	PROCESS_INFORMATION pi{};
	BOOL s = TrueCreateProcessA(
		lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		newflag,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi
	);
	if (!s) return s;

	DWORD nowerr = GetLastError();
	if (!pi.hProcess || !pi.hThread) {
		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread) CloseHandle(pi.hThread);
		SetLastError(nowerr);
		return FALSE;
	}
	if (!reqcontrol(pi.dwProcessId, &customerr)) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		SetLastError(customerr);
		return FALSE;
	}
	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		// resume process
		ResumeThread(pi.hThread);
	}
	*lpProcessInformation = pi;

	SetLastError(nowerr);
	return s;
}


BOOL WINAPI HookedCreateProcessAsUserW(
	HANDLE hToken,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	unsigned long customerr = 0;
	wstring app = lpApplicationName ? lpApplicationName : L"(null)";
	wstring cmd = lpCommandLine ? lpCommandLine : L"(null)";
	wstring cd = lpCurrentDirectory ? lpCurrentDirectory : L"(null)";
	unsigned long sisize = lpStartupInfo ? (lpStartupInfo->cb) : 0;

	if (!myaskconsent(1, app.c_str(), cmd.c_str(), bInheritHandles, dwCreationFlags,
		cd.c_str(), sisize, (unsigned long long)hToken, 0,
		L"", L"", L"", &customerr)) {
		if (customerr) SetLastError(customerr);
		else SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	DWORD newflag = dwCreationFlags | CREATE_SUSPENDED;
	PROCESS_INFORMATION pi{};
	BOOL s = TrueCreateProcessAsUserW(
		hToken,
		lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		newflag,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi
	);
	if (!s) return s;

	DWORD nowerr = GetLastError();
	if (!pi.hProcess || !pi.hThread) {
		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread) CloseHandle(pi.hThread);
		SetLastError(nowerr);
		return FALSE;
	}
	if (!reqcontrol(pi.dwProcessId, &customerr)) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		SetLastError(customerr);
		return FALSE;
	}
	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(pi.hThread);
	}
	*lpProcessInformation = pi;

	SetLastError(nowerr);
	return s;
}

BOOL WINAPI HookedCreateProcessAsUserA(
	HANDLE hToken,
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	unsigned long customerr = 0;
	wstring app = lpApplicationName ? s2ws(lpApplicationName) : L"(null)";
	wstring cmd = lpCommandLine ? s2ws(lpCommandLine) : L"(null)";
	wstring cd = lpCurrentDirectory ? s2ws(lpCurrentDirectory) : L"(null)";
	unsigned long sisize = lpStartupInfo ? (lpStartupInfo->cb) : 0;

	if (!myaskconsent(1, app.c_str(), cmd.c_str(), bInheritHandles, dwCreationFlags,
		cd.c_str(), sisize, (unsigned long long)hToken, 0,
		L"", L"", L"", &customerr)) {
		if (customerr) SetLastError(customerr);
		else SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	DWORD newflag = dwCreationFlags | CREATE_SUSPENDED;
	PROCESS_INFORMATION pi{};
	BOOL s = TrueCreateProcessAsUserA(
		hToken,
		lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		newflag,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi
	);
	if (!s) return s;

	DWORD nowerr = GetLastError();
	if (!pi.hProcess || !pi.hThread) {
		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread) CloseHandle(pi.hThread);
		SetLastError(nowerr);
		return FALSE;
	}
	if (!reqcontrol(pi.dwProcessId, &customerr)) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		SetLastError(customerr);
		return FALSE;
	}
	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(pi.hThread);
	}
	*lpProcessInformation = pi;

	SetLastError(nowerr);
	return s;
}

BOOL WINAPI HookedCreateProcessWithTokenW(
	HANDLE hToken,
	DWORD dwLogonFlags,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	unsigned long customerr = 0;
	wstring app = lpApplicationName ? lpApplicationName : L"(null)";
	wstring cmd = lpCommandLine ? lpCommandLine : L"(null)";
	wstring cd = lpCurrentDirectory ? lpCurrentDirectory : L"(null)";
	unsigned long sisize = lpStartupInfo ? (lpStartupInfo->cb) : 0;

	if (!myaskconsent(2, app.c_str(), cmd.c_str(), 0, dwCreationFlags,
		cd.c_str(), sisize, (unsigned long long)hToken, dwLogonFlags,
		L"", L"", L"", &customerr)) {
		if (customerr) SetLastError(customerr);
		else SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	DWORD newflag = dwCreationFlags | CREATE_SUSPENDED;
	PROCESS_INFORMATION pi{};
	BOOL s = TrueCreateProcessWithTokenW(
		hToken,
		dwLogonFlags,
		lpApplicationName,
		lpCommandLine,
		newflag,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi
	);
	if (!s) return s;

	DWORD nowerr = GetLastError();
	if (!pi.hProcess || !pi.hThread) {
		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread) CloseHandle(pi.hThread);
		SetLastError(nowerr);
		return FALSE;
	}
	if (!reqcontrol(pi.dwProcessId, &customerr)) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		SetLastError(customerr);
		return FALSE;
	}
	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(pi.hThread);
	}
	*lpProcessInformation = pi;

	SetLastError(nowerr);
	return s;
}

BOOL WINAPI HookedCreateProcessWithLogonW(
	LPCWSTR lpUsername,
	LPCWSTR lpDomain,
	LPCWSTR lpPassword,
	DWORD dwLogonFlags,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
) {
	unsigned long customerr = 0;
	wstring app = lpApplicationName ? lpApplicationName : L"(null)";
	wstring cmd = lpCommandLine ? lpCommandLine : L"(null)";
	wstring cd = lpCurrentDirectory ? lpCurrentDirectory : L"(null)";
	unsigned long sisize = lpStartupInfo ? (lpStartupInfo->cb) : 0;
	wstring user = lpUsername ? lpUsername : L"";
	wstring domain = lpDomain ? lpDomain : L"";
	wstring pass = lpPassword ? lpPassword : L"";

	if (!myaskconsent(3, app.c_str(), cmd.c_str(), 0, dwCreationFlags,
		cd.c_str(), sisize, 0, dwLogonFlags,
		user.c_str(), domain.c_str(), pass.c_str(), &customerr)) {
		if (customerr) SetLastError(customerr);
		else SetLastError(ERROR_ACCESS_DENIED);
		return FALSE;
	}

	DWORD newflag = dwCreationFlags | CREATE_SUSPENDED;
	PROCESS_INFORMATION pi{};
	BOOL s = TrueCreateProcessWithLogonW(
		lpUsername,
		lpDomain,
		lpPassword,
		dwLogonFlags,
		lpApplicationName,
		lpCommandLine,
		newflag,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi
	);
	if (!s) return s;

	DWORD nowerr = GetLastError();
	if (!pi.hProcess || !pi.hThread) {
		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread) CloseHandle(pi.hThread);
		SetLastError(nowerr);
		return FALSE;
	}
	if (!reqcontrol(pi.dwProcessId, &customerr)) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		SetLastError(customerr);
		return FALSE;
	}
	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(pi.hThread);
	}
	*lpProcessInformation = pi;

	SetLastError(nowerr);
	return s;
}


