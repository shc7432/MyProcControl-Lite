#pragma once
#include "targetver.h"
#include <atomic>
#include <string>
#include <set>
#include <mutex>

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

namespace ServiceCore {
	extern std::wstring consent_secret;
	std::wstring calculate_consent_sig(const std::wstring& payload, time_t r = 0);
	std::set<std::wstring> calculate_possible_consent_sig(const std::wstring& payload);
}

extern std::recursive_mutex consentUI_HighPermOpGlobalLock;


} // namespace MyProcControl_Lite
