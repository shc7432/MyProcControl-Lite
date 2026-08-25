#pragma once
#include "targetver.h"


struct MyProcControlLite_ServiceParameters {
	bool FailOpen = false;
	DWORD ConsentTimeout = 30;
	bool NoConsentOnUnprivilegedSession = false;
};

