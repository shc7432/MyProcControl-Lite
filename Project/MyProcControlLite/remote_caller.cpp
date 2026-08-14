#include "remote_caller.hpp"
#include <w32use.hpp>
using namespace std;

std::vector<std::wstring> app::RemoteCaller::Invoke(std::wstring args) {
	ULONGLONG tid;
	{
		tid = ++last_task;
	}

	std::promise<std::vector<std::wstring>> promise;
	auto future = promise.get_future();

	{
		std::lock_guard gg(_lock);
		tasks.emplace(tid, std::move(promise));
	}

	std::string cmd = to_string(tid) + " " + w32oop::util::str::converts::wstr_str(args) + "\n";
	pusher.push(cmd.data(), cmd.size());

	auto status = future.wait_for(std::chrono::seconds(10));
	if (status == std::future_status::timeout) {
		std::lock_guard lock(_lock);
		tasks.erase(tid);
		throw std::runtime_error("Timeout");
	}

	auto result = future.get();
	return result;
}

void app::RemoteCaller::onLine(const std::string& line) {
	wstring wcmd = w32oop::util::str::converts::str_wstr(line);
	int argc{};
	wchar_t** argv = CommandLineToArgvW(wcmd.c_str(), &argc);
	w32oop::util::RAIIHelper __([&argv] { LocalFree(argv); });

	if (argc < 1) return;
	ULONGLONG tid;
	try {
		tid = std::stoull(argv[0]);
	}
	catch (...) { return; }
	vector<wstring> out;
	for (int i = 1; i < argc; ++i) {
		out.push_back(argv[i]);
	}
	lock_guard gg(_lock);
	auto it = tasks.find(tid);
	if (it == tasks.end()) return;
	it->second.set_value(out);
	tasks.erase(it);
}

