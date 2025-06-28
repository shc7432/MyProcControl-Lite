#pragma once
#include <GUI/Window.hpp>
using namespace w32oop;
using namespace w32oop::ui;
using namespace w32oop::ui::foundation;

class ConsentDialog : public Window
{
private:
	bool m_constructor_data__allow_remember;
	wstring m_constructor_data__app_name;
    wstring m_constructor_data__operation_name;
    wstring m_constructor_data__details;
    wstring m_constructor_data__allow_button_text;
    wstring m_constructor_data__deny_button_text;

public:
	ConsentDialog(
		wstring app_name, wstring operation_name, wstring details,
		wstring allow_button_text, wstring deny_button_text,
		bool allow_remember
	);
	virtual ~ConsentDialog();

protected:
	virtual const COLORREF get_window_background_color() const {
		return RGB(0xF0, 0xF0, 0xF0);
	}

	Static titlebar;
	Button close_button;

private:
	void onCreated();

	virtual void setup_event_handlers() override {
		WINDOW_add_handler(WM_NCHITTEST, [this](EventData& ev) {
			ev.returnValue(HTCAPTION);
		});
	}

private:
	int m_result;
	bool m_remember;
public:
	int result() const {
		return m_result;
	}
    bool remember() const {
        return m_remember;
    }
};


