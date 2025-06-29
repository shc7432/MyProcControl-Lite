#pragma once
#include <w32use.hpp>
using namespace std;

class ConsentDialog : public Window
{
private:
	bool m_constructor_data__allow_remember;
	wstring m_constructor_data__app_name;
	wstring m_constructor_data__operation_name;
	wstring m_constructor_data__details;
	wstring m_constructor_data__allow_button_text;
	wstring m_constructor_data__deny_button_text;

private:
	HBRUSH m_hWhiteBrush = nullptr;
	HFONT m_hTitleFont = nullptr;
	HPEN m_hLinePen = nullptr;
	HFONT contentFont;
	HFONT btnFont;

public:
	ConsentDialog(
		wstring app_name, wstring operation_name, wstring details,
		wstring allow_button_text, wstring deny_button_text,
		bool allow_remember, int times = 10
	);
	virtual ~ConsentDialog();

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

	virtual void setup_event_handlers() override {
		WINDOW_add_handler(WM_NCHITTEST, [this](EventData& ev) {
			ev.returnValue(HTCAPTION);
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


