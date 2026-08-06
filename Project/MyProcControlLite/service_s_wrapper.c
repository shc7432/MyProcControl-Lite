//////////////////////////////////////////////////////////////////////////////
// Wrapper: rename the server-implementation symbol so the client proxy
// (MyProcControlLite_LaunchWithControl in service_c.c) and the server stub
// can coexist in the same binary.
//
#define MyProcControlLite_LaunchWithControl MyProcControlLite_LaunchWithControl_Impl
#include "service_s.c"
