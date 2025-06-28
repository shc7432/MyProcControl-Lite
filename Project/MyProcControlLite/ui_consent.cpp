#include <GUI/Window.hpp>
#include "ui_consent.hpp"

ConsentDialog::ConsentDialog(
	wstring app_name, wstring operation_name, wstring details,
	wstring allow_button_text, wstring deny_button_text,
	bool allow_remember
) : Window(L"Consent Dialog", 480, 320, 0, 0, WS_POPUP | WS_BORDER | WS_SYSMENU)
{
	m_constructor_data__app_name = app_name;
	m_constructor_data__operation_name = operation_name;
	m_constructor_data__details = details;
	m_constructor_data__allow_button_text = allow_button_text;
	m_constructor_data__deny_button_text = deny_button_text;
	m_constructor_data__allow_remember = allow_remember;

	m_result = 0;
	m_remember = false;
}

ConsentDialog::~ConsentDialog() {

}


void ConsentDialog::onCreated() {
	titlebar.set_parent(this);
	titlebar.create(L"Permission Request", 460, 25, 10, 10);

	// 获取桌面大小
	RECT rc{}, rcwindow{};
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
	GetWindowRect(hwnd, &rcwindow);

	// 把窗口放置在右下角，距离屏幕边缘 10 像素
	int width = rcwindow.right - rcwindow.left;
	int height = rcwindow.bottom - rcwindow.top;

	// 计算右下角位置（距离边缘10像素）
	int x = rc.right - width - 10;
	int y = rc.bottom - height - 10;

	move(x, y);

	set_topmost(true);
}
