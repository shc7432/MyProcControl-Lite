#include <Windows.h>
#include <detours.h>
#include <stdio.h>
#include <string>
using namespace std;

// 原始函数指针
static BOOL(WINAPI* TrueCreateProcessW)(
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

// 钩子函数
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
    wstring str;
    str = L"Attempting to create process: " + wstring(lpApplicationName ? lpApplicationName : L"(null)") + L"\n" +
        L"Command Line: " + wstring(lpCommandLine ? lpCommandLine : L"(null)") + L"\n" +
        L"Current Directory: " + wstring(lpCurrentDirectory ? lpCurrentDirectory : L"(null)") + L"\n";
    if (MessageBoxW(GetForegroundWindow(), str.c_str(), L"Attempting to CreateProcess", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING | MB_TOPMOST) == IDNO) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    // 3. 调用原始函数
    return TrueCreateProcessW(
        lpApplicationName,
        lpCommandLine,
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        dwCreationFlags,
        lpEnvironment,
        lpCurrentDirectory,
        lpStartupInfo,
        lpProcessInformation
    );
}

// DLL 入口点
extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved) {
    if (DetourIsHelperProcess())
        return TRUE;

    switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
#pragma region 加入 Hooks 代码
        DetourAttach(&(PVOID&)TrueCreateProcessW, HookedCreateProcessW);

        // 更多功能待实现...
#pragma endregion
        DetourTransactionCommit();
    }
                           break;

    case DLL_PROCESS_DETACH: {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
#pragma region 移除 Hooks 代码
        DetourDetach(&(PVOID&)TrueCreateProcessW, HookedCreateProcessW);

#pragma endregion
        DetourTransactionCommit();
    }
                           break;
    }
    return TRUE;
}