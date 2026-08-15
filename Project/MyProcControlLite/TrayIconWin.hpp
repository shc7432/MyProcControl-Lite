#pragma once
#include "targetver.h"
#include <w32use.hpp>
using namespace std;

namespace MyProcControl_Lite {
namespace UIService {

class TrayIconWindow : public Window
{
public:
	TrayIconWindow(wstring svc) : Window(svc, 1, 1), svc(svc) {};
	const HICON get_window_icon() const override {
		return myicon();
	}

protected:
	wstring svc;
	TrayIcon icon;
	Menu menu;
	static HICON app_icon;
	std::wstring m_rpcEndpoint;
	void onCreated();
	virtual void setup_event_handlers() override {
		WINDOW_add_handler(WM_ENDSESSION, [this](EventData&) {
			PostQuitMessage(ERROR_SHUTDOWN_IN_PROGRESS);
		});
		WINDOW_add_handler(WM_APP + WM_QUIT, [this](EventData& ev) {
			if (ev.wParam == 1868812) DestroyWindow(hwnd);
		});
	}
	static HICON myicon();

	void LaunchWithControl();
	void handleUserLaunchError(wstring cmd, int err);
	bool LaunchElevated(wstring cmd, int& err);
};

}

int TrayIconWin_RequestLaunchProc(PCWSTR appPath, PCWSTR cmd, PCWSTR endpoint);

int TrayIconWin_RequestAttachControl(
	unsigned long pid,
	unsigned long* errorp,
	PCWSTR endpoint
);
}
