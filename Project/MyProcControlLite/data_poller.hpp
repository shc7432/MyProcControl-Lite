#pragma once
#include "targetver.h"
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <stdexcept>

namespace app {
    class DataPoller {
    public:
        DataPoller(HANDLE dataSource);
        DataPoller(HANDLE dataSource, std::function<void(PVOID, DWORD)> onData);
        virtual ~DataPoller();
        virtual void setCallback(std::function<void(PVOID, DWORD)> cb) { this->_cb = cb; }
    protected:
        static DWORD WINAPI _Worker(PVOID pThis);
        virtual void worker_loop();
        virtual void runScheduledCallback(PVOID, DWORD);
    protected:
        std::atomic<bool> running;
        HANDLE hSource, hWorker;
        std::function<void(PVOID, DWORD)> _cb;
    };

    class LineDataPoller : public DataPoller {
    public:
        using LineCallback = std::function<void(const std::string& line)>;
        LineDataPoller(HANDLE dataSource);
        LineDataPoller(HANDLE dataSource, LineCallback onLine);
        void setCallback(LineCallback onLine) { m_lineCb = onLine; }
    protected:
        void runScheduledCallback(PVOID buffer, DWORD len) override;
    private:
        LineCallback m_lineCb;
        std::string m_buffer;
    };
}
