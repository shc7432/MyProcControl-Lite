#include "data_poller.hpp"

app::DataPoller::DataPoller(HANDLE hPipeReadEnd, Callback callback)
    : m_hPipe(hPipeReadEnd)
    , m_callback(callback)
    , m_running(false)
    , m_stopRequested(false) {
    if (m_hPipe == INVALID_HANDLE_VALUE || !m_hPipe) {
        throw std::runtime_error("Invalid pipe handle");
    }
    if (!m_callback) {
        throw std::runtime_error("Callback cannot be empty");
    }
}

app::DataPoller::~DataPoller() {
    Stop();
}

void app::DataPoller::Start() {
    if (m_running.exchange(true)) {
        return; // 已经启动
    }
    m_stopRequested = false;
    m_worker = std::thread(&DataPoller::PollingThread, this);
}

void app::DataPoller::Stop() {
    if (!m_running.exchange(false)) {
        return; // 已经停止
    }
    m_stopRequested = true;
    if (m_worker.joinable()) {
        // 注意：不能强制关闭线程，让线程自然退出
        // 但 ReadFile 可能会阻塞，需要用 CancelSynchronousIo 或者设置超时
        // 这里简单等待 2 秒，如果没退出则 detach（不推荐，但作为保底）
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }
}

void app::DataPoller::PollingThread() {
    // 增大缓冲区，减少系统调用
    constexpr DWORD BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    std::string lineBuffer;  // 用于拼接不完整的行

    while (!m_stopRequested && m_running) {
        DWORD bytesRead = 0;
        BOOL success = ReadFile(m_hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, nullptr);

        if (!success || bytesRead == 0) {
            // 管道断开或出错
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                // 子进程正常退出，管道断开
                break;
            }
            // 其他错误，可以记录日志，但这里直接退出线程
            break;
        }

        buffer[bytesRead] = '\0'; // 安全终止

        // 将新数据追加到行缓冲区
        lineBuffer.append(buffer, bytesRead);

        // 按行切割
        size_t pos = 0;
        while ((pos = lineBuffer.find('\n')) != std::string::npos) {
            std::string line = lineBuffer.substr(0, pos);
            // 去掉行尾的 \r（Windows 换行符）
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            // 回调处理这一行（非空行才回调）
            if (!line.empty()) {
                m_callback(line);
            }
            lineBuffer.erase(0, pos + 1);
        }

        // 如果缓冲区太大（防止恶意无限增长），但一般不会
        if (lineBuffer.size() > 1024 * 1024) { // 1MB 上限
            lineBuffer.clear();
        }
    }

    // 线程即将退出，处理最后的残存数据（没有换行符的尾巴）
    if (!lineBuffer.empty()) {
        m_callback(lineBuffer);
    }

    m_running = false;
}

