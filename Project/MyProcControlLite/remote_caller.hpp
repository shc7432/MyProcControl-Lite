#pragma once
#include "targetver.h"
#include "push_data_runner.hpp"
#include "data_poller.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <future>
#include <functional>

namespace app {
	class RemoteCaller {
	public:
		RemoteCaller(HANDLE _Master_out_slave_in, HANDLE _Master_in_slave_out) :
			pusher(_Master_out_slave_in), poller(_Master_in_slave_out), last_task(0) {
			poller.setCallback([this](const std::string& _) { return this->onLine(_); });
		}
	protected:
		SimpleDataPusher pusher;
		LineDataPoller poller;
		void onLine(const std::string& line);
		std::recursive_mutex _lock;
		std::atomic<ULONGLONG> last_task;
		std::unordered_map<ULONGLONG, std::promise<std::vector<std::wstring>>> tasks;
	public:
		std::vector<std::wstring> Invoke(std::wstring args);
	};
}

