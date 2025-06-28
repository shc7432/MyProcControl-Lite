#include <windows.h>
#include "../../../MyLearn/resource/tool.h"
#include "ui_consent.hpp"
using namespace std;


int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	CmdLineW cl(lpCmdLine);
	wstring type; cl.getopt(L"type", type);

	if (type == L"ui-service") {

	}

	if (type == L"consent-test") {
		ConsentDialog cdlg(L"MyApp.exe", L"CreateProcess", L"Process Name: cmd.exe", L"Allow", L"Block", true);
		cdlg.create();
		cdlg.show();
		cdlg.run(&cdlg);
		MessageBox(NULL, (cdlg.result() == 1) ? L"Allowed" : L"Blocked", L"Consent Test", MB_OK | MB_ICONINFORMATION);
		return 0;
	}

	return 0;
}