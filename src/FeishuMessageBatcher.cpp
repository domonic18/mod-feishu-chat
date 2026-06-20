#include "FeishuMessageBatcher.h"

#include "Log.h"

namespace ModFeishuChat
{
    FeishuMessageBatcher::FeishuMessageBatcher(FlushCallback flushCallback,
                                               int batchWindowMs,
                                               size_t batchMaxSize)
        : flushCallback_(std::move(flushCallback)),
        batchWindowMs_(batchWindowMs > 0 ? batchWindowMs : 1000),
        batchMaxSize_(batchMaxSize > 0 ? batchMaxSize : 20),
        running_(false),
        lastFlush_(std::chrono::steady_clock::now())
    {
    }

    FeishuMessageBatcher::~FeishuMessageBatcher()
    {
        Stop();
    }

    void FeishuMessageBatcher::Start()
    {
        if (running_.exchange(true))
        {
            return;
        }

        lastFlush_ = std::chrono::steady_clock::now();
        worker_ = std::thread(&FeishuMessageBatcher::WorkerThread, this);
    }

    void FeishuMessageBatcher::Stop()
    {
        running_.store(false);
        cv_.notify_all();

        if (worker_.joinable())
        {
            worker_.join();
        }

        Flush();
    }

    void FeishuMessageBatcher::Enqueue(FeishuMessage* msg)
    {
        if (!msg)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.push_back(msg);
        }

        cv_.notify_all();
    }

    void FeishuMessageBatcher::WorkerThread()
    {
        while (running_.load())
        {
            std::unique_lock<std::mutex> lock(mutex_);

            auto now = std::chrono::steady_clock::now();
            auto nextFlush = lastFlush_ + std::chrono::milliseconds(batchWindowMs_);

            bool flushed = false;
            if (ShouldFlush(buffer_.size(), now))
            {
                lock.unlock();
                Flush();
                flushed = true;
            }

            if (!running_.load())
            {
                break;
            }

            if (flushed)
            {
                lock.lock();
                nextFlush = lastFlush_ + std::chrono::milliseconds(batchWindowMs_);
            }

            cv_.wait_until(lock, nextFlush, [this]() {
                if (!running_.load())
                {
                    return true;
                }

                return ShouldFlush(buffer_.size(), std::chrono::steady_clock::now());
            });
        }
    }

    void FeishuMessageBatcher::Flush()
    {
        std::vector<FeishuMessage*> buffer;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (buffer_.empty())
            {
                return;
            }

            buffer.swap(buffer_);
            lastFlush_ = std::chrono::steady_clock::now();
        }

        LOG_INFO("module", "[ModFeishuChat] Flushing {} batched messages", buffer.size());

        if (flushCallback_)
        {
            flushCallback_(std::move(buffer));
        }
        else
        {
            for (FeishuMessage* msg : buffer)
            {
                delete msg;
            }
        }
    }

    bool FeishuMessageBatcher::ShouldFlush(size_t bufferSize, std::chrono::steady_clock::time_point const& now) const
    {
        if (bufferSize == 0)
        {
            return false;
        }

        if (bufferSize >= batchMaxSize_)
        {
            return true;
        }

        return now >= lastFlush_ + std::chrono::milliseconds(batchWindowMs_);
    }
}
