#include "commandline.h"
#include "processhelper.h"
#include "TrayIconWin.hpp"
using namespace std;


int RunCommandLineInterface(std::wstring name, std::wstring action, const std::array<std::string, 16>& u8extras) {
	EnableAllPrivileges(NULL);
	if (action == L"") return 87;
	else if (action == L"attach") {
		try {
			DWORD pid = stoi(u8extras[0]);
			bool ui = u8extras[1] == "y";

			wstring p = L"MyProcControlLiteRpc_" + name;

			unsigned long err = 0;
			if (!MyProcControl_Lite::TrayIconWin_RequestAttachControl(pid, &err, p.c_str())) {
				if (ui) MessageBoxW(NULL, ErrorChecker(err).message().c_str(), NULL, MB_ICONERROR | MB_TOPMOST);
			}
			else {
				if (ui) MessageBoxTimeoutW(NULL, L"Successfully attached to the process.", L"Success", MB_ICONINFORMATION, 0, 1000);
			}
		}
		catch (...) {
			return GetLastError();
		}
		return 0;
	}
	else return 87;
}


