#include "BackgroundLayeredAlphaWindowClass.h"

#define MYWM_SET_TRANSPARENT (WM_USER+WS_EX_TRANSPARENT)

static decltype(RegisterClassW(0)) MyRegisterClassW(
	PCWSTR className, WNDPROC WndProc,
	WNDCLASSEXW content = WNDCLASSEXW{ 0 });
static LRESULT CALLBACK WndProc_BackgroundLayeredAlphaWindowClass(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void RegClass_BackgroundLayeredAlphaWindowClass() {
	WNDCLASSEXW wcex{ 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.lpszClassName = BackgroundLayeredAlphaWindowClassNameW;
	wcex.hCursor = LoadCursor(NULL, IDC_NO);
	wcex.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
	wcex.lpfnWndProc = WndProc_BackgroundLayeredAlphaWindowClass;
	MyRegisterClassW(0, 0, wcex);
}

static LRESULT CALLBACK WndProc_BackgroundLayeredAlphaWindowClass(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		SetWindowLong(hWnd, GWL_STYLE,
			(GetWindowLong(hWnd, GWL_STYLE) & ~WS_CAPTION)
			& ~WS_SIZEBOX & ~WS_BORDER);
		SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE)
			| WS_EX_LAYERED | WS_EX_TOOLWINDOW & ~WS_EX_APPWINDOW);
		SetLayeredWindowAttributes(hWnd, 0, 127, LWA_ALPHA);
		RECT rc{ 0 };
		HWND hParent = GetParent(hWnd);
		if (hParent) GetWindowRect(hParent, &rc);
		else {
			rc.right = GetSystemMetrics(SM_CXSCREEN);
			rc.bottom = GetSystemMetrics(SM_CYSCREEN);
		}
		SetWindowPos(hWnd, HWND_TOPMOST, 0, 0,
			rc.right - rc.left, rc.bottom - rc.top, 0);

		EnableMenuItem(GetSystemMenu(hWnd, FALSE), SC_CLOSE, MF_DISABLED | MF_GRAYED);
		RemoveMenu(GetSystemMenu(hWnd, FALSE), 1, MF_BYPOSITION);

		SetTimer(hWnd, 6, 60000, NULL);
		SetTimer(hWnd, 8, 1000, NULL);
	}
				  break;

	case MYWM_SET_TRANSPARENT:
		if (wParam) {
			//SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE)
			//	& ~WS_EX_LAYERED | WS_EX_TRANSPARENT);
			SetLayeredWindowAttributes(hWnd, 0, 1, LWA_ALPHA);
		}
		else {
			//SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE)
			//	& ~WS_EX_TRANSPARENT | WS_EX_LAYERED);
			SetLayeredWindowAttributes(hWnd, 0, 127, LWA_ALPHA);
		}
		break;

	case WM_TIMER:
		switch (wParam) { /* nIDEvent */
		case 6:
			SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
			break;
		case 8: {
			RECT rc{ 0 };
			HWND hParent = GetParent(hWnd);
			if (hParent) GetWindowRect(hParent, &rc);
			else {
				rc.right = GetSystemMetrics(SM_CXSCREEN);
				rc.bottom = GetSystemMetrics(SM_CYSCREEN);
			}
			SetWindowPos(hWnd, HWND_TOPMOST, 0, 0,
				rc.right - rc.left, rc.bottom - rc.top,
				SWP_NOACTIVATE | SWP_NOZORDER);
		}
			break;
		default:
			return DefWindowProc(hWnd, msg, wParam, lParam);
		}
		break;

	case WM_MOVE:
	case WM_SIZE:
	case WM_MOVING:
	case WM_SIZING:
	{
		RECT rc{ 0 };
		HWND hParent = GetParent(hWnd);
		if (hParent) GetWindowRect(hParent, &rc);
		else {
			rc.right = GetSystemMetrics(SM_CXSCREEN);
			rc.bottom = GetSystemMetrics(SM_CYSCREEN);
		}
		SetWindowPos(hWnd, HWND_TOPMOST, 0, 0,
			rc.right - rc.left, rc.bottom - rc.top, 0);
		break;
	}

	case WM_KEYDOWN:
		//if (wParam == VK_ESCAPE) DestroyWindow(hWnd);
		break;

	case WM_CLOSE:
		break;

	case WM_DESTROY:
		KillTimer(hWnd, 6);
		KillTimer(hWnd, 8);
		return DefWindowProc(hWnd, msg, wParam, lParam);
		break;

	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}


#define _myrclassf(key, _default) \
	if(content. ## key) wcex. ## key = content. ## key;\
	else wcex. ## key = _default;
#define _myrclassc \
	_myrclassf(style, CS_HREDRAW | CS_VREDRAW);\
	_myrclassf(lpfnWndProc, WndProc);\
	_myrclassf(cbClsExtra, 0);\
	_myrclassf(cbWndExtra, 0);\
	_myrclassf(hInstance, NULL);\
	_myrclassf(hIcon, NULL);\
	_myrclassf(hIconSm, NULL);\
	_myrclassf(lpszMenuName, NULL);\
	_myrclassf(hCursor, LoadCursor(nullptr, IDC_ARROW));\
	_myrclassf(hbrBackground, (HBRUSH)(COLOR_WINDOW + 1));\
	_myrclassf(lpszClassName, className);\

#pragma warning(push)
#pragma warning(disable: 4302)
static decltype(RegisterClassW(0))
MyRegisterClassW(PCWSTR className, WNDPROC WndProc, WNDCLASSEXW content) {
	WNDCLASSEXW wcex{ 0 };
	wcex.cbSize = sizeof(wcex);

	_myrclassc

		return RegisterClassExW(&wcex);
}
#pragma warning(pop)
#undef _myrclassc
#undef _myrclassf




