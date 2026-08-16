#include "commandline.h"
#include "processhelper.h"
#include "srvapi.hpp"
#include "TrayIconWin.hpp"
using namespace std;


int RunCommandLineInterface(std::wstring name, std::wstring action, const std::array<std::string, 16>& u8extras) {
	EnableAllPrivileges(NULL);
	if (action == L"") return 87;
	else if (action == L"attach") {
		try {
			DWORD pid = stoi(u8extras[0]);
			bool ui = u8extras[1] == "y";

			wstring p = L"MyProcControlLiteRpc_" + name;

			unsigned long err = 0;
			if (!MyProcControl_Lite::TrayIconWin_RequestAttachControl(pid, &err, p.c_str())) {
				if (ui) MessageBoxW(NULL, ErrorChecker(err).message().c_str(), NULL, MB_ICONERROR | MB_TOPMOST);
			}
			else {
				if (ui) MessageBoxTimeoutW(NULL, L"Successfully attached to the process.", L"Success", MB_ICONINFORMATION, 0, 1000);
			}
		}
		catch (...) {
			return GetLastError();
		}
		return 0;
	}
	else if (action == L"launch") {
		if (u8extras[0] != "1") return 87;
		try {
			DWORD pid = stoi(u8extras[1]);
			auto mem = (ULONG_PTR)stoull(u8extras[2]);
			auto size = (SIZE_T)stoull(u8extras[3]);
			if (size > 65536) return ERROR_INSUFFICIENT_BUFFER;
			w32ProcessHandle hProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, pid);
			auto Mybuf = make_unique<uint8_t[]>(size + 1); SIZE_T r{};
			if (!ReadProcessMemory(hProcess, (LPCVOID)mem, Mybuf.get(), size, &r)) return GetLastError();
			auto Mydata = (PCWSTR)Mybuf.get();
			return MyProcControl_Lite::TrayIconWin_RequestLaunchProc(L"", Mydata, (L"MyProcControlLiteRpc_" + name).c_str());
		}
		catch (...) {
			return GetLastError();
		}
		return 0;
	}
	else if (action == L"update-control-state") {
		try {
			int newState = stoi(u8extras[0]);
			wstring p = L"MyProcControlLiteRpc_" + name;

			unsigned long err = 0;
			bool ok = MyProcControl_Lite::RpcClient::ScControl(2, newState, &err, p.c_str());
			if (ok) return 0;
			else return (int)err;
		}
		catch (...) {
			return GetLastError();
		}
		return 0;
	}
	else return 87;
}


