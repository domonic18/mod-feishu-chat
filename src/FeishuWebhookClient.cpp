#include "FeishuWebhookClient.h"

#include "httplib.h"

#include "FeishuMessageBuilder.h"
#include "Log.h"

namespace ModFeishuChat
{
    FeishuWebhookClient::FeishuWebhookClient(std::string const& webhookUrl,
                                             std::string const& secret,
                                             std::string const& messageFormat,
                                             int timeoutSeconds,
                                             size_t maxQueueSize)
        : host_(""),
        path_(""),
        secret_(secret),
        messageFormat_(messageFormat),
        timeoutSeconds_(timeoutSeconds),
        maxQueueSize_(maxQueueSize),
        running_(false)
    {
        if (!ParseUrl(webhookUrl, host_, path_))
        {
            LOG_ERROR("module", "[ModFeishuChat] Invalid webhook URL configured.");
        }
    }

    FeishuWebhookClient::~FeishuWebhookClient()
    {
        Stop();
    }

    bool FeishuWebhookClient::IsValid() const
    {
        return !host_.empty();
    }

    void FeishuWebhookClient::Start()
    {
        if (!IsValid())
        {
            return;
        }

        running_.store(true);
        worker_ = std::thread(&FeishuWebhookClient::WorkerThread, this);
    }

    void FeishuWebhookClient::Stop()
    {
        running_.store(false);
        queue_.Shutdown();

        if (worker_.joinable())
        {
            worker_.join();
        }

        FeishuMessage* msg = nullptr;
        while (queue_.Pop(msg))
        {
            delete msg;
        }
    }

    void FeishuWebhookClient::Enqueue(FeishuMessage* msg)
    {
        if (!msg || !running_.load())
        {
            delete msg;
            return;
        }

        if (queue_.Size() >= maxQueueSize_)
        {
            LOG_WARN("module", "[ModFeishuChat] Message queue full, dropping oldest message.");
            FeishuMessage* dropped = nullptr;
            queue_.Pop(dropped);
            delete dropped;
        }

        queue_.Push(msg);
    }

    void FeishuWebhookClient::WorkerThread()
    {
        while (running_.load())
        {
            FeishuMessage* msg = nullptr;
            queue_.WaitAndPop(msg);
            if (msg)
            {
                try
                {
                    Send(*msg);
                }
                catch (...)
                {
                    LOG_ERROR("module", "[ModFeishuChat] Unhandled exception while sending message to Feishu");
                }
                delete msg;
            }
        }

        FeishuMessage* msg = nullptr;
        while (queue_.Pop(msg))
        {
            delete msg;
        }
    }

    bool FeishuWebhookClient::Send(FeishuMessage const& msg)
    {
        nlohmann::json payload = FeishuMessageBuilder::Build(msg, messageFormat_, secret_);
        std::string body = payload.dump();

        httplib::Client cli(host_);
        cli.set_connection_timeout(timeoutSeconds_);
        cli.set_read_timeout(timeoutSeconds_);
        cli.set_write_timeout(timeoutSeconds_);

        auto res = cli.Post(path_, body, "application/json");
        if (!res)
        {
            LOG_ERROR("module", "[ModFeishuChat] Failed to send message to Feishu: network error");
            return false;
        }

        if (res->status >= 200 && res->status < 300)
        {
            return true;
        }

        LOG_ERROR("module", "[ModFeishuChat] Feishu returned HTTP {}", res->status);
        return false;
    }

    bool FeishuWebhookClient::ParseUrl(std::string const& url, std::string& outHost, std::string& outPath)
    {
        std::string::size_type schemeEnd = url.find("://");
        if (schemeEnd == std::string::npos)
        {
            return false;
        }

        std::string scheme = url.substr(0, schemeEnd);
        if (scheme != "http" && scheme != "https")
        {
            return false;
        }

        std::string::size_type hostStart = schemeEnd + 3;
        std::string::size_type pathStart = url.find('/', hostStart);

        if (pathStart == std::string::npos)
        {
            outHost = url;
            outPath = "/";
            return true;
        }

        outHost = url.substr(0, pathStart);
        outPath = url.substr(pathStart);
        return true;
    }
}
