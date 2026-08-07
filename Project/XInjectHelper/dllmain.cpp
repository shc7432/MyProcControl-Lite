#include "../MyProcControlLite/targetver.h"
#include <string>
#include <functional>
#include <iostream>
#include "../../../w32oop/Utility/StringUtil/converts.hpp"
#include "../MyProcControlLite/processhelper.h"
#include "../lib/inject/inject.h"
using namespace std;

BOOL APIENTRY DllMain( HMODULE hModule,
					   DWORD  ul_reason_for_call,
					   LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}


VOID WINAPI RunDLL(HWND, HINSTANCE, PSTR, int) {
	int argc{};
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argc < 7) ExitProcess(ERROR_INVALID_PARAMETER);
	if (argv[2] != L"/=help[]"s || argv[3] != L"/password=0812"s) ExitProcess(ERROR_WRONG_PASSWORD);
	DWORD ppid = (DWORD)(ULONG_PTR)std::stoul(argv[4]);
	HANDLE hNeedClose1 = (HANDLE)(ULONG_PTR)std::stoull(argv[5]);
	HANDLE hNeedClose2 = (HANDLE)(ULONG_PTR)std::stoull(argv[6]);
	LocalFree(argv);
	class RAII {
		private: std::function<void()> _; public: RAII(std::function<void()>_) : _(_) {}~RAII() { _(); }
	};

	EnableAllPrivileges(NULL);
	if (hNeedClose1) CloseHandle(hNeedClose1);
	if (hNeedClose2) CloseHandle(hNeedClose2);
	if (IsDebuggerPresent()) DebugBreak();

#if 0
	if (ppid) {
		HANDLE hWaiter = CreateThread(NULL, 0, [](PVOID p)->DWORD {
			DWORD ppid = (DWORD)(ULONG_PTR)p;
			HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, ppid);
			if (!hProcess) return GetLastError();
			WaitForSingleObject(hProcess, INFINITE);
			CloseHandle(hProcess);
			ExitProcess(0);
			return 0;
			}, (PVOID)(ULONG_PTR)ppid, 0, 0);
		if (hWaiter) CloseHandle(hWaiter);
		else return;
	}
#endif

	string line, cmd, arg;
	while (getline(cin, line) && !cin.eof() && !cin.bad()) {
		if (line.empty()) {
			continue;
		}

		wstring wcmd = w32oop::util::str::converts::str_wstr(line);
		int argc{};
		wchar_t** argv = CommandLineToArgvW(wcmd.c_str(), &argc);
		RAII __([&argv] { LocalFree(argv); });

		if (argc < 3) continue;

		wstring taskid = argv[0];
		DWORD pid = (DWORD)std::stoul(argv[1]);
		wstring inject = argv[2];

		bool success = (InjectDllToProcess(pid, inject.c_str(), 10000));
		cout << "\"" << w32oop::util::str::converts::wstr_str(taskid) << "\" " << (success ? 1 : 0) << " " << GetLastError() << endl;
		cout.flush();
	}
}

