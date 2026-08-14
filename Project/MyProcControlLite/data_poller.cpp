#include "data_poller.hpp"


app::DataPoller::DataPoller(HANDLE dataSource): hSource(dataSource), running(true) {
	if (!hSource || hSource == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("Invalid pipe handle");
	}

	hWorker = CreateThread(NULL, 0, _Worker, this, 0, NULL);

	if (!hWorker) {
		throw std::runtime_error("Cannot create thread");
	}
}
app::DataPoller::DataPoller(HANDLE dataSource, std::function<void(PVOID, DWORD)> onData): hSource(dataSource), _cb(onData), running(true) {
	if (!hSource || hSource == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("Invalid pipe handle");
	}

	hWorker = CreateThread(NULL, 0, _Worker, this, 0, NULL);

	if (!hWorker) {
		throw std::runtime_error("Cannot create thread");
	}
}

app::DataPoller::~DataPoller() {
	if (hWorker) {
		CancelSynchronousIo(hWorker);
		if (WaitForSingleObject(hWorker, 3000) == WAIT_TIMEOUT) {
#pragma warning(push)
#pragma warning(disable: 6258)
			TerminateThread(hWorker, 1);
#pragma warning(pop)
		}
		CloseHandle(hWorker);
		hWorker = NULL;
	}
	hSource = NULL;
}

DWORD WINAPI app::DataPoller::_Worker(PVOID pThis) {
	DataPoller* that = static_cast<DataPoller*>(pThis);
	that->worker_loop();
	return 0;
}

void app::DataPoller::worker_loop() {
	uint8_t buffer[4096]{};
	while (running) {
		DWORD bytesRead = 0;
		if (!ReadFile(hSource, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead == 0) {
			break;
		}
		runScheduledCallback(buffer, bytesRead);
	}
}

void app::DataPoller::runScheduledCallback(PVOID buffer, DWORD len) {
	if (_cb) _cb(buffer, len);
}


app::LineDataPoller::LineDataPoller(HANDLE dataSource): DataPoller(dataSource) {}
app::LineDataPoller::LineDataPoller(HANDLE dataSource, LineCallback onLine): DataPoller(dataSource), m_lineCb(onLine) {}

void app::LineDataPoller::runScheduledCallback(PVOID buffer, DWORD len) {
	m_buffer.append(static_cast<const char*>(buffer), len);
	size_t pos = 0;
	while ((pos = m_buffer.find('\n')) != std::string::npos) {
		std::string line = m_buffer.substr(0, pos);
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (m_lineCb) m_lineCb(line);
		m_buffer.erase(0, pos + 1);
	}
	if (m_buffer.size() > 16 * 1024 * 1024) {
		m_buffer.clear();
	}
}
