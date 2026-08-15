#include "injectusinghelper.hpp"
using namespace std;


namespace MyProcControl_Lite {
	class MyWindowsServiceInjectHelper {
	public:
		static BOOL InjectUsingHelper(DWORD dwProcessId, BOOL isWOW, std::wstring DLL);
		static BOOL InjectCoreDllUsingHelper(DWORD dwProcessId, BOOL isWOW);
	};
}


BOOL MyProcControl_Lite::InjectUsingHelper(DWORD dwProcessId, BOOL isWOW, std::wstring DLL) {
	return MyWindowsServiceInjectHelper::InjectUsingHelper(dwProcessId, isWOW, DLL);
}


BOOL MyProcControl_Lite::InjectCoreDllUsingHelper(DWORD dwProcessId, BOOL isWOW) {
	return MyWindowsServiceInjectHelper::InjectCoreDllUsingHelper(dwProcessId, isWOW);
}


BOOL MyProcControl_Lite::MyWindowsServiceInjectHelper::InjectCoreDllUsingHelper(DWORD dwProcessId, BOOL isWOW) {
	wstring DLL = isWOW ? ServiceCoreProcess::Instance->coredll86 : ServiceCoreProcess::Instance->coredll64;
	return InjectUsingHelper(dwProcessId, isWOW, DLL);
}


BOOL MyProcControl_Lite::MyWindowsServiceInjectHelper::InjectUsingHelper(DWORD dwProcessId, BOOL isWOW, std::wstring DLL) {
	if (dwProcessId == GetCurrentProcessId() || dwProcessId == ServiceCoreProcess::Instance->ppid) {
		SetLastError(0xC0000010);
		return FALSE;
	}
	auto& p = isWOW ? ServiceCoreProcess::Instance->injectHelperCaller86 : ServiceCoreProcess::Instance->injectHelperCaller64;
	if (!p) {
		SetLastError(STATUS_ACCESS_VIOLATION);
		return FALSE;
	}
	try {
		// format: <PID> <DLL>
		auto ret = p->Invoke(to_wstring(dwProcessId) + L" \"" + DLL + L"\"");
		if (!ret.size()) {
			SetLastError(ERROR_INTERNAL_ERROR);
			return FALSE;
		}
		if (!stoull(ret[0])) {
			SetLastError(ERROR_INTERNAL_ERROR);
			return FALSE;
		}
		return TRUE;
	}
	catch (...) {
		return FALSE;
	}
}



