#include "setupui.h"
#include <w32use.hpp>
#include <shlobj.h>
#include <fstream>
#include "configtool.hpp"
#include "processhelper.h"
#include "resource.h"
using namespace std;

extern HINSTANCE hInst;

signed char __stdcall FileDeleteTreeW(std::wstring szPath, bool = false);


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
				L"RpcSs\0DcomLaunch\0LSM\0SamSs\0ProfSvc\0UserManager\0",
				NULL, NULL));
			SERVICE_DESCRIPTIONW a{};
			WCHAR desc[] = L"Process Control Server";
			a.lpDescription = desc;
			ChangeServiceConfig2W(myService, SERVICE_CONFIG_DESCRIPTION, &a);

			int user = 0;
			if (interactive) TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
				L"The product has been successfully installed to your computer.",
				format(L"Do you want to start the service now?\n\nIf you don't want to start now, you can run the "
					L"following command later to start the service:\nSC.EXE START \"{}\"", name).c_str(),
				TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, &user);
			if (user == IDYES) {
				if (!myService.start()) throw w32oop::exceptions::system_exception("Failed to start service.");
			}
			return 0;
		}
		if (action == L"uninstall") {
			BOOL keep = false;
			if (interactive) {
				TASKDIALOGCONFIG c{};
				c.cbSize = sizeof(c);
				c.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON;
				c.hMainIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_APP));
				c.pszVerificationText = L"&Keep the application executable file?";
				c.pszWindowTitle = L"Uninstall MyProcControl (Lite)";
				c.pszMainInstruction = L"Are you sure you want to uninstall this product?";
				c.pszContent = name.c_str();
				c.dwFlags = TDF_USE_HICON_MAIN;
				TaskDialogIndirect(&c, &btn, NULL, &keep);
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
			try {
				RegistryKey uninstall(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
				if (uninstall.exists(L"Service_" + name)) try {
					auto p = uninstall.open(L"Service_" + name);
					auto tmp = p.get_value<wstring>(L"MyData:TempDir");
					if (!tmp.empty()) FileDeleteTreeW(tmp + L"\\");
				} catch (...) {
					MessageBoxW(0, ErrorChecker().message().c_str(), 0, MB_ICONHAND);
				}
				if (uninstall.exists(L"Service_" + name)) uninstall.delete_key(L"Service_" + name);
			}
			catch (...) {}

			if (interactive) TaskDialog(NULL, NULL, L"MyProcControl (Lite) Setup",
				L"The product has been successfully removed from your computer.",
				L"Click OK to dismiss.",
				TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON, TD_INFORMATION_ICON, NULL);

			if (!keep) {
				WCHAR buf[260]{};
				GetTempPathW(260, buf);
				wstring f = buf;
				f += L"\\";
				f += GenerateUUIDW();
				f += L".sct";
				auto appPath = make_shared<WCHAR[]>(32768);
				GetModuleFileNameW(NULL, appPath.get(), 32768);
				ofstream fp(f, ios::out | ios::binary);
				wstring sApp = appPath.get();
				w32oop::util::str::operations::replace(sApp, L"\\", L"\\\\");
				wstring cont = L"<?XML version=\"1.0\"?><scriptlet><registration classid=\"{" + GenerateUUIDW() +
					L"}\"><script language=\"JScript\"><![CDATA[\r\nvar i=0,f=\"" + sApp + L"\",a=new ActiveXObject(\""
					"Scripting.FileSystemObject\");for(;i<9999999;++i){try{if(!a.FileExists(f))break;a."
					"DeleteFile(f,true);break}catch(e){}}\r\n]]>\r\n</script></registration></scriptlet>";
				fp.write("\xff\xfe", 2);
				fp.write((PCSTR)cont.data(), cont.size() * sizeof(decltype(cont)::value_type));
				fp.close();
				STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
				WCHAR s32[260]{}; GetSystemDirectoryW(s32, 260);
				wstring cmd = format(L"regsvr32 /u /n /s /i:\"{}\" \"{}\\scrobj.dll\"", f, s32);
				if (!CreateProcessW((s32 + L"/regsvr32.exe"s).c_str(), cmd.data(), 0, 0, 0, 0, 0, 0, &si, &pi)) {
					if (interactive) MessageBoxW(NULL, (L"Cannot Delete Remaining File! Please manually delete "s +
						appPath.get()).c_str(), NULL, MB_ICONERROR);
				}
				else {
					CloseHandle(pi.hProcess);
					CloseHandle(pi.hThread);
				}
			}
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


signed char __stdcall FileDeleteTreeW(std::wstring szPath, bool __internal_is_first_call__) {
	signed char status = 0;
	if (__internal_is_first_call__) {
		if (szPath.find(L"/") != wstring::npos) w32oop::util::str::operations::replace(szPath, L"/", L"\\");
		if (!szPath.ends_with(L"\\")) szPath += L"\\";
	}
	WIN32_FIND_DATAW findd{};
	HANDLE hFind = FindFirstFileW((szPath + L"*").c_str(), &findd);
	if (!hFind || hFind == INVALID_HANDLE_VALUE) {
		status = -1;
		return status;
	}
	do {
		if (wcscmp(findd.cFileName, L".") == 0 ||
			wcscmp(findd.cFileName, L"..") == 0) continue;
		wstring wstrFileName;
		wstrFileName.assign(szPath);
		//wstrFileName.append(L"\\");
		wstrFileName.append(findd.cFileName);
		if (findd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			wstrFileName.append(L"\\");
			auto result0 = FileDeleteTreeW(wstrFileName, false);
			if (result0 != 0) status = (status == -1) ? -1 : 1;
		}
		else {
			if (findd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
				SetFileAttributesW(wstrFileName.c_str(), FILE_ATTRIBUTE_NORMAL);
			if (!DeleteFileW(wstrFileName.c_str()))
				status = (status == -1) ? -1 : 1;
		}
	} while (FindNextFileW(hFind, &findd));
	FindClose(hFind);
	if (!RemoveDirectoryW(szPath.c_str()))
		status = (status == -1) ? -1 : 1;
	return status;
}


