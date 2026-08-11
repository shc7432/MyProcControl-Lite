#pragma once
#include "targetver.h"
#include <stdexcept>
#include <deque>
#include <mutex>

namespace app {
	class DataPusher {
	protected:
		bool alive;
		HANDLE hPipe;
		HANDLE hEvent;
		HANDLE hWorker;
		HANDLE hStopEvent;
	public:
		DataPusher(HANDLE hPipe) : hPipe(hPipe) {
			alive = true;
			hEvent = CreateEventW(0, 0, 0, 0);
			if (!hEvent) throw std::runtime_error("cannot create event");
			hStopEvent = CreateEventW(0, 1, 0, 0);
			if (!hStopEvent) throw std::runtime_error("cannot create event");
			hWorker = CreateThread(0, 0, _Myworker, this, CREATE_SUSPENDED, 0);
			if (!hWorker) throw std::runtime_error("cannot create pusher thread");
			ResumeThread(hWorker);
		}
		~DataPusher() {
			alive = false;
			if (hWorker) {
				SetEvent(hStopEvent);
				if (WaitForSingleObject(hWorker, 10000)) {
#pragma warning(push)
#pragma warning(disable: 6258)
					TerminateThread(hWorker, 1);
#pragma warning(pop)
				}
				CloseHandle(hWorker);
			}
			if (hEvent) CloseHandle(hEvent);
			if (hStopEvent) CloseHandle(hStopEvent);
		}
	public:
		struct TaskInfo {
			PVOID data;
			SIZE_T length;
			HANDLE signal;
		};
	protected:
		static DWORD WINAPI _Myworker(PVOID pThis);

		std::recursive_mutex queue_lock;
		std::deque<TaskInfo> queue;
	public:
		void push(PVOID data, SIZE_T length);
	};

	class SimpleDataPusher {
	protected:
		HANDLE hPipe;
		std::recursive_mutex mtx;
	public:
		SimpleDataPusher(HANDLE hPipe) : hPipe(hPipe) {};
		void push(void* data, size_t len);
	};
}
