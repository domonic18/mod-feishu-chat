#ifndef _MOD_FEISHU_CHAT_WEBHOOK_CLIENT_H_
#define _MOD_FEISHU_CHAT_WEBHOOK_CLIENT_H_

#include <atomic>
#include <string>
#include <thread>

#include "PCQueue.h"

#include "FeishuMessage.h"

namespace ModFeishuChat
{
    class FeishuWebhookClient
    {
    public:
        FeishuWebhookClient(std::string const& webhookUrl,
                            std::string const& secret,
                            std::string const& messageFormat,
                            int timeoutSeconds,
                            size_t maxQueueSize);
        ~FeishuWebhookClient();

        void Start();
        void Stop();
        void Enqueue(FeishuMessage* msg);
        bool IsValid() const;

    private:
        void WorkerThread();
        bool Send(FeishuMessage const& msg);
        static bool ParseUrl(std::string const& url, std::string& outHost, std::string& outPath);

        std::string host_;
        std::string path_;
        std::string secret_;
        std::string messageFormat_;
        int timeoutSeconds_;
        size_t maxQueueSize_;

        std::atomic<bool> running_;
        std::thread worker_;
        ProducerConsumerQueue<FeishuMessage*> queue_;
    };
}

#endif // _MOD_FEISHU_CHAT_WEBHOOK_CLIENT_H_
