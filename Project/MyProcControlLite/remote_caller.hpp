#pragma once
#include "targetver.h"
#include "push_data_runner.hpp"
#include "data_poller.hpp"
#include <unordered_map>
#include <mutex>
#include <future>
#include <chrono>

namespace app {

    class RemoteCaller {
    public:
        RemoteCaller(HANDLE hPipeWrite, HANDLE hPipeRead)
            : m_pusher(hPipeWrite),
            m_poller(hPipeRead, std::bind(&RemoteCaller::OnResponse, this, std::placeholders::_1)) {

            // 启动后台读取线程（内部无回调暴露给外部）
            m_poller.Start();
        }

        // 核心接口：同步调用，像本地函数一样
        // cmd: 要发送给子进程的命令（不含换行符）
        // timeoutMs: 超时时间（毫秒），防止子进程卡死导致无限等待
        std::string Execute(const std::string& cmd, DWORD timeoutMs = 5000) {
            // 1. 生成唯一任务 ID（可以用原子计数器）
            std::string taskId = std::to_string(++m_idCounter);

            // 2. 创建 Promise 和 Future（用于阻塞等待）
            std::promise<std::string> promise;
            auto future = promise.get_future();

            // 3. 将 Promise 存入待处理映射表（加锁保护）
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pending[taskId] = std::move(promise);
            }

            // 4. 通过 SimpleDataPusher 发送数据（格式：任务ID + 空格 + 实际命令）
            //    注意：子进程的逻辑是按行读取，所以这里要加换行符
            std::string sendData = taskId + " " + cmd + "\n";
            m_pusher.push((void*)sendData.c_str(), sendData.size());

            // 5. 阻塞等待 Future 完成，或者超时
            auto status = future.wait_for(std::chrono::milliseconds(timeoutMs));

            if (status == std::future_status::timeout) {
                // 超时处理：移除映射表中的条目，防止内存泄漏
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pending.erase(taskId);
                throw std::runtime_error("RemoteCaller timeout for task: " + taskId);
            }

            // 6. 获取结果并返回
            return future.get();
        }

    private:
        // 这是 DataPoller 内部调用的回调（对外完全隐藏，调用者看不到）
        void OnResponse(const std::string& line) {
            if (line.empty()) return;

            // 子进程的输出格式："taskId" 0/1 errorcode (根据你的子进程逻辑)
            // 这里我们假设第一个空格前是 taskId，后面是结果
            size_t pos = line.find(' ');
            if (pos == std::string::npos) return; // 格式错误

            std::string taskId = line.substr(0, pos);
            std::string result = line.substr(pos + 1);

            // 在映射表中查找对应的 Promise
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            auto it = m_pending.find(taskId);
            if (it != m_pending.end()) {
                // 将结果设置给 Promise，唤醒正在等待的 Execute 线程
                it->second.set_value(result);
                m_pending.erase(it); // 用完了就移除
            }
        }

    private:
        SimpleDataPusher m_pusher;          // 写端
        DataPoller m_poller;                // 读端（内部带线程）

        std::atomic<uint64_t> m_idCounter{ 0 };
        std::mutex m_pendingMutex;
        std::unordered_map<std::string, std::promise<std::string>> m_pending;
    };

} // namespace app

