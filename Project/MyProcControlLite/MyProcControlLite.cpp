#include <windows.h>
#include "../../../MyLearn/resource/tool.h"
#include "ui_consent.hpp"
#include "srv.hpp"
#include <w32use.hpp>
using namespace std;

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	CmdLineW cl(GetCommandLineW());
	wstring type; cl.getopt(L"type", type);

	if (type == L"service") {
		wstring name; cl.getopt(L"name", name);
		if (name.empty()) return ERROR_INVALID_PARAMETER;
		WindowsService service(name);
		service.Run();
		return 0;
	}

	if (type == L"ui-service") {
		
	}

	if (type == L"consent-test") {
		Window::set_global_option(Window::Option_DebugMode, true);
		ConsentDialog cdlg(L"MyApp.exe", L"CreateProcess",
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

	if (type == L"setup" || (type == L"" && cl.argc() < 2)) {
		INITCOMMONCONTROLSEX icce{};
		icce.dwSize = sizeof(icce);
		icce.dwICC = ICC_ALL_CLASSES;
		InitCommonControlsEx(&icce);
		wstring action; cl.getopt(L"action", action);
		wstring service_name; cl.getopt(L"name", service_name);
		if (service_name.empty()) service_name = L"MyProcControl-Lite";
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
			if (!IsRunAsAdmin()) {
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
				wstring programDir = GetProgramDirW();  
				sei.lpFile = programDir.c_str();
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
					if (scm.get(service_name)) {
						SetLastError(ERROR_SERVICE_EXISTS);
						throw w32oop::exceptions::system_exception("Service already exists.");
					}
				}
                catch (w32oop::exceptions::system_exception& exc) {
					if (!dynamic_cast<invalid_scm_handle_exception*>(&exc)) throw;
				}
				wstring cmdLine = L"\"" + GetProgramDirW() + 
					L"\" --type=service --name=\"" + service_name + L"\"";
				Service myService = scm.create(service_name, cmdLine, SERVICE_AUTO_START,
					L"Process Command and Control Server (" + service_name + L")",
					L"Process Command and Control Server", SERVICE_WIN32_OWN_PROCESS);
				if (!myService.start()) throw w32oop::exceptions::system_exception("Failed to start service.");

				TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
					L"The product has been successfully installed to your computer.",
					L"Click OK to dismiss.", TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, NULL);
				return 0;
			}
			if (action == L"uninstall") {
				Service myService = scm.get(service_name);
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
					LastErrorStrW()).c_str(), TDCBF_CANCEL_BUTTON, TD_ERROR_ICON, 0);
			return GetLastError();
		}
	}

	return ERROR_INVALID_PARAMETER;
}