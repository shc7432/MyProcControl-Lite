#pragma once
#include "targetver.h"
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>

namespace app {

    class DataPoller {
    public:
        // 回调函数类型：参数是子进程输出的一行完整字符串（不含换行）
        using Callback = std::function<void(const std::string& line)>;

        DataPoller(HANDLE hPipeReadEnd, Callback callback);
        ~DataPoller();

        // 启动轮询线程（内部自动开始读取）
        void Start();

        // 停止轮询（线程安全）
        void Stop();

        // 是否正在运行
        bool IsRunning() const { return m_running; }

    private:
        // 工作线程函数
        void PollingThread();

        HANDLE m_hPipe;                  // 管道读端（子进程的 stdout）
        Callback m_callback;             // 业务回调
        std::thread m_worker;            // 后台读取线程
        std::atomic<bool> m_running;     // 运行标志
        std::atomic<bool> m_stopRequested; // 停止请求
    };

} // namespace app

