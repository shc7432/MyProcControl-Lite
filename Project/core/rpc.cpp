#include "../MyProcControlLite/targetver.h"
#include "../out/generated/midl/service_h.h"
#pragma comment(lib, "RpcRT4.lib")

extern "C" {
	void __RPC_USER MIDL_user_free(_Pre_maybenull_ _Post_invalid_ void* p)
	{
		free(p);
	}

	_Must_inspect_result_ _Ret_maybenull_ _Post_writable_byte_size_(size) void* __RPC_USER MIDL_user_allocate(_In_ size_t size)
	{
		return malloc(size);
	}
}

