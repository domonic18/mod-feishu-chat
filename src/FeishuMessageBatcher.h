#ifndef _MOD_FEISHU_CHAT_MESSAGE_BATCHER_H_
#define _MOD_FEISHU_CHAT_MESSAGE_BATCHER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "FeishuMessage.h"

namespace ModFeishuChat
{
    class FeishuMessageBatcher
    {
    public:
        using FlushCallback = std::function<void(std::vector<FeishuMessage*>&&)>;

        FeishuMessageBatcher(FlushCallback flushCallback,
                             int batchWindowMs,
                             size_t batchMaxSize);
        ~FeishuMessageBatcher();

        void Start();
        void Stop();
        void Enqueue(FeishuMessage* msg);

    private:
        void WorkerThread();
        void Flush();
        bool ShouldFlush(size_t bufferSize, std::chrono::steady_clock::time_point const& now) const;

        FlushCallback flushCallback_;
        int batchWindowMs_;
        size_t batchMaxSize_;

        std::atomic<bool> running_;
        std::thread worker_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<FeishuMessage*> buffer_;
        std::chrono::steady_clock::time_point lastFlush_;
    };
}

#endif // _MOD_FEISHU_CHAT_MESSAGE_BATCHER_H_
