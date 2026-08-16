#pragma once
#include "targetver.h"
#include <w32use.hpp>
#include <array>
using namespace std;

namespace MyProcControl_Lite {

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
	HBRUSH m_hWhiteBrush = nullptr;
	HFONT m_hTitleFont = nullptr;
	HPEN m_hLinePen = nullptr;
	HFONT contentFont;
	HFONT btnFont;

public:
	SecondaryConsentDialog(
		wstring app_name, wstring operation_name, wstring details,
		wstring allow_button_text, wstring deny_button_text,
		bool allow_remember, bool allow_extra, int times = 10
	);
	virtual ~SecondaryConsentDialog();

protected:
	virtual const COLORREF get_window_background_color() const {
		return RGB(0xF0, 0xF0, 0xF0);
	}

	Static operation_content;
	Edit details_content;
	Button allow_button, deny_button;
	CheckBox remember_checkbox;

private:
	void onCreated();
	void onDestroy();
	void onPaint(EventData& ev);
	void onFocus(EventData& ev);

	void showMoreOptions();
	bool doCopy();

	virtual void setup_event_handlers() override {
		WINDOW_add_handler(WM_NCHITTEST, [this](EventData& ev) {
			ev.returnValue(HTCAPTION);
		});
		WINDOW_add_handler(WM_CLOSE, [this](EventData& ev) {
			notExited = false;
		});
		WINDOW_add_handler(WM_PAINT, onPaint);
		WINDOW_add_handler(WM_SETFOCUS, onFocus);
	}

private:
	int m_result;
	bool m_remember;

	bool notExited = true;

	int timesLeft;
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

