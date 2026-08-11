#include "processhelper.h"
#include <userenv.h>
#pragma comment(lib, "Userenv.lib")
using namespace std;

FARPROC app::GetProcAddress(HMODULE hModule, PCSTR name) {
	static HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
	if (!k32) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
	static auto GetProcAddress = reinterpret_cast<decltype(&::GetProcAddress)>(::GetProcAddress(k32, "GetProcAddress"));
	if (!GetProcAddress) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
	return GetProcAddress(hModule, name);
}

#pragma warning(push)
#pragma warning(disable: 6101)
BOOL CreateProcessInSession(_In_ DWORD dwSessionId,
	_In_opt_ LPCTSTR lpApplicationName,
	_Inout_opt_ LPTSTR lpCommandLine,
	_In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
	_In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
	_In_ BOOL bInheritHandles,
	_In_ DWORD dwCreationFlags,
	_In_opt_ LPVOID lpEnvironment,
	_In_opt_ LPCTSTR lpCurrentDirectory,
	_In_ LPSTARTUPINFO lpStartupInfo,
	_Out_ LPPROCESS_INFORMATION lpProcessInformation,
	_In_ BOOL uiaccess
) {
	auto& si = *lpStartupInfo;         //
	//auto& pi = *lpProcessInformation;  //
	//HANDLE hUserToken = NULL;          // 当前登录用户的令牌
	HANDLE hUserTokenDup = NULL;       // 复制的用户令牌
	HANDLE hPToken = NULL;             // 进程令牌

	//// 不需要获取用户token,子进程以父进程权限运行
	////WTSQueryUserToken(dwSessionId, &hUserToken);
	dwCreationFlags |= NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE;

	WCHAR lpDesktop[] = L"winsta0\\default";
	si.lpDesktop = lpDesktop;
	//指定创建进程的窗口站，Windows下唯一可交互的窗口站就是WinSta0\Default

	//打开进程令牌
	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES |
		TOKEN_QUERY | TOKEN_DUPLICATE |
		TOKEN_ASSIGN_PRIMARY |
		TOKEN_ADJUST_SESSIONID |
		TOKEN_READ | TOKEN_WRITE, &hPToken)) {
		return FALSE;
	}

	//复制当前用户的令牌
	if (!DuplicateTokenEx(hPToken, MAXIMUM_ALLOWED, NULL,
		SecurityIdentification, TokenPrimary, &hUserTokenDup)) {
		CloseHandle(hPToken);
		return FALSE;
	}

	//设置当前进程的令牌信息
	if (!SetTokenInformation(hUserTokenDup, TokenSessionId,
		(void*)&dwSessionId, sizeof(DWORD))) {
		CloseHandle(hUserTokenDup);
		CloseHandle(hPToken);
		return FALSE;
	}
	EnableAllPrivileges(hUserTokenDup);

	if (uiaccess) {
		BOOL bUIAccess = TRUE;
		if (!SetTokenInformation(hUserTokenDup, TokenUIAccess, &bUIAccess, sizeof(bUIAccess))) {
			CloseHandle(hUserTokenDup);
			CloseHandle(hPToken);
			return FALSE;
		}
	}

	//创建进程环境块，保证环境块是在用户桌面的环境下
	LPVOID pEnv = NULL;
	if (CreateEnvironmentBlock(&pEnv, hUserTokenDup, TRUE)) {
		dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;
	}

	//创建用户进程
	if (!CreateProcessAsUser(hUserTokenDup, lpApplicationName, lpCommandLine,
		lpProcessAttributes, lpThreadAttributes, bInheritHandles,
		dwCreationFlags, lpEnvironment ? lpEnvironment : pEnv,
		lpCurrentDirectory, lpStartupInfo, lpProcessInformation))
	{
		CloseHandle(hUserTokenDup);
		CloseHandle(hPToken);
		return FALSE;
	}

	//关闭句柄
	//CloseHandle(pi.hProcess);
	//CloseHandle(pi.hThread);
	if (pEnv) DestroyEnvironmentBlock(pEnv);
	//if (hUserToken) CloseHandle(hUserToken);
	if (hUserTokenDup) CloseHandle(hUserTokenDup);
	if (hPToken) CloseHandle(hPToken);

	return TRUE;
}
#pragma warning(pop)

BOOL EnableAllPrivileges(HANDLE hToken) {
	BOOL bResult = FALSE;
	HANDLE hTokenLocal = nullptr;
	DWORD dwTokenInfoSize = 0;
	PTOKEN_PRIVILEGES pTokenPrivileges = nullptr;

	// 处理令牌句柄
	if (hToken == nullptr) {
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hTokenLocal)) {
			return FALSE;
		}
		hToken = hTokenLocal;
	}

	// 获取所需缓冲区大小
	if (!GetTokenInformation(hToken, TokenPrivileges, nullptr, 0, &dwTokenInfoSize) &&
		GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		goto cleanup;
	}

	// 分配权限信息缓冲区
	pTokenPrivileges = reinterpret_cast<PTOKEN_PRIVILEGES>(malloc(dwTokenInfoSize));
	if (!pTokenPrivileges) {
		goto cleanup;
	}

	// 获取实际权限信息
	if (!GetTokenInformation(hToken, TokenPrivileges, pTokenPrivileges, dwTokenInfoSize, &dwTokenInfoSize)) {
		goto cleanup;
	}

	// 启用所有权限
	bResult = TRUE;
	for (DWORD i = 0; i < pTokenPrivileges->PrivilegeCount; ++i) {
		LUID_AND_ATTRIBUTES& la = pTokenPrivileges->Privileges[i];
		TOKEN_PRIVILEGES tp = { 1, { { la.Luid, SE_PRIVILEGE_ENABLED } } };

		if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
			bResult = FALSE;
		}
		else if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
			bResult = FALSE;
		}
	}

cleanup:
	if (pTokenPrivileges) free(pTokenPrivileges);
	if (hTokenLocal) CloseHandle(hTokenLocal);
	return bResult;
}

bool FreeResFile(DWORD dwResName, const std::wstring& lpResType, const std::wstring& lpFilePathName, HMODULE hInst, int maxRetries, DWORD retryDelayMs) {
	// 获取模块句柄
	HMODULE hInstance = hInst ? hInst : GetModuleHandleW(nullptr);

	// 查找资源
	HRSRC hResInfo = FindResourceW(hInstance, MAKEINTRESOURCEW(dwResName), lpResType.c_str());
	if (!hResInfo) return false;

	// 加载并锁定资源
	HGLOBAL hResData = LoadResource(hInstance, hResInfo);
	if (!hResData) return false;

	const void* pRes = LockResource(hResData);
	if (!pRes) return false;

	DWORD dwSize = SizeofResource(hInstance, hResInfo);
	if (dwSize == 0) return false;

	// 先尝试打开磁盘上的现有文件，比较内容是否一致
	// 如果完全一致则跳过写出，节省磁盘写入寿命
	HANDLE hExistingFile = CreateFileW(
		lpFilePathName.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);
	if (hExistingFile != INVALID_HANDLE_VALUE) {
		bool bIdentical = false;
		DWORD dwFileSize = GetFileSize(hExistingFile, nullptr);
		if (dwFileSize == dwSize) {
			// 大小相同，进一步比较内容
			std::unique_ptr<BYTE[]> pFileData(new (std::nothrow) BYTE[dwSize]);
			if (pFileData) {
				DWORD dwRead = 0;
				if (ReadFile(hExistingFile, pFileData.get(), dwSize, &dwRead, nullptr) && dwRead == dwSize) {
					if (memcmp(pRes, pFileData.get(), dwSize) == 0) {
						bIdentical = true;
					}
				}
			}
		}
		CloseHandle(hExistingFile);
		if (bIdentical) {
			return true; // 文件内容完全一致，无需写出
		}
	}

	// 文件不存在或内容不同，需要写出到磁盘

	for (int retry = 0; retry < maxRetries; ++retry) {
		HANDLE hFile = CreateFileW(
			lpFilePathName.c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr
		);
		if (hFile != INVALID_HANDLE_VALUE) {
			DWORD dwWritten = 0, dwErr = 0;
			BOOL bWriteOk = WriteFile(hFile, pRes, dwSize, &dwWritten, nullptr);
			if (!bWriteOk) dwErr = GetLastError();
			CloseHandle(hFile);
			if (bWriteOk && dwSize == dwWritten) {
				return true;
			}
			if (dwErr == ERROR_SHARING_VIOLATION) {
				if (retry < maxRetries - 1) {
					Sleep(retryDelayMs);
					continue;
				}
			}
			return false;
		}
		DWORD dwErr = GetLastError();
		if (dwErr != ERROR_SHARING_VIOLATION) return false;
		if (retry < maxRetries - 1) Sleep(retryDelayMs);
	}
	return false;
}

static NTSTATUS _InternalSuspendProcess(HANDLE ProcessHandle, PCSTR func) {
	static HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (!ntdll) return STATUS_DLL_NOT_FOUND;
	typedef NTSTATUS(NTAPI*F)(HANDLE ProcessHandle);
	F f = (F)app::GetProcAddress(ntdll, func);
	if (!f) return STATUS_DLL_INIT_FAILED;
	return f(ProcessHandle);
}

NTSTATUS app::SuspendProcess(_In_ HANDLE ProcessHandle) {
	return _InternalSuspendProcess(ProcessHandle, "NtSuspendProcess");
}

NTSTATUS app::ResumeProcess(_In_ HANDLE ProcessHandle) {
	return _InternalSuspendProcess(ProcessHandle, "NtResumeProcess");
}

