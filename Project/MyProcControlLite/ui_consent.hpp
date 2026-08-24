#pragma once
#include "targetver.h"
#include <w32use.hpp>
#include <array>
using namespace std;

namespace MyProcControl_Lite {

namespace UIService::internal {
	class SecondaryConsentDialogLocker : public Window {
	public:
		SecondaryConsentDialogLocker() : Window(), myParent(NULL) {}
		SecondaryConsentDialogLocker(HWND parent) : Window(L"Lockdown", 240, 150, 0, 0, WS_POPUP | WS_BORDER), myParent(parent) {}
		
		void setType(bool bTypeIsPriv) { isPriv = bTypeIsPriv; }
		void associate(HWND hwnd) { associated = hwnd; }

	protected:
		COLORREF get_window_background_color() override {
			return RGB(0xF0, 0xF0, 0xF0);
		}
		HWND associated{};
		bool isPriv{};
		Static myText;
		Button logon, dismiss;
		HWND myParent;
		HWND new_window() override;
		void onCreated() override;
		void setup_event_handlers() override;
	};
}

class SecondaryConsentDialog : public Window
{
private:
	bool m_constructor_data__allow_remember;
	wstring m_constructor_data__app_name;
	wstring m_constructor_data__operation_name;
	wstring m_constructor_data__details;
	wstring m_constructor_data__allow_button_text;
	wstring m_constructor_data__deny_button_text;
	bool m_constructor_data__allow_extras;

private:
	w32EventHandle hCloseEvent;
	HWND hLocker;
	UIService::internal::SecondaryConsentDialogLocker m_lockerText;
	HBRUSH m_hWhiteBrush = nullptr;
	HFONT m_hTitleFont = nullptr;
	HPEN m_hLinePen = nullptr;
	HFONT contentFont{};
	HFONT btnFont{};

public:
	SecondaryConsentDialog(
		wstring app_name, wstring operation_name, wstring details,
		wstring allow_button_text, wstring deny_button_text,
		bool allow_remember, bool allow_extra, bool isPrivReq, DWORD times = 10
	);
	virtual ~SecondaryConsentDialog();

protected:
	COLORREF get_window_background_color() override {
		return RGB(0xF0, 0xF0, 0xF0);
	}

	Static operation_content;
	Edit details_content;
	Button allow_button, deny_button;
	CheckBox remember_checkbox;

private:
	void onCreated() override;
	void onDestroy() override;
	void onPaint(EventData& ev);
	void onFocus(EventData& ev);
	void onSessionChange(EventData& ev);

	bool isLocked = false;
	bool isPrivilegeRequired = false;
	void setLocked(bool bLocked);
	void showMoreOptions();
	bool doCopy();

	virtual void setup_event_handlers() override;

private:
	int m_result;
	bool m_remember;

	bool notExited = true;

	DWORD timesLeft;
	thread timer_thread;
public:
	int result() const {
		return m_result;
	}
	bool remember() const {
		return m_remember;
	}
};

int RunConsentUI(std::wstring name, std::wstring action, std::wstring signature, const std::array<std::string, 16>& u8extras);


}

