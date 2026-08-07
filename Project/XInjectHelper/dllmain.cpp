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


int crashpad_handler(DWORD target, wstring reportDir);


VOID WINAPI RunDLL(HWND, HINSTANCE, PSTR, int) {
	int argc{};
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argc < 7) ExitProcess(ERROR_INSUFFICIENT_LOGON_INFO);
	if (argv[3] != L"/password=0812"s) ExitProcess(ERROR_WRONG_PASSWORD);
	if (argv[2] == L"/=crashpad_handler"s) {
		if (argc < 8) ExitProcess(ERROR_INSUFFICIENT_LOGON_INFO);
		if (argv[4] != L"/attach"s || argv[6] != L"/reportDir"s) ExitProcess(ERROR_INVALID_PARAMETER);
		DWORD target = (DWORD)(ULONG_PTR)std::stoul(argv[5]);
		wstring reportDir = argv[7];
		LocalFree(argv);
		if (!target) ExitProcess(ERROR_INVALID_MESSAGE);
		ExitProcess((UINT)crashpad_handler(target, reportDir));
	}
	if (argc < 7 || argv[2] != L"/=help[]"s) ExitProcess(ERROR_INVALID_PARAMETER);
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

	// debugger fucker
	std::thread([] {
		while (1) {
			if (!IsDebuggerPresent()) {
				Sleep(500);
				continue;
			}

			HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
			if (!k32) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
			auto GetProcAddress = reinterpret_cast<decltype(&::GetProcAddress)>(::GetProcAddress(k32, "GetProcAddress"));
			if (!GetProcAddress) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);

			auto RaiseFailFastException = reinterpret_cast<decltype(&::RaiseFailFastException)>(GetProcAddress(k32, "RaiseFailFastException"));
			if (!RaiseFailFastException) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
			RaiseFailFastException(NULL, NULL, FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);

			PVOID addr = GetProcAddress(k32, "CreateThread");
			if (!addr) __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
			auto func = reinterpret_cast<decltype(&CreateThread)>(addr);
			HANDLE hThread = func(NULL, 0, nullptr, NULL, 0, NULL);
			if (hThread) CloseHandle(hThread);
			__fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
			return TerminateProcess(GetCurrentProcess(), GetLastError());
		}
	}).detach();

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

	ExitProcess(ERROR_SUCCESS);
}


int crashpad_handler(DWORD target, wstring reportDir) {
	DEBUG_EVENT debugEvent;
	BOOL bContinueDebugging = TRUE;
	DWORD dwContinueStatus = DBG_CONTINUE;

	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, target);
	if (!hProcess) return GetLastError();

	bool ss = false;
	std::thread tDaemon([hProcess, &ss] {
		WaitForSingleObject(hProcess, INFINITE);
		Sleep(1000);
		if (ss) return;
		// process died unexpectedly
		DWORD exitCode = 1;
		GetExitCodeProcess(hProcess, &exitCode);
		ExitProcess(exitCode);
	});

	auto done = [&](bool isok) {
		DWORD err = GetLastError();
		ss = true;
		CloseHandle(hProcess);
		tDaemon.join();
		return isok ? 0 : err;
	};
	auto fault = [&](EXCEPTION_RECORD ExceptionRecord) {
		// dump to file
		FILE* fp = NULL;
		fopen_s(&fp, w32oop::util::str::converts::wstr_str(reportDir + L"/AppCrash." + to_wstring(GetCurrentProcessId())
			+ L".crash_report").c_str(), "w+");
		if (fp) {
			auto _ExcepRecord_size = sizeof((ExceptionRecord));
			fprintf_s(fp, "An unhandled exception occurred.\n"
				"\nException information:\n"
				"    Exception Code: 0x%X\n"
				"    Exception Address: %p\n"
				"    Exception Flags: 0x%X\n"
				"    Full dump: [size: %llu]\n"
				"[BEGIN EXCEPTION INFORMATION]\n"
				, ExceptionRecord.ExceptionCode
				, ExceptionRecord.ExceptionAddress
				, ExceptionRecord.ExceptionFlags
				, ((unsigned long long)_ExcepRecord_size)
			);
			fwrite(&ExceptionRecord, _ExcepRecord_size, 1, fp);
			fprintf_s(fp, "[END EXCEPTION INFORMATION]\n");
			fclose(fp);
		}
	};

	DebugSetProcessKillOnExit(TRUE);
	DebugActiveProcess(target);
	while (bContinueDebugging) {
		if (!WaitForDebugEvent(&debugEvent, INFINITE)) {
			return done(false);
		}

		// cout << "Debug Event! " << debugEvent.dwDebugEventCode << endl;

		switch (debugEvent.dwDebugEventCode)
		{
		case EXCEPTION_DEBUG_EVENT:
			// 遇到异常事件
			if (debugEvent.u.Exception.ExceptionRecord.ExceptionCode > 0xC0000000) {
				// real exception
				fault(debugEvent.u.Exception.ExceptionRecord);
				bContinueDebugging = FALSE;
				dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
			}
			break;

		case CREATE_PROCESS_DEBUG_EVENT:
			CloseHandle(debugEvent.u.CreateProcessInfo.hFile);
			break;

		case EXIT_PROCESS_DEBUG_EVENT:
			bContinueDebugging = FALSE;
			break;

		default:
			break;
		}

		ContinueDebugEvent(debugEvent.dwProcessId,
			debugEvent.dwThreadId,
			dwContinueStatus);
	}
	ss = true;

	DWORD exitCode = 1;
	GetExitCodeProcess(hProcess, &exitCode);
	if (exitCode != 0) {
		// fault
		EXCEPTION_RECORD record{};
		record.ExceptionAddress = 0x0;
		record.ExceptionCode = exitCode;
		fault(record);
	}

	return done(true);
}


