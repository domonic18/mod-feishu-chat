#ifndef _MOD_FEISHU_CHAT_WEBHOOK_CLIENT_H_
#define _MOD_FEISHU_CHAT_WEBHOOK_CLIENT_H_

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "httplib.h"
#include "nlohmann/json.hpp"

#include "PCQueue.h"

namespace ModFeishuChat
{
    class FeishuWebhookClient
    {
    public:
        FeishuWebhookClient(std::string const& webhookUrl,
                            std::string const& secret,
                            int timeoutSeconds,
                            size_t maxQueueSize);
        ~FeishuWebhookClient();

        void Start();
        void Stop();
        void Enqueue(nlohmann::json payload);
        bool IsValid() const;

    private:
        void WorkerThread();
        bool Send(nlohmann::json const& payload);
        static bool ParseUrl(std::string const& url, std::string& outHost, std::string& outPath);

        std::string host_;
        std::string path_;
        std::string secret_;
        int timeoutSeconds_;
        size_t maxQueueSize_;

        std::atomic<bool> running_;
        std::thread worker_;
        ProducerConsumerQueue<nlohmann::json> queue_;
        std::unique_ptr<httplib::Client> cli_;
    };
}

#endif // _MOD_FEISHU_CHAT_WEBHOOK_CLIENT_H_
