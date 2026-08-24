#include "session_worker.hpp"
#include <userenv.h>
#include "TrayIconWin.hpp"
#include <memory>
#include <wtsapi32.h>
#pragma comment(lib, "wtsapi32.lib")
using namespace std;

static HANDLE hUIProcess;

int SessionWorker(std::wstring name, DWORD ppid) {
	if (name.empty() || !ppid) return ERROR_INVALID_PARAMETER;

	HANDLE PPID = OpenProcess(GENERIC_READ | SYNCHRONIZE, FALSE, ppid);
	if (!PPID) return GetLastError();

	DWORD current_session{};
	ProcessIdToSessionId(GetCurrentProcessId(), &current_session);
	auto app = make_shared<WCHAR[]>(32768);
	GetModuleFileNameW(NULL, app.get(), 32768);
	DWORD code{};
	size_t nFailure = 0;
	HANDLE can_sync = OpenProcess(SYNCHRONIZE, TRUE, GetCurrentProcessId());
	PVOID pEnv{};
	HANDLE hToken = NULL;
	if (!WTSQueryUserToken(current_session, &hToken)) ExitProcess(GetLastError());
	if (!CreateEnvironmentBlock(&pEnv, hToken, FALSE)) {
		CloseHandle(hToken);
		return GetLastError();
	}
	while (1) {
		if (WAIT_OBJECT_0 == WaitForSingleObject(PPID, 0)) break;

		// kill other ui process
		while (HWND hwnd = FindWindowW(MyProcControl_Lite::UIService::TrayIconWindow(name).get_class_name().c_str(), name.c_str())) {
			if (SendMessageTimeoutW(hwnd, WM_APP + WM_QUIT, 1868812, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, NULL))
				for (size_t i = 0; i < 100; ++i) if (IsWindow(hwnd)) Sleep(10); else break;
			if (IsWindow(hwnd)) {
				DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
				if (pid) {
					HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
					if (hProcess) {
						TerminateProcess(hProcess, ERROR_TIMEOUT);
						CloseHandle(hProcess);
					}
				}
			}
		}

		wstring cmd = L"interfaced --type=tray-icon --name=\"" + name + L"\" --ppid=" + to_wstring((ULONG_PTR)can_sync);
		STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
		if (!CreateProcessAsUserW(hToken, app.get(), cmd.data(), NULL, NULL, TRUE,
			CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
			pEnv, NULL, &si, &pi)) {
			Sleep(1000);
			continue;
		}
		hUIProcess = pi.hProcess;
		ResumeThread(pi.hThread);
		CloseHandle(pi.hThread);
		HANDLE waits[]{ PPID, pi.hProcess };
		if (WAIT_OBJECT_0 == WaitForMultipleObjects(2, waits, 0, INFINITE)) break;
		GetExitCodeProcess(pi.hProcess, &code);
		if (code == ERROR_SHUTDOWN_IN_PROGRESS) Sleep(10000);
		++nFailure;
		//if (nFailure % 5 == 0) Sleep(5000);
	}

	DestroyEnvironmentBlock(pEnv);
	CloseHandle(can_sync);
	CloseHandle(hUIProcess);
	CloseHandle(PPID);
	return 0;
}
