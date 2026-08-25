#include "setupui.h"
#include <w32use.hpp>
#include <shlobj.h>
#include "configtool.hpp"
#include "resource.h"
using namespace std;

extern HINSTANCE hInst;


int RunConfigUI(std::wstring name) {
	INITCOMMONCONTROLSEX icce{};
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_ALL_CLASSES;
	InitCommonControlsEx(&icce);
	bool withName = !name.empty();
	if (!withName) name = L"MyProcControl-Lite";

	MyProcControl_Lite::ConfigTool win(name, withName);
	win.create();
	win.set_main_window();
	win.center();
	win.show();
	return win.run();
}

int RunSetupUI(std::wstring name, std::wstring action, bool interactive) {
	if (action.empty()) return RunConfigUI(name);
	INITCOMMONCONTROLSEX icce{};
	icce.dwSize = sizeof(icce);
	icce.dwICC = ICC_ALL_CLASSES;
	InitCommonControlsEx(&icce);
	if (name.empty()) name = L"MyProcControl-Lite";
	int btn = 0;
	if (action.empty()) {
		if (!interactive) return ERROR_AMBIGUOUS_SYSTEM_DEVICE;
		TaskDialog(NULL, hInst, L"MyProcControl (Lite) Setup", L"What would you want to do?",
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
			TaskDialog(NULL, hInst, L"MyProcControl (Lite) Setup",
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
			wstring params = L"--type=setup --action=" + action + L" --name=\"" + name + L"\"";
			if (interactive) params += L"  --interactive";
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
	if (!IsUserAnAdmin() && interactive) goto setup_askprivilege;
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
			w32ServiceHandle scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
			auto program = make_unique<WCHAR[]>(32768);
			GetModuleFileNameW(NULL, program.get(), 32768);
			wstring cmdLine = L"\""s + program.get() +
				L"\" --type=service --name=\"" + name + L"\"";
			Service myService = w32ServiceHandle(CreateServiceW(scm, name.c_str(),
				(L"Process Control Server (" + name + L")").c_str(),
				SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
				cmdLine.c_str(), NULL, NULL,
				L"RpcSs\0DcomLaunch\0LSM\0CryptSvc\0SamSs\0ProfSvc\0UserManager\0EventSystem\0SENS\0",
				NULL, NULL));
			SERVICE_DESCRIPTIONW a{};
			WCHAR desc[] = L"Process Control Server";
			a.lpDescription = desc;
			ChangeServiceConfig2W(myService, SERVICE_CONFIG_DESCRIPTION, &a);
			if (!myService.start()) throw w32oop::exceptions::system_exception("Failed to start service.");

			if (interactive) TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
				L"The product has been successfully installed to your computer.",
				L"Click OK to dismiss.", TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, NULL);
			return 0;
		}
		if (action == L"uninstall") {
			if (interactive) {
				TaskDialog(NULL, hInst, L"Uninstall MyProcControl (Lite)",
					L"Are you sure you want to uninstall this product?", name.c_str(),
					TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, MAKEINTRESOURCEW(IDI_ICON_APP), &btn);
				if (btn != IDYES) return ERROR_CANCELLED;
			}
			Service myService = scm.get(name);
			if (!myService.remove()) throw w32oop::exceptions::system_exception("Failed to remove service.");
			if (myService.status() == SERVICE_RUNNING) {
				if (!myService.pause_service()) throw w32oop::exceptions::system_exception("Failed to pause service.");
				Sleep(500);
			}
			if (myService.status() == SERVICE_PAUSED) {
				if (!myService.stop()) throw w32oop::exceptions::system_exception("Failed to stop service.");
			}
			RegistryKey uninstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
			uninstall.delete_key(L"Service_" + name);
				
			if (interactive) TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
				L"The product has been successfully removed from your computer.",
				L"Click OK to dismiss.",
				TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, NULL);
			return 0;
		}
		if (interactive) MessageBoxW(NULL, L"Unknown action type", L"Error", MB_ICONERROR);
		return 87;
	}
	catch (w32oop::exceptions::system_exception& e) {
		if (interactive) TaskDialog(NULL, nullptr, L"MyProcControl (Lite) Setup",
			L"The operation failed for an operation-specific reason",
			(w32oop::util::str::converts::str_wstr(e.what()) + L"\n\n" +
				ErrorChecker().message()).c_str(), TDCBF_CANCEL_BUTTON, TD_ERROR_ICON, 0);
		DWORD err = GetLastError();
		return int((!!err) ? err : 1);
	}
}

