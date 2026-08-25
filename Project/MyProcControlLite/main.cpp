#include "targetver.h"
#include <shlobj.h>
#include <VersionHelpers.h>
#include "../lib/CLI11/CLI11.hpp"
#include <w32use.hpp>
#include "srv.hpp"
#include "srvapi.hpp"
#include "processhelper.h"
#include "session_worker.hpp"
#include "ui_consent.hpp"
#include "setupui.h"
#include "commandline.h"
#include "TrayIconWin.hpp"
#include "../lib/ui/BackgroundLayeredAlphaWindowClass.h"
using namespace std;

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

#ifndef _WIN64
#ifdef _WIN32
#error X86 Not supported!!
#endif // _WIN32
#endif // !_WIN64


HINSTANCE hInst;

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	using namespace w32oop::util::str::encodings;
	using namespace MyProcControl_Lite;
	::hInst = hInstance;
	PROCESS_MITIGATION_IMAGE_LOAD_POLICY il2{};
	il2.PreferSystem32Images = true;
	{
		HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
		if (!k32) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
		auto GetProcAddress = reinterpret_cast<decltype(&::GetProcAddress)>(::GetProcAddress(k32, "GetProcAddress"));
		if (!GetProcAddress) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
		using t = BOOL(WINAPI*)(PROCESS_MITIGATION_POLICY, PVOID, SIZE_T);
		auto p = (t)GetProcAddress(k32, "SetProcessMitigationPolicy");
		if (p) p(ProcessImageLoadPolicy, &il2, sizeof(il2));
	}
	if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) __fastfail(FAST_FAIL_FATAL_APP_EXIT);
	w32oop::util::RAIIHelper comUninit([] { CoUninitialize(); });
	CLI::App app;
	app.allow_extras();
	string u8type, u8name, u8action, u8ppid, u8sig;
	bool interactive = false;
	array<string, 16> u8extras;
	app.add_option("--type", u8type);
	app.add_option("--name", u8name);
	app.add_option("--action", u8action);
	app.add_option("--ppid", u8ppid);
	app.add_option("--signature", u8sig);
	app.add_flag("--interactive", interactive);
	for (int i = 0; i < 16; ++i) app.add_option("--extra" + to_string(i + 1), u8extras[i]);
	try { app.parse(utf16_utf8(GetCommandLineW()), true); } catch (...) {}
	auto argc = app.count_all();
	wstring type = utf8_utf16(u8type), name = utf8_utf16(u8name), action = utf8_utf16(u8action), ppid = utf8_utf16(u8ppid);
	wstring signature = utf8_utf16(u8sig);
	DWORD Ppid{};
	try { Ppid = std::stoul(ppid); }
	catch (...) { Ppid = (DWORD)-1; }

	if (type == L"service") {
		if (name.empty()) return ERROR_INVALID_PARAMETER;
		WindowsService service(name);
		service.Run();
		return 0;
	}

	if (type == L"service-core-worker") {
		return ServiceCoreProcess::ServiceWorkerProcess(name, Ppid, u8extras[0]);
	}

	if (type == L"session-worker") {
		return SessionWorker(name, Ppid);
	}

	if (type == L"tray-icon") {
		HANDLE ppidn{};
		try { ppidn = (HANDLE)(ULONG_PTR)stoull(ppid); } catch (...) {}
		if (ppidn) {
			HANDLE hWaiter = CreateThread(NULL, 0, [](PVOID p)->DWORD {
				HANDLE hProcess = (HANDLE)(ULONG_PTR)p;
				if (!hProcess) return GetLastError();
				if (WAIT_OBJECT_0 != WaitForSingleObject(hProcess, INFINITE)) return GetLastError();
				CloseHandle(hProcess);
				ExitProcess(0);
				return 0;
			}, (PVOID)(ULONG_PTR)ppidn, 0, 0);
			if (hWaiter) CloseHandle(hWaiter);
			else return GetLastError();
		}
		UIService::TrayIconWindow win(name);
		win.create();
		win.set_main_window();
		return win.run();
	}

#if 0
	if (type == L"consent-test") {
		Window::set_global_option(Window::Option_DebugMode, 1);
		RegClass_BackgroundLayeredAlphaWindowClass();
		MyProcControl_Lite::SecondaryConsentDialog cdlg(L"u8e0", L"u8e1", L"text",
			L"u8e2", L"u8e3", u8extras[4] == "y", u8extras[9] == "y", 30);
		cdlg.create();
		cdlg.show();
		cdlg.run(&cdlg);
		int result = cdlg.result();
		return int(result | (cdlg.remember() ? 0x20000000 : 0));
	}
#endif

	if (type == L"consent") {
		if (!util_IsCurrentProcessSYSTEM()) return ERROR_SIGNAL_REFUSED;
		return RunConsentUI(name, action, signature, u8extras);
	}

	if (type == L"command-line-interface") {
		return RunCommandLineInterface(name, action, u8extras);
	}

	if (type == L"config") {
		return RunConfigUI(name);
	}

	if (type == L"setup" || (type == L"" && argc < 2)) {
		return RunSetupUI(name, action, interactive);
	}

	return ERROR_INVALID_PARAMETER;
}