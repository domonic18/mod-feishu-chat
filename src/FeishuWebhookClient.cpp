#include "FeishuWebhookClient.h"

#include "Log.h"

namespace ModFeishuChat
{
    FeishuWebhookClient::FeishuWebhookClient(std::string const& webhookUrl,
                                             std::string const& secret,
                                             int timeoutSeconds,
                                             size_t maxQueueSize)
        : host_(""),
        path_(""),
        secret_(secret),
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
        if (!IsValid() || running_.exchange(true))
        {
            return;
        }

        cli_ = std::make_unique<httplib::Client>(host_);
        cli_->set_connection_timeout(timeoutSeconds_);
        cli_->set_read_timeout(timeoutSeconds_);
        cli_->set_write_timeout(timeoutSeconds_);
        cli_->enable_server_certificate_verification(true);

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

        nlohmann::json payload;
        while (queue_.Pop(payload))
        {
            // discard
        }

        cli_.reset();
    }

    void FeishuWebhookClient::Enqueue(nlohmann::json payload)
    {
        if (payload.is_null() || !running_.load())
        {
            LOG_WARN("module", "[ModFeishuChat] Dropping payload, webhook worker is not running");
            return;
        }

        if (queue_.Size() >= maxQueueSize_)
        {
            LOG_WARN("module", "[ModFeishuChat] Payload queue full, dropping oldest payload.");
            nlohmann::json dropped;
            queue_.Pop(dropped);
        }

        LOG_INFO("module", "[ModFeishuChat] Enqueued payload for forwarding");
        queue_.Push(std::move(payload));
    }

    void FeishuWebhookClient::WorkerThread()
    {
        while (running_.load())
        {
            nlohmann::json payload;
            queue_.WaitAndPop(payload);
            if (!payload.is_null())
            {
                try
                {
                    Send(payload);
                }
                catch (...)
                {
                    LOG_ERROR("module", "[ModFeishuChat] Unhandled exception while sending payload to Feishu");
                }
            }
        }

        nlohmann::json payload;
        while (queue_.Pop(payload))
        {
            // discard
        }
    }

    bool FeishuWebhookClient::Send(nlohmann::json const& payload)
    {
        std::string body = payload.dump();

        LOG_INFO("module", "[ModFeishuChat] Sending payload to Feishu: {}", body);

        if (!cli_)
        {
            LOG_ERROR("module", "[ModFeishuChat] HTTP client is not initialized");
            return false;
        }

        LOG_INFO("module", "[ModFeishuChat] POST {} -> {}", host_, path_);
        auto res = cli_->Post(path_, body, "application/json");
        if (!res)
        {
            LOG_ERROR("module", "[ModFeishuChat] Failed to send payload to Feishu: network error (host={})", host_);
            return false;
        }

        if (res->status >= 200 && res->status < 300)
        {
            LOG_INFO("module", "[ModFeishuChat] Feishu accepted payload (HTTP {})", res->status);
            return true;
        }

        LOG_ERROR("module", "[ModFeishuChat] Feishu returned HTTP {}: {}", res->status, res->body);
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
