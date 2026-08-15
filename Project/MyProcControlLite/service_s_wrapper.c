//////////////////////////////////////////////////////////////////////////////
// Wrapper: rename the server-implementation symbol so the client proxy
// (MyProcControlLite_LaunchWithControl in service_c.c) and the server stub
// can coexist in the same binary.
//
#define MyProcControlLite_ConsentUI_CheckAuthorization MyProcControlLite_ConsentUI_CheckAuthorization_Impl
#define MyProcControlLite_LaunchWithControl MyProcControlLite_LaunchWithControl_Impl
#define MyProcControlLite_Consent_CreateProcess MyProcControlLite_Consent_CreateProcess_Impl
#define MyProcControlLite_RequestAddControl MyProcControlLite_RequestAddControl_Impl
#include "../out/generated/midl/service_s.c"
