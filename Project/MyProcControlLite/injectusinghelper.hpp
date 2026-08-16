#pragma once
#include "targetver.h"
#include <thread>
#include <atomic>
#include <string>


namespace MyProcControl_Lite {
	BOOL InjectUsingHelper(DWORD dwProcessId, BOOL isWOW, std::wstring DLL);
	BOOL InjectCoreDllUsingHelper(DWORD dwProcessId, BOOL isWOW);

}

