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

static int myaskconsent(
	const wchar_t* application,
	const wchar_t* cmdline,
	int inherithandles,
	unsigned long flags,
	const wchar_t* cd,
	unsigned long sisize,
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
	if (status != RPC_S_OK) return (int)status;

	RPC_BINDING_HANDLE hBinding = nullptr;
	status = RpcBindingFromStringBindingW(bindingStr, &hBinding);
	RpcStringFreeW(&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	int bSuccess = 0;
	unsigned long error = 0;
	int rpcRet = 0;
	RpcTryExcept{
		rpcRet = MyProcControlLite_Consent_CreateProcess(hBinding, application, cmdline, inherithandles, flags, cd, sisize, custom_err_code);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		return RPC_S_CALL_FAILED;
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
	if (status != RPC_S_OK) return (int)status;

	RPC_BINDING_HANDLE hBinding = nullptr;
	status = RpcBindingFromStringBindingW(bindingStr, &hBinding);
	RpcStringFreeW(&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	int bSuccess = 0;
	unsigned long error = 0;
	int rpcRet = 0;
	RpcTryExcept{
		rpcRet = MyProcControlLite_RequestAddControl(hBinding, pid, errorp);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		return RPC_S_CALL_FAILED;
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
	if (!myaskconsent(app.c_str(), cmd.c_str(), bInheritHandles, dwCreationFlags, cd.c_str(), sisize, &customerr)) {
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
	if (!myaskconsent(app.c_str(), cmd.c_str(), bInheritHandles, dwCreationFlags, cd.c_str(), sisize, &customerr)) {
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


