#include "push_data_runner.hpp"
#include <w32use.hpp>
using namespace std;

DWORD __stdcall app::DataPusher::_Myworker(PVOID pThis) {
	DataPusher* that = (DataPusher*)pThis;

	while (that->alive) {
		{
			lock_guard gg(that->queue_lock);
			deque tasks = that->queue;

			for (auto& i : tasks) {
				DWORD written = 0;
				void(WriteFile(that->hPipe, i.data, (DWORD)i.length, &written, NULL));
				if (i.signal) SetEvent(i.signal);
			}

			that->queue.clear();
		}

		HANDLE waits[]{
			that->hStopEvent,
			that->hEvent,
		};
		WaitForMultipleObjects(2, waits, FALSE, INFINITE);
	}

	return 0;
}

void app::DataPusher::push(PVOID data, SIZE_T length) {
	w32EventHandle sig = CreateEventW(0, 1, 0, 0);
	
	{
		lock_guard gg(this->queue_lock);
		queue.push_back(TaskInfo{
			.data = data,
			.length = length,
			.signal = sig
		});
		SetEvent(hEvent);
	}

	WaitForSingleObject(sig, INFINITE);
}

void app::SimpleDataPusher::push(void* data, size_t len) {
	std::lock_guard lock(mtx);
	DWORD written = 0;
	void(WriteFile(hPipe, data, (DWORD)len, &written, nullptr));
}

