#include "targetver.h"
#include <shlobj.h>
#include <VersionHelpers.h>
#include "../lib/CLI11/CLI11.hpp"
#include <w32use.hpp>
#include "srv.hpp"
#include "session_worker.hpp"
#include "ui_consent.hpp"
#include "TrayIcon.hpp"
using namespace std;

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

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
	string u8type, u8name, u8action, u8ppid;
	app.add_option("--type", u8type);
	app.add_option("--name", u8name);
	app.add_option("--action", u8action);
	app.add_option("--ppid", u8ppid);
	try { app.parse(utf16_utf8(GetCommandLineW()), true); } catch (...) {}
	auto argc = app.count_all();
	wstring type = utf8_utf16(u8type), name = utf8_utf16(u8name), action = utf8_utf16(u8action), ppid = utf8_utf16(u8ppid);

	if (type == L"service") {
		if (name.empty()) return ERROR_INVALID_PARAMETER;
		WindowsService service(name);
		service.Run();
		return 0;
	}

	if (type == L"session-worker") {
		return SessionWorker(name, std::stoul(ppid));
	}

	if (type == L"tray-icon") {
		HANDLE ppidn = (HANDLE)(ULONG_PTR)stoull(ppid);
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
			UIService::TrayIconWindow win;
		win.SetRpcEndpoint(name);
		win.create();
		win.set_main_window();
		return win.run();
	}

	if (type == L"consent-test") {
		Window::set_global_option(Window::Option_DebugMode, true);
		ConsentDialog cdlg(L"MyApp-VeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVerylongname.exe", L"CreateProcess",
			L"Process Name: cmd.exe\r\nProcess : C:\\Windows\\System32\\cmd.exe\r\n"
			L"Process Name: cmd.exe\r\nProcess : C:\\Windows\\System32\\cmd.exe\r\n"
			L"User: Current User\r\nAdditional info:\r\nNone",
			L"Allow", L"Block", true);
		cdlg.create();
		cdlg.show();
		cdlg.run(&cdlg);
		MessageBoxTimeoutW(NULL, (cdlg.result() == 1) ? L"Allowed" : L"Blocked", L"Consent Test",
			(cdlg.remember() ? MB_ICONWARNING : MB_ICONINFORMATION) | MB_OK | MB_TOPMOST, 0, 2000);
		return 0;
	}

	if (type == L"setup" || (type == L"" && argc < 2)) {
		INITCOMMONCONTROLSEX icce{};
		icce.dwSize = sizeof(icce);
		icce.dwICC = ICC_ALL_CLASSES;
		InitCommonControlsEx(&icce);
		if (name.empty()) name = L"MyProcControl-Lite";
		if (action.empty()) {
			int btn = 0;
			TaskDialog(NULL, hInstance, L"MyProcControl (Lite) Setup", L"What would you want to do?", 
				L"Press [Yes] to Install.\n"
				L"Press [No] to Uninstall.\n"
				L"Press [Cancel] to cancel.\n",
				TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON,
				TD_INFORMATION_ICON,
				&btn
			);
			if (btn == IDYES) action = L"install";
			else if (btn == IDNO) action = L"uninstall";
			else return ERROR_CANCELLED;
			if (!IsUserAnAdmin()) {
				setup_askprivilege:
				TaskDialog(NULL, hInstance, L"MyProcControl (Lite) Setup",
					L"Administrators privilege is required to install or modify the product.",
					L"Press Yes to continue.",
					TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON,
					TD_SHIELD_ICON,
					&btn
				);
				if (btn != IDYES) return ERROR_CANCELLED;

				SHELLEXECUTEINFOW sei{};
				sei.cbSize = sizeof(sei);
				sei.fMask = SEE_MASK_NOCLOSEPROCESS;
				sei.lpVerb = L"runas";
				sei.nShow = SW_SHOW;
				auto program = make_unique<WCHAR[]>(32768);
				GetModuleFileNameW(NULL, program.get(), 32768);
				sei.lpFile = program.get();
				wstring params = L"--type=setup --action=" + action;
				sei.lpParameters = params.c_str();
				if (!ShellExecuteExW(&sei)) goto setup_askprivilege;
				if (!sei.hProcess) return GetLastError();
				WaitForSingleObject(sei.hProcess, INFINITE);
				DWORD exitCode{};
				GetExitCodeProcess(sei.hProcess, &exitCode);
				CloseHandle(sei.hProcess);

				return exitCode;
			}
		} // if (action.empty())
		try {
			ServiceManager scm;
			if (action == L"install") {
				try {
					if (scm.get(name)) {
						SetLastError(ERROR_SERVICE_EXISTS);
						throw w32oop::exceptions::system_exception("Service already exists.");
					}
				}
				catch (w32oop::exceptions::system_exception& exc) {
					if (!dynamic_cast<invalid_scm_handle_exception*>(&exc)) throw;
				}
				auto program = make_unique<WCHAR[]>(32768);
				GetModuleFileNameW(NULL, program.get(), 32768);
				wstring cmdLine = L"\""s + program.get() + 
					L"\" --type=service --name=\"" + name + L"\"";
				Service myService = scm.create(name, cmdLine, SERVICE_AUTO_START,
					L"Process Command and Control Server (" + name + L")",
					L"Process Command and Control Server", SERVICE_WIN32_OWN_PROCESS);
				if (!myService.start()) throw w32oop::exceptions::system_exception("Failed to start service.");

				TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
					L"The product has been successfully installed to your computer.",
					L"Click OK to dismiss.", TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, NULL);
				return 0;
			}
			if (action == L"uninstall") {
				Service myService = scm.get(name);
				if (!myService.remove()) throw w32oop::exceptions::system_exception("Failed to remove service.");
				if (myService.status() == SERVICE_RUNNING) {
					if (!myService.pause_service()) throw w32oop::exceptions::system_exception("Failed to pause service.");
					Sleep(500);
				}
				if (myService.status() == SERVICE_PAUSED) {
					if (!myService.stop()) throw w32oop::exceptions::system_exception("Failed to stop service.");
				}

				TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
					L"The product has been successfully removed from your computer.",
					L"Click OK to dismiss.",
					TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, NULL);
				return 0;
			}
			MessageBoxW(NULL, L"Unknown action type", L"Error", MB_ICONERROR);
			return 87;
		}
		catch (w32oop::exceptions::system_exception& e) {
			TaskDialog(NULL, nullptr, L"MyProcControl (Lite) Setup", 
				L"The operation failed for an operation-specific reason",
				(w32oop::util::str::converts::str_wstr(e.what()) + L"\n\n" +
					ErrorChecker().message()).c_str(), TDCBF_CANCEL_BUTTON, TD_ERROR_ICON, 0);
			return GetLastError();
		}
	}

	return ERROR_INVALID_PARAMETER;
}