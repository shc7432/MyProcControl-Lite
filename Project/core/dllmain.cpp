#include "dll.h"
#include <detours.h>
#include <memory>
#include "hook_process.h"
using namespace std;

HMODULE hModule;
PCWSTR ENDPOINT;
wstring szDll;
wstring svcName, svcEndpoint;


__declspec(noreturn) void crash() {
	CONTEXT ctx{}; EXCEPTION_RECORD rec{};
	rec.ExceptionCode = GetLastError();
	rec.ExceptionAddress = _ReturnAddress();
	RtlCaptureContext(&ctx);
	RaiseFailFastException(&rec, &ctx, 0);
	__fastfail(2);
}


static std::wstring utf8_utf16(PCSTR utf8Str) {
	if (utf8Str == nullptr) return L"";
	int utf8Len = static_cast<int>(strlen(utf8Str));
	int utf16Len = MultiByteToWideChar(CP_UTF8, 0, utf8Str,
		utf8Len, nullptr, 0);
	std::wstring utf16Str(utf16Len, 0);  // 创建一个足够大的wstring来容纳UTF-16字符串
	MultiByteToWideChar(CP_UTF8, 0, utf8Str, utf8Len,
		&utf16Str[0], utf16Len);
	return utf16Str;
}


BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	if (DetourIsHelperProcess())
		return TRUE;

	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH: {
		::hModule = hModule;
		auto app = make_unique<WCHAR[]>(32768);
		GetModuleFileNameW(hModule, app.get(), 32768);
		szDll = app.get();
		{
			HANDLE hFile = CreateFileW((szDll +
#if not defined(_WIN64) && defined(_WIN32)
				L"/.."
#endif
				L"/../SERVICE").c_str(), FILE_READ_DATA | SYNCHRONIZE,
				FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (!hFile || hFile == INVALID_HANDLE_VALUE) crash();
			CHAR buffer[2049]{}; DWORD byte{};
			if (!ReadFile(hFile, buffer, 2048, &byte, NULL)) crash();
			CloseHandle(hFile);
			svcName = utf8_utf16(buffer);
			svcEndpoint = L"MyProcControlLiteRpc_" + svcName;
			ENDPOINT = svcEndpoint.c_str();
		}

		DetourRestoreAfterWith();
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
#pragma region 加入 Hooks 代码
		DetourAttach(&(PVOID&)TrueCreateProcessW, HookedCreateProcessW);
		DetourAttach(&(PVOID&)TrueCreateProcessA, HookedCreateProcessA);

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
		DetourDetach(&(PVOID&)TrueCreateProcessA, HookedCreateProcessA);

#pragma endregion
		DetourTransactionCommit();

		if (lpReserved == nullptr) {
			SetLastError(ERROR_BLOCKED_BY_PARENTAL_CONTROLS);
			crash();
		}
	}
		break;
	}
	return TRUE;
}


