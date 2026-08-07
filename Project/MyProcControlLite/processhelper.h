#pragma once
#include "targetver.h"
#include <Windows.h>
#include <string>
#include <memory>

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
	_In_ BOOL uiaccess = false
);

BOOL EnableAllPrivileges(HANDLE hToken);

bool FreeResFile(DWORD dwResName, const std::wstring& lpResType, const std::wstring& lpFilePathName, HMODULE hInst = nullptr, int maxRetries = 5, DWORD retryDelayMs = 100);

