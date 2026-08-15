#pragma once
#include "targetver.h"
#include <atomic>
#include <string>

namespace MyProcControl_Lite {

class RpcServer {
public:
	RpcServer() = default;
	~RpcServer();

	// Start the RPC server (non-blocking).  Returns true on success.
	bool Start(const std::wstring& serviceName);

	// Stop the RPC server.  Returns true on success.
	bool Stop();

private:
	std::atomic<bool> m_running{false};
	std::wstring m_endpoint;
};

bool ConsentVerifySignature(std::wstring payload, std::wstring sig, std::wstring endpoint);

} // namespace MyProcControl_Lite
