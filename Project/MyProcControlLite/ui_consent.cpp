#include "ui_consent.hpp"
using namespace std;

MyProcControl_Lite::ConsentDialog::ConsentDialog(
	wstring app_name, wstring operation_name, wstring details,
	wstring allow_button_text, wstring deny_button_text,
	bool allow_remember, bool allow_extra, int times
) : Window(L"Consent Dialog", 480, 320, 0, 0, WS_POPUP | WS_BORDER | WS_SYSMENU)
{
	m_constructor_data__app_name = app_name;
	m_constructor_data__operation_name = operation_name;
	m_constructor_data__details = details;
	m_constructor_data__allow_button_text = allow_button_text;
	m_constructor_data__deny_button_text = deny_button_text;
	m_constructor_data__allow_remember = allow_remember;
	m_constructor_data__allow_extras = allow_extra;

	m_result = 0;
	m_remember = false;

	hCloseEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	// 创建白色背景画刷
	m_hWhiteBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));

	// 创建标题字体
	m_hTitleFont = CreateFont(
		20, 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Consolas"
	);

	// 创建分隔线画笔
	m_hLinePen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));

	contentFont = CreateFont(
		24, 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Consolas"
	);
	btnFont = CreateFont(
		20, 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Consolas"
	);

	timesLeft = times;
}

MyProcControl_Lite::ConsentDialog::~ConsentDialog() {
	if (m_hWhiteBrush) DeleteObject(m_hWhiteBrush);
	if (m_hTitleFont) DeleteObject(m_hTitleFont);
	if (m_hLinePen) DeleteObject(m_hLinePen);
	if (contentFont) DeleteObject(contentFont);
	if (btnFont) DeleteObject(btnFont);
}


void MyProcControl_Lite::ConsentDialog::onCreated() {
	add_style_ex(WS_EX_TOOLWINDOW);
	
	operation_content.set_parent(this);
	operation_content.create(m_constructor_data__app_name + L" wants to " + 
		m_constructor_data__operation_name, 460, 50, 10, 50);
	operation_content.font(contentFont);

	details_content.set_parent(this);
	details_content.create(L"", 460, 115, 10, 110, Edit::STYLE | ES_MULTILINE |
		ES_AUTOVSCROLL | WS_VSCROLL | ES_AUTOHSCROLL | WS_HSCROLL);
	details_content.readonly(true);
	details_content.text(m_constructor_data__details);

	allow_button.set_parent(this);
	allow_button.create(m_constructor_data__allow_button_text + (L" (Ctrl + Enter)"), 225, 40, 10, 235);
	deny_button.set_parent(this);
	deny_button.create(m_constructor_data__deny_button_text + (L" (Esc)"), 225, 40, 240, 235, 
		Button::STYLE | (m_constructor_data__allow_extras ? BS_SPLITBUTTON : 0));
	allow_button.font(btnFont);
	deny_button.font(btnFont);

	allow_button.onClick([this](EventData&) {
		m_result = 0x10000000;
		notExited = false;
		this->close();
	});
	deny_button.onClick([this](EventData&) {
		m_result = 0;
		notExited = false;
		this->close();
	});
	deny_button.on(BCN_DROPDOWN, [this](EventData& ev) {
		if (!m_constructor_data__allow_extras) return;
		ev.preventDefault();
		showMoreOptions();
	});

	register_hot_key(true, false, false, VK_RETURN, [this](HotKeyProcData& ev) {
		ev.preventDefault();
		m_result = 0x10000000;
		notExited = false;
		this->close();
	}, HotKeyOptions::Windowed);
	register_hot_key(false, false, false, VK_ESCAPE, [this](HotKeyProcData& ev) {
		ev.preventDefault();
		m_result = 0;
		notExited = false;
		this->close();
	}, HotKeyOptions::Windowed);

	remember_checkbox.set_parent(this);
	remember_checkbox.create(L"[Ctrl+R] Remember my choice for " + m_constructor_data__app_name, 460, 20, 10, 285);
	remember_checkbox.onChanged([this](EventData& ev) {
		m_remember = remember_checkbox.checked();
	});
	remember_checkbox.enable(m_constructor_data__allow_remember);
	register_hot_key(true, false, false, 'R', [this](HotKeyProcData& ev) {
		ev.preventDefault();
		if (!m_constructor_data__allow_remember) return;
		remember_checkbox.check(!remember_checkbox.checked());
		m_remember = remember_checkbox.checked();
	}, HotKeyOptions::Windowed);
	register_hot_key(true, false, true, 'C', [this](HotKeyProcData& ev) {
		ev.preventDefault();
		if (!doCopy()) MessageBoxW(NULL, ErrorChecker().message().c_str(), L"Cannot Copy", MB_ICONERROR);
	}, HotKeyOptions::Windowed);
	register_hot_key(false, false, true, VK_F10, [this](HotKeyProcData& ev) {
		if (!m_constructor_data__allow_extras) return;
		ev.preventDefault();
		showMoreOptions();
	}, HotKeyOptions::Windowed);

	// -------

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

	move_to(x, y);

	set_topmost(true);

	// -------

	timer_thread = thread([this] {
		int times = timesLeft;
		if (!times) return;
		wstring origText = deny_button.text();
		while (notExited) {
			if (times < 0) {
				m_result = 0;
				m_remember = false;
				this->close();
				return;
			}
			deny_button.text(origText + L" (" + to_wstring(times) + L")");
			WaitForSingleObject(hCloseEvent, 1000);
			--times;
		};
	});

	thread([this] { force_focus(); }).detach();
}

void MyProcControl_Lite::ConsentDialog::onDestroy() {
	notExited = false;
	SetEvent(hCloseEvent);
	if (timer_thread.joinable()) timer_thread.join();
}

void MyProcControl_Lite::ConsentDialog::onPaint(EventData& ev) {
	ev.preventDefault();

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);

	// 1. 绘制顶部白色背景 (480x40)
	RECT headerRect = { 0, 0, 480, 40 };
	FillRect(hdc, &headerRect, m_hWhiteBrush);

	// 2. 绘制标题文本
	HFONT hOldFont = (HFONT)SelectObject(hdc, m_hTitleFont);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(0, 0, 0));
	TextOutW(hdc, 10, 10, L"Permission Request", 19);

	// 3. 绘制黑色分隔线
	HPEN hOldPen = (HPEN)SelectObject(hdc, m_hLinePen);
	MoveToEx(hdc, 0, 40, nullptr);
	LineTo(hdc, 480, 40);

	// 恢复原始对象
	SelectObject(hdc, hOldFont);
	SelectObject(hdc, hOldPen);

	EndPaint(hwnd, &ps);
}

void MyProcControl_Lite::ConsentDialog::onFocus(EventData& ev) {
	set_topmost(true);
}

void MyProcControl_Lite::ConsentDialog::showMoreOptions() {
	RECT rc{}; GetWindowRect(deny_button, &rc);
	int ret = Menu({
		MenuItem(m_constructor_data__deny_button_text, 1),
		MenuItem::separator(),
		MenuItem(L"&Copy\tCtrl+Shift+C", 4),
		MenuItem::separator(),
		MenuItem(L"&Block all further requests within...", {
			MenuItem(L"5 seconds", 5),
			MenuItem(L"10 seconds", 10),
			MenuItem(L"30 seconds", 30),
			MenuItem(L"1 minute", 60),
			MenuItem(L"2 minutes", 120),
			MenuItem(L"5 minutes", 300),
			MenuItem(L"10 minutes", 600),
			MenuItem(L"15 minutes", 900),
			MenuItem(L"20 minutes", 1200),
			MenuItem(L"30 minutes", 1800),
			MenuItem(L"45 minutes", 60 * 45),
			MenuItem(L"1 hour", 3600),
			MenuItem(L"2 hour", 7200),
			MenuItem(L"5 hour", 3600 * 5),
			MenuItem(L"6 hour", 3600 * 6),
			MenuItem(L"12 hour", 3600 * 12),
			MenuItem(L"1 day", 86400),
		}),
		MenuItem::separator(),
		MenuItem(L"C&lose the application", 2),
		MenuItem(L"Close and &uninstall the application", 3),
	}).pop(rc.left, rc.bottom);
	if (!ret) return;
	switch (ret) {
	case 1:
		m_result = 0;
		break;
	case 2:
		m_result = 0x00100000;
		break;
	case 3:
		m_result = 0x00200000;
		break;
	case 4:
		if (!doCopy()) MessageBoxW(NULL, ErrorChecker().message().c_str(), L"Cannot Copy", MB_ICONERROR);
		return;
		break;
	default:
		m_result = 0x00400000 + ret;
	}
	notExited = false;
	this->close();
}

bool MyProcControl_Lite::ConsentDialog::doCopy() {
	wstring text;

	// get data
	text = L"[Window Title]\r\nPermission Request\r\n\r\n[Main Instruction]\r\n" + operation_content.text() +
		L"\r\n\r\n[Content]\r\n" + m_constructor_data__details + format(L"\r\n\r\n[{}] [{}]\r\n[{}] Remember my choice for {}",
			m_constructor_data__allow_button_text, m_constructor_data__deny_button_text, remember_checkbox.checked() ? L"x" : L" ",
			m_constructor_data__app_name);

	// do copy
	if (text.empty() || !OpenClipboard(hwnd)) return false;
	EmptyClipboard();
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
	if (!hMem) {
		CloseClipboard();
		return false;
	}
	wchar_t* pBuf = (wchar_t*)GlobalLock(hMem);
	if (!pBuf) {
		CloseClipboard();
		return false;
	}
	wcscpy_s(pBuf, (text.size() + 1), text.c_str());
	GlobalUnlock(hMem);
	SetClipboardData(CF_UNICODETEXT, hMem);
	CloseClipboard();
	MessageBeep(MB_ICONINFORMATION);
	return true;
}


