#include "TrayIconWin.hpp"
#include "processhelper.h"
#include "resource.h"
#include "../out/generated/midl/service_h.h"
#include <shobjidl.h>
#include <shlobj.h>

#pragma comment(lib, "RpcRT4.lib")

extern HINSTANCE hInst;

int MyProcControl_Lite::TrayIconWin_RequestLaunchProc(PCWSTR appPath, PCWSTR cmd, PCWSTR endpoint) {
	RPC_WSTR bindingStr = nullptr;
	RPC_STATUS status = RpcStringBindingComposeW(
		nullptr,
		(RPC_WSTR)L"ncalrpc",
		nullptr,
		(RPC_WSTR)endpoint,
		nullptr,
		&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	RPC_BINDING_HANDLE hBinding = nullptr;
	status = RpcBindingFromStringBindingW(bindingStr, &hBinding);
	RpcStringFreeW(&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	int bSuccess = 0;
	unsigned long error = 0;
	int rpcRet = 0;
	RpcTryExcept {
		rpcRet = MyProcControlLite_LaunchWithControl(hBinding, appPath, cmd, &bSuccess, &error);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		return RPC_S_CALL_FAILED;
	}
	RpcEndExcept

	RpcBindingFree(&hBinding);

	if (rpcRet != RPC_S_OK) return rpcRet;
	SetLastError(error);
	if (!bSuccess) return error;
	return error;
}

int MyProcControl_Lite::TrayIconWin_RequestAttachControl(
	unsigned long pid,
	unsigned long* errorp,
	PCWSTR endpoint
) {
	RPC_WSTR bindingStr = nullptr;
	RPC_STATUS status = RpcStringBindingComposeW(
		nullptr,
		(RPC_WSTR)L"ncalrpc",
		nullptr,
		(RPC_WSTR)endpoint,
		nullptr,
		&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	RPC_BINDING_HANDLE hBinding = nullptr;
	status = RpcBindingFromStringBindingW(bindingStr, &hBinding);
	RpcStringFreeW(&bindingStr);
	if (status != RPC_S_OK) return (int)status;

	int bSuccess = 0;
	unsigned long error = 0;
	int rpcRet = 0;
	RpcTryExcept{
		rpcRet = MyProcControlLite_RequestAddControl(hBinding, pid, errorp);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		return RPC_S_CALL_FAILED;
	}
	RpcEndExcept

	RpcBindingFree(&hBinding);

	return rpcRet;
}


HICON MyProcControl_Lite::UIService::TrayIconWindow::app_icon;

[[nodiscard]] inline bool IsKeyDown(int vk) noexcept {
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

[[nodiscard]] inline bool IsKeyUp(int vk) noexcept {
	return !IsKeyDown(vk);
}

void MyProcControl_Lite::UIService::TrayIconWindow::onCreated() {
	icon.setTooltip(L"My Proc Control Lite");
	icon.onClick([this](EventData& ev) { ev.preventDefault(); LaunchWithControl(); });
	menu = Menu({
		MenuItem(L"&Launch Process with control", 0x21, [this] { LaunchWithControl(); }),
		MenuItem(L"Launch &Custom process with control", 0x22, [this] {
			InputDialog idd;
			idd.create();
			idd.setButtonsText(L"Launch", L"Cancel");
			auto r = idd.getInput<wstring>(L"Enter the command line:");
			if (!r.has_value() || r.value().empty()) return;
			if (IsKeyDown(VK_CONTROL) && IsKeyDown(VK_SHIFT) && 
				IsKeyUp(VK_MENU) && IsKeyUp(VK_LWIN) && IsKeyUp(VK_RWIN)) {
				int err = 0;
				if (!LaunchElevated(r.value(), err)) err = GetLastError();
				if (err) MessageBoxW(hwnd, ErrorChecker(err).message().c_str(), NULL, MB_ICONERROR | MB_TOPMOST);
				return;
			}
			int err = MyProcControl_Lite::TrayIconWin_RequestLaunchProc(L"",
				r.value().c_str(), (L"MyProcControlLiteRpc_" + svc).c_str());
			if (err != ERROR_SUCCESS) handleUserLaunchError(r.value(), err);
		}),
		MenuItem(L"Attach control to process by &PID", 0x23, [this] {
			std::thread([] (HWND hwnd, wstring svc) {
				EnableAllPrivileges(NULL);
				unsigned long err = 0;
				DWORD pid{};
				InputDialog idd;
				idd.create();
				idd.setButtonsText(L"Attach", L"Cancel");
				auto r = idd.getInput<DWORD>(L"Enter the identifier of the target process");
				if (!r.has_value() || !r.value()) return;
				pid = r.value();
				if (!TrayIconWin_RequestAttachControl(pid, &err, (L"MyProcControlLiteRpc_" + svc).c_str())) {
					if (!IsUserAnAdmin()) {
						// try elevate and try again
						wstring cmd = format(L"--type=command-line-interface --action=attach "
							L"--name=\"{}\" --extra1={} --extra2=y", svc, pid);
						auto appPath = make_shared<WCHAR[]>(32768);
						if (GetModuleFileNameW(NULL, appPath.get(), 32768)) {
							if ((INT_PTR)ShellExecuteW(NULL, L"runas", appPath.get(), cmd.c_str(), NULL, SW_NORMAL) > 32) {
								return;
							}
						}
					}
					MessageBoxW(hwnd, ErrorChecker(err).message().c_str(), NULL, MB_ICONERROR | MB_TOPMOST);
					return;
				}
				MessageBoxTimeoutW(hwnd, L"Successfully attached to the process.", L"Success", MB_ICONINFORMATION, 0, 1000);
			}, hwnd, svc).detach();
		}),
		// TODO: graphics process selector
	});
	icon.setMenu(&menu);
	icon.setIcon(get_window_icon());
}

HICON MyProcControl_Lite::UIService::TrayIconWindow::myicon() {
	if (app_icon) return app_icon;
	return app_icon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_APP));
}

void MyProcControl_Lite::UIService::TrayIconWindow::LaunchWithControl() {
	wstring user;
	IFileOpenDialog* pFileOpen = nullptr;
	HRESULT hr = CoCreateInstance(
		CLSID_FileOpenDialog,
		NULL,
		CLSCTX_ALL,
		IID_IFileOpenDialog,
		reinterpret_cast<void**>(&pFileOpen)
	);
	if (FAILED(hr)) {
		MessageBoxW(hwnd, ErrorChecker().message().c_str(), NULL, MB_ICONHAND);
		return;
	}
	// {39294400-1DA3-455B-BE42-6D680E540DF7}
	static const GUID client = { 0x39294400, 0x1da3, 0x455b, { 0xbe, 0x42, 0x6d, 0x68, 0xe, 0x54, 0xd, 0xf7 } };
	pFileOpen->SetClientGuid(client);

	COMDLG_FILTERSPEC filter[] = {
		{ L"Application", L"*.exe;*.com;*.scr" },
		{ L"All Files", L"*.*" }
	};
	pFileOpen->SetFileTypes(_countof(filter), filter);

	PWSTR pProfilePath = nullptr;
	hr = SHGetKnownFolderPath(FOLDERID_Profile, 0, NULL, &pProfilePath);
	if (SUCCEEDED(hr)) {
		IShellItem* pDefaultFolder = nullptr;
		hr = SHCreateItemFromParsingName(
			pProfilePath, NULL, IID_PPV_ARGS(&pDefaultFolder)
		);
		if (SUCCEEDED(hr)) {
			hr = pFileOpen->SetDefaultFolder(pDefaultFolder);
			pDefaultFolder->Release();
		}
		CoTaskMemFree(pProfilePath);
	}

	pFileOpen->SetTitle(L"Choose a process to launch with control");

	hr = pFileOpen->Show(hwnd);
	if (FAILED(hr)) goto cleanup;
	{
		IShellItem* pItem = nullptr;
		hr = pFileOpen->GetResult(&pItem);
		if (SUCCEEDED(hr)) {
			PWSTR filePath = nullptr;
			hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
			if (SUCCEEDED(hr)) {
				user = filePath;
				CoTaskMemFree(filePath);
			}
			pItem->Release();
		}
	}
cleanup:
	pFileOpen->Release();

	if (user.empty()) return;

	std::thread([this](HWND hwnd, wstring user, wstring endpoint) {
		int err = MyProcControl_Lite::TrayIconWin_RequestLaunchProc(
			user.c_str(), (L"\""s + user + L"\"").c_str(), endpoint.c_str());
		if (err != ERROR_SUCCESS) handleUserLaunchError((L"\""s + user + L"\""), err);
	}, hwnd, user, L"MyProcControlLiteRpc_" + svc).detach();
}

void MyProcControl_Lite::UIService::TrayIconWindow::handleUserLaunchError(wstring cmd, int err) {
	if (err == ERROR_ELEVATION_REQUIRED && !IsUserAnAdmin()) {
		int u = 0;
		TaskDialog(NULL, hInst, L"Launch with control", ErrorChecker(err).raw_message().c_str(),
			(cmd + L"\r\n\r\n" + L"Do you want to continue?").c_str(), TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON,
			TD_SHIELD_ICON, &u);
		if (u == IDYES) {
			LaunchElevated(cmd, err);
		}
	}
	MessageBoxW(hwnd, ErrorChecker(err).message().c_str(), NULL, MB_ICONERROR);
}

bool MyProcControl_Lite::UIService::TrayIconWindow::LaunchElevated(wstring cmd, int& err) {
	auto Memory = cmd.c_str();
	auto Size = (cmd.size() + 1) * sizeof(decltype(cmd)::value_type);

	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	sei.nShow = SW_SHOW;
	auto program = make_unique<WCHAR[]>(32768);
	GetModuleFileNameW(NULL, program.get(), 32768);
	sei.lpFile = program.get();
	wstring params = format(L"--type=command-line-interface --action=launch "
		L"--name=\"{}\" --extra1=1 --extra2={} --extra3={} --extra4={}", svc, GetCurrentProcessId(),
		(ULONG_PTR)Memory, Size);
	sei.lpParameters = params.c_str();
	if (ShellExecuteExW(&sei) && sei.hProcess) {
		WaitForSingleObject(sei.hProcess, INFINITE);
		DWORD code{};
		GetExitCodeProcess(sei.hProcess, &code);
		CloseHandle(sei.hProcess);
		if (code == ERROR_SUCCESS) return true;
		err = (int)code;
		return true;
	}
	else return false;
}


