#include "ui_consent.hpp"
#include "processhelper.h"
#include "srvapi.hpp"
#include <WtsApi32.h>
#include <VersionHelpers.h>
#include "../lib/ui/BackgroundLayeredAlphaWindowClass.h"
using namespace std;
using namespace w32oop::util::str::encodings;

static bool IsWorkStationLocked(DWORD dwSessionId = (DWORD)-1);

MyProcControl_Lite::SecondaryConsentDialog::SecondaryConsentDialog(
	wstring app_name, wstring operation_name, wstring details,
	wstring allow_button_text, wstring deny_button_text,
	bool allow_remember, bool allow_extra, DWORD times
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

	// 创建分隔线画笔
	m_hLinePen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));

	timesLeft = times;
	hLocker = 0;
}

MyProcControl_Lite::SecondaryConsentDialog::~SecondaryConsentDialog() {
	if (m_hWhiteBrush) DeleteObject(m_hWhiteBrush);
	if (m_hTitleFont) DeleteObject(m_hTitleFont);
	if (m_hLinePen) DeleteObject(m_hLinePen);
	if (contentFont) DeleteObject(contentFont);
	if (btnFont) DeleteObject(btnFont);
}


void MyProcControl_Lite::SecondaryConsentDialog::onCreated() {
	add_style_ex(WS_EX_TOOLWINDOW);

	hLocker = CreateWindowExW(0, BackgroundLayeredAlphaWindowClassNameW, L"", WS_POPUP, 0, 0, 1, 1, hwnd, 0, 0, 0);
	m_lockerText = UIService::internal::SecondaryConsentDialogLocker::SecondaryConsentDialogLocker(hLocker);
	m_lockerText.associate(*this);
	m_lockerText.create();

	if (IsWorkStationLocked()) setLocked(true);

	// 创建标题字体
	m_hTitleFont = CreateFontW(
		scaled(20), 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Consolas"
	);

	contentFont = CreateFontW(
		scaled(24), 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Consolas"
	);
	btnFont = CreateFontW(
		scaled(20), 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		L"Consolas"
	);
	
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
		if (isLocked) return;
		m_result = 0x10000000;
		notExited = false;
		this->close();
	});
	deny_button.onClick([this](EventData&) {
		if (isLocked) return;
		m_result = 0;
		notExited = false;
		this->close();
	});
	deny_button.on(BCN_DROPDOWN, [this](EventData& ev) {
		if (isLocked) return;
		if (!m_constructor_data__allow_extras) return;
		ev.preventDefault();
		showMoreOptions();
	});

	register_hot_key(true, false, false, VK_RETURN, [this](HotKeyProcData& ev) {
		ev.preventDefault();
		if (isLocked) return;
		m_result = 0x10000000;
		notExited = false;
		this->close();
	}, HotKeyOptions::Windowed);
	register_hot_key(false, false, false, VK_ESCAPE, [this](HotKeyProcData& ev) {
		ev.preventDefault();
		if (isLocked) return;
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
		if (isLocked) return;
		if (!m_constructor_data__allow_remember) return;
		remember_checkbox.check(!remember_checkbox.checked());
		m_remember = remember_checkbox.checked();
	}, HotKeyOptions::Windowed);
	register_hot_key(true, false, true, 'C', [this](HotKeyProcData& ev) {
		ev.preventDefault();
		if (isLocked) return;
		if (!doCopy()) MessageBoxW(NULL, ErrorChecker().message().c_str(), L"Cannot Copy", MB_ICONERROR);
	}, HotKeyOptions::Windowed);
	register_hot_key(false, false, true, VK_F10, [this](HotKeyProcData& ev) {
		if (!m_constructor_data__allow_extras) return;
		if (isLocked) return;
		ev.preventDefault();
		showMoreOptions();
	}, HotKeyOptions::Windowed);

	// -------

	// 获取桌面大小
	RECT rc{}, rcwindow{};
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
	::GetWindowRect(hwnd, &rcwindow);

	// 把窗口放置在右下角，距离屏幕边缘 10 像素
	int width = rcwindow.right - rcwindow.left;
	int height = rcwindow.bottom - rcwindow.top;

	// 计算右下角位置（距离边缘10像素）
	int x = rc.right - width - 10;
	int y = rc.bottom - height - 10;

	SetWindowPos(hwnd, nullptr, (x), (y), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

	// 手动定位locker窗口
	SendMessageW(hLocker, WM_SIZE, 0, 0);
	m_lockerText.center(hLocker);

	set_topmost(true);

	// -------

	timer_thread = thread([this] {
		if (timesLeft == (DWORD)-1) {
			return;
		}
		LONG64 times = (LONG64)timesLeft;
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

void MyProcControl_Lite::SecondaryConsentDialog::onDestroy() {
	notExited = false;
	SetEvent(hCloseEvent);
	WTSUnRegisterSessionNotification(hwnd);
	if (hLocker) DestroyWindow(hLocker);
	if (timer_thread.joinable()) timer_thread.join();
}

void MyProcControl_Lite::SecondaryConsentDialog::onPaint(EventData& ev) {
	ev.preventDefault();

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);

	// 1. 绘制顶部白色背景 (480x40)
	RECT headerRect = { 0, 0, scaled(480), scaled(40) };
	FillRect(hdc, &headerRect, m_hWhiteBrush);

	// 2. 绘制标题文本
	HFONT hOldFont = (HFONT)SelectObject(hdc, m_hTitleFont);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(0, 0, 0));
	TextOutW(hdc, 10, 10, L"Permission Request", 19);

	// 3. 绘制黑色分隔线
	HPEN hOldPen = (HPEN)SelectObject(hdc, m_hLinePen);
	MoveToEx(hdc, scaled(0), scaled(40), nullptr);
	LineTo(hdc, scaled(480), scaled(40));

	// 恢复原始对象
	SelectObject(hdc, hOldFont);
	SelectObject(hdc, hOldPen);

	EndPaint(hwnd, &ps);
}

void MyProcControl_Lite::SecondaryConsentDialog::onFocus(EventData& ev) {
	set_topmost(true);
}

void MyProcControl_Lite::SecondaryConsentDialog::onSessionChange(EventData& ev) {
	if (ev.wParam == WTS_SESSION_LOCK) {
		setLocked(true);
	}
	if (ev.wParam == WTS_SESSION_UNLOCK) {
		setLocked(false);
		show();
	}
}

void MyProcControl_Lite::SecondaryConsentDialog::setLocked(bool bLocked) {
	isLocked = bLocked;
	enable(!isLocked);
	ShowWindow(hLocker, isLocked ? SW_SHOW : SW_HIDE);
	SendMessageW(hLocker, WM_SIZE, 0, 0);
	if (m_lockerText.created()) { m_lockerText.show(bLocked ? SW_SHOW : SW_HIDE); m_lockerText.center(hLocker); }
	if (allow_button.created()) allow_button.enable(!isLocked);
	if (deny_button.created()) deny_button.enable(!isLocked);
	if (operation_content.created()) operation_content.enable(!isLocked);
	if (details_content.created()) details_content.enable(!isLocked);
	if (remember_checkbox.created()) remember_checkbox.enable(isLocked ? false : m_constructor_data__allow_remember);
	if (isLocked) blur();
}

void MyProcControl_Lite::SecondaryConsentDialog::showMoreOptions() {
	if (isLocked) return;
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
	}).pop(rc.left, rc.bottom, true, this);
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

bool MyProcControl_Lite::SecondaryConsentDialog::doCopy() {
	if (isLocked) return false;
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

void MyProcControl_Lite::SecondaryConsentDialog::setup_event_handlers() {
	WINDOW_add_handler(WM_NCHITTEST, [this](EventData& ev) {
		ev.returnValue(HTCAPTION);
	});
	WINDOW_add_handler(WM_CLOSE, [this](EventData& ev) {
		if (isLocked) {
			ev.preventDefault();
			return;
		}
		notExited = false;
	});
	WINDOW_add_handler(WM_PAINT, onPaint);
	WINDOW_add_handler(WM_SETFOCUS, onFocus);
	WINDOW_add_handler(WM_WTSSESSION_CHANGE, onSessionChange);
	WINDOW_add_handler(WM_USER + 0x109, [this](EventData& ev) {
		ev.preventDefault();
		hide();
		ShowWindow(hLocker, SW_HIDE);
	});
	WINDOW_add_handler(WM_USER + 0x111, [this](EventData& ev) {
		ev.preventDefault();
		// set unlocked state
		setLocked(!ev.wParam);
	});
	WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
}


static int _RunConsentUI_Secondary(
	std::wstring name, std::wstring signature, const std::array<std::string, 16>& u8extras
) {
	if (u8extras[6] != "1883") return ERROR_WRONG_PASSWORD;
	if (u8extras[8].empty()) return ERROR_INVALID_PARAMETER;
	wstring text = utf8_utf16(u8extras[7]);
	wstring text_raw = text, nonce = utf8_utf16(u8extras[8]);
	if (!MyProcControl_Lite::ConsentVerifySignature(text_raw, signature, L"MyProcControlLiteRpc_" + name))
		return ERROR_ACCESS_DENIED;
	EnableAllPrivileges(NULL);
	std::thread([&] {
		atomic<HDESK> hDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
		if (hDesk) {
			SetThreadDesktop(hDesk);
			std::thread([&hDesk] {
				while (1) {
					HDESK dsk2 = OpenInputDesktop(0, FALSE, GENERIC_ALL);
					WCHAR name1[256]{}, name2[256]{};
					DWORD nLen = 0;
					if (!hDesk || !dsk2) return;
					GetUserObjectInformationW(hDesk,
						UOI_NAME, name1, sizeof(name1), &nLen);
					GetUserObjectInformationW(dsk2,
						UOI_NAME, name2, sizeof(name2), &nLen);
					CloseDesktop(dsk2);
					if (wstring(name1) != name2 && name1[0] && name2[0]) {
						ExitProcess(ERROR_BUSY);
					}
					SleepEx(1000, TRUE);
				}
			}).detach();
		}
		DWORD ttl = 10;
		try { ttl = stoul(u8extras[5]); }
		catch (...) {}
		w32oop::util::str::operations::replace(text, nonce, L"\r\n");
		RegClass_BackgroundLayeredAlphaWindowClass();
		MyProcControl_Lite::SecondaryConsentDialog cdlg(utf8_utf16(u8extras[0]), utf8_utf16(u8extras[1]), text,
			utf8_utf16(u8extras[2]), utf8_utf16(u8extras[3]), u8extras[4] == "y", u8extras[9] == "y", ttl);
		cdlg.create();
		HANDLE hWaiter = CreateThread(NULL, 0, [](PVOID p)->DWORD {
			HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)(ULONG_PTR)p);
			if (!hProcess) return GetLastError();
			if (WAIT_OBJECT_0 != WaitForSingleObject(hProcess, INFINITE)) return GetLastError();
			CloseHandle(hProcess);
			ExitProcess(0);
			return 0;
		}, (PVOID)(ULONG_PTR)app::GetCurrentProcessPPID(), 0, 0);
		if (hWaiter) CloseHandle(hWaiter);
		cdlg.show();
		cdlg.run(&cdlg);
		if (hDesk) {
			HDESK myDesk = hDesk;
			hDesk = NULL;
			if (myDesk) CloseDesktop(myDesk);
		}

		int result = cdlg.result();
		ExitProcess(result | (cdlg.remember() ? 0x20000000 : 0));
	}).join();
	return 1;
}

static int _RunConsentUI_ScControl(
	std::wstring name, std::wstring signature, const std::array<std::string, 16>& u8extras
) {
	if (u8extras[0] != "v1") return ERROR_VERSION_PARSE_ERROR;
	wstring optype = utf8_utf16(u8extras[1]);
	wstring SigVerif = optype;
	if (!MyProcControl_Lite::ConsentVerifySignature(SigVerif, signature, L"MyProcControlLiteRpc_" + name))
		return ERROR_ACCESS_DENIED;

	static const set<wstring> allowedOpTypes{ L"pausectl", L"resumectl" };
	if (!allowedOpTypes.contains(optype)) return ERROR_UNKNOWN_COMPONENT;

	EnableAllPrivileges(NULL);
	std::thread([&] {
		auto hInput = OpenInputDesktop(0, FALSE, GENERIC_ALL);
		auto hDesk = OpenDesktopW(L"Winlogon", 0, FALSE, GENERIC_ALL);
		if (hDesk) {
			if (SetThreadDesktop(hDesk)) SwitchDesktop(hDesk);
			CloseDesktop(hDesk);
		}

		std::thread([hInput] {
			Sleep(30000);
			SwitchDesktop(hInput);
			ExitProcess(1);
		}).detach();

		int user = 0;

		INITCOMMONCONTROLSEX icex{};
		icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
		icex.dwICC = ICC_ALL_CLASSES;
		InitCommonControlsEx(&icex);

		TASKDIALOGCONFIG cfg{};
		cfg.cbSize = sizeof(TASKDIALOGCONFIG);

		wstring title;

		const TASKDIALOG_BUTTON btnsPorR[] = {
			{0x100000, L"Allow (&Y)"},
			{0x1, L"Block (&N)"},
		};
		if (optype == L"pausectl" || optype == L"resumectl") {
			title = L"Change Control State - " + name;
			cfg.pszMainInstruction = optype == L"pausectl" ?
				L"Do you really want to pause the control?" :
				L"Do you want to resume the control?";
			cfg.pszContent = optype == L"pausectl" ?
				L"A program on your device has requested to pause the control. If continue, "
				L"the control will be paused and all requests will be automatically accepted."
				L"\nDo you really want to continue?\nThis request will be blocked after 30 seconds." :
				L"Click Allow to resume the control.\nThis request will be blocked after 30 seconds.";
			cfg.pszMainIcon = optype == L"pausectl" ? TD_WARNING_ICON : TD_INFORMATION_ICON;
			cfg.cButtons = 2;
			cfg.pButtons = btnsPorR;
			cfg.nDefaultButton = 1;
		}

		RegClass_BackgroundLayeredAlphaWindowClass();
		HWND hbg = CreateWindowExW(0, BackgroundLayeredAlphaWindowClassNameW, L"", WS_OVERLAPPED, 0, 0, 1, 1, 0, 0, 0, 0);
		ShowWindow(hbg, SW_SHOW);
		SetWindowPos(hbg, HWND_TOPMOST, 0, 0, 1, 1, 0);
		cfg.hwndParent = hbg;
		cfg.pszWindowTitle = title.c_str();
		HRESULT hr = TaskDialogIndirect(&cfg, &user, nullptr, nullptr);
		if (!SUCCEEDED(hr)) user = 1;
		if (hbg) DestroyWindow(hbg);

		SwitchDesktop(hInput);
		CloseDesktop(hInput);
		ExitProcess(user);
	}).join();
	return 1;
}

int MyProcControl_Lite::RunConsentUI(
	std::wstring name, std::wstring action, std::wstring signature, const std::array<std::string, 16>& u8extras
) {
	if (action == L"secondary") return _RunConsentUI_Secondary(name, signature, u8extras);
	if (action == L"sc-control") return _RunConsentUI_ScControl(name, signature, u8extras);
	return 87;
}


static bool IsWorkStationLocked(DWORD dwSessionId) {
	if ((DWORD)-1 == dwSessionId) ProcessIdToSessionId(GetCurrentProcessId(), &dwSessionId);
	if (dwSessionId == 0) return true;

	PWTSINFOEXW pWtsInfoEx = nullptr;
	LPWSTR pBuffer = nullptr;
	DWORD dwBytesReturned = 0;

	BOOL ok = WTSQuerySessionInformationW(
		WTS_CURRENT_SERVER_HANDLE,
		dwSessionId,
		WTSSessionInfoEx,
		&pBuffer,
		&dwBytesReturned
	);
	if (!ok) return true;

	pWtsInfoEx = (PWTSINFOEXW)pBuffer;
	LONG flags = pWtsInfoEx->Data.WTSInfoExLevel1.SessionFlags;

	WTSFreeMemory(pBuffer);

	static BOOL isWin7Or2008R2 = IsWindows7OrGreater() && (!IsWindows8OrGreater());

	int r = -1;
	if (flags == WTS_SESSIONSTATE_LOCK) r = 1;
	else if (flags == WTS_SESSIONSTATE_UNLOCK) r = 0;
	if (r == -1) return true;
	if (isWin7Or2008R2) r = (r == 1 ? 0 : 1);
	return r;
}


HWND MyProcControl_Lite::UIService::internal::SecondaryConsentDialogLocker::new_window() {
	return CreateWindowExW(
		setup_info->styleEx,
		get_class_name().c_str(),
		setup_info->title.c_str(),
		setup_info->style,
		scaled(setup_info->x), scaled(setup_info->y),
		scaled(setup_info->width), scaled(setup_info->height),
		myParent, setup_info->hMenu, GetModuleHandleW(NULL), this
	);
}

void MyProcControl_Lite::UIService::internal::SecondaryConsentDialogLocker::setup_event_handlers() {}

void MyProcControl_Lite::UIService::internal::SecondaryConsentDialogLocker::onCreated() {
	// window: 240*150
	myText.set_parent(this);
	myText.create(L"Unlock the workstation\r\nor logon to process\r\nthe consent request.", 240, 70, 0, 30, Static::STYLE | SS_CENTER);
	logon.set_parent(this);
	logon.create(L"&Logon", 120, 30, 10, 110);
	dismiss.set_parent(this);
	dismiss.create(L"&Dismiss", 90, 30, 140, 110);

	logon.onClick([this](EventData&) {
		DWORD sess{};ProcessIdToSessionId(GetCurrentProcessId(), &sess);
		PWSTR pWtsUserName{}, pWtsDomainName{}; DWORD dwSize{};
		if (!(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sess,
			WTSUserName, &pWtsUserName, &dwSize) && 
			WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sess,
				WTSDomainName, &pWtsDomainName, &dwSize))) {
			logon.disable();
			return;
		}
		disable();
		w32oop::util::RAIIHelper _([this] {enable();});
		wstring user = pWtsUserName, domain = pWtsDomainName;
		WTSFreeMemory(pWtsUserName);
		WTSFreeMemory(pWtsDomainName);
		bool allowAdmin = false;
		if (user.empty()) {
			allowAdmin = true;
			optional<wstring> t;
			InputDialog idd(L"Logon User: Enter Username", 600);
			idd.create();
			idd.setButtonsText(L"Continue", L"Cancel");
			wstring prompt = L"The user specified should be an administrator.";
			t = idd.getInput<wstring>(prompt, user);
			if (!t.has_value()) return;
			user = t.value();
			idd = InputDialog(L"Logon User: Enter Domain", 600);
			idd.create();
			idd.setButtonsText(L"Continue", L"Cancel");
			if (domain.empty() || domain == L"NT AUTHORITY") {
				WCHAR b[256]{};
				GetEnvironmentVariableW(L"COMPUTERNAME", b, 256);
				domain = b;
			}
			t = idd.getInput<wstring>(L"Please enter the domain of: " + user, domain);
			if (!t.has_value()) return;
			domain = t.value();
		}
		InputDialog idd(L"Logon User: Enter Password", 600);
		idd.setPasswordMode();
		idd.create();
		idd.setButtonsText(L"Logon", L"Cancel");
		auto p = idd.getInput<wstring>(L"Enter password for " + domain + L"\\" + user);
		if (!p.has_value()) return;
		HANDLE hToken{};
		auto r = LogonUserW(user.c_str(), domain.c_str(), p.value().c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &hToken);
		if (!r || !hToken) {
			MessageBoxTimeoutW(hwnd, ErrorChecker().message().c_str(), NULL, MB_ICONERROR, 0, 5000);
			if (hToken) CloseHandle(hToken);
			return;
		}
		TOKEN_LINKED_TOKEN linkedToken{};
		DWORD retLen = 0;
		if (GetTokenInformation(hToken, TokenLinkedToken, &linkedToken, sizeof(linkedToken), &retLen)) {
			HANDLE NeedClose = hToken;
			hToken = linkedToken.LinkedToken;
			CloseHandle(NeedClose);
		}
		if (allowAdmin) do {
			HANDLE hToken2{};
			if (DuplicateTokenEx(hToken, TOKEN_QUERY | TOKEN_IMPERSONATE, NULL, SecurityImpersonation, TokenImpersonation, &hToken2)) {
				HANDLE NeedClose = hToken;
				hToken = hToken2;
				CloseHandle(NeedClose);
			}
			bool isAdmin = app::IsTokenAdministrators(hToken);
			if (isAdmin) break;
			CloseHandle(hToken);
			MessageBoxTimeoutW(hwnd, L"You don't have the permission to complete this operation.", NULL, MB_ICONERROR, 0, 5000);
			return;
		} while (0);
		CloseHandle(hToken);
		if (associated) PostMessageW(associated, WM_USER + 0x111, 1, 0);
	});

	dismiss.onClick([this](EventData&) {
		if (associated) PostMessageW(associated, WM_USER + 0x109, 0, 0);
		hide();
	});
}



