#include "TrayIcon.hpp"
#include "resource.h"
#include "service_h.h"
#include <shobjidl.h>
#include <shlobj.h>

#pragma comment(lib, "RpcRT4.lib")

extern HINSTANCE hInst;

void MyProcControl_Lite::UIService::TrayIconWindow::SetRpcEndpoint(const std::wstring& svcName)
{
	m_rpcEndpoint = L"MyProcControlLiteRpc_" + svcName;
}

static int mystart(PCWSTR appPath, PCWSTR cmd, PCWSTR endpoint)
{
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
	int rpcRet = 0;
	RpcTryExcept {
		rpcRet = MyProcControlLite_LaunchWithControl(hBinding, appPath, cmd, &bSuccess);
	}
	RpcExcept(EXCEPTION_EXECUTE_HANDLER) {
		RpcBindingFree(&hBinding);
		return RPC_S_CALL_FAILED;
	}
	RpcEndExcept

	RpcBindingFree(&hBinding);

	if (rpcRet != RPC_S_OK) return rpcRet;
	if (!bSuccess) return ERROR_FUNCTION_FAILED;
	return 0;
}

HICON MyProcControl_Lite::UIService::TrayIconWindow::app_icon;

void MyProcControl_Lite::UIService::TrayIconWindow::onCreated() {
	icon.setTooltip(L"My Proc Control Lite");
	menu = Menu({
		MenuItem(L"&Launch Process with control", 0x21, [this] {
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

			std::thread([] (HWND hwnd, wstring user, wstring endpoint) {
				int err = mystart(user.c_str(), (L"\""s + user + L"\"").c_str(), endpoint.c_str());
				if (err != ERROR_SUCCESS) {
					SetLastError(err);
					MessageBoxW(hwnd, ErrorChecker().message().c_str(), NULL, MB_ICONERROR);
				}
			}, hwnd, user, m_rpcEndpoint).detach();
		}),
	});
	icon.setMenu(&menu);
	icon.setIcon(get_window_icon());
}

HICON MyProcControl_Lite::UIService::TrayIconWindow::myicon() {
	if (app_icon) return app_icon;
	return app_icon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ICON_APP));
}
