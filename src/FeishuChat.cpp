#include "FeishuChat.h"

#include "Config.h"
#include "Log.h"

#include "FeishuMessage.h"
#include "FeishuMessageBatcher.h"
#include "FeishuMessageBuilder.h"
#include "FeishuWebhookClient.h"

namespace ModFeishuChat
{
    extern void AddFeishuChatPlayerScripts();
    extern void AddFeishuChatWorldScripts();

    FeishuChat::FeishuChat()
        : enabled_(false),
        forwardSay_(true),
        forwardYell_(true),
        forwardEmote_(true),
        forwardGuild_(true),
        forwardChannel_(true),
        webhookUrl_(""),
        secret_(""),
        messageFormat_("[{type}] {player} (Lv{level} {class}): {message}"),
        useInteractiveCard_(false),
        useBatching_(false),
        batchWindowMs_(1000),
        batchMaxSize_(20),
        timeoutSeconds_(5),
        maxQueueSize_(1000),
        client_(nullptr),
        batcher_(nullptr)
    {
        AddFeishuChatPlayerScripts();
        AddFeishuChatWorldScripts();
    }

    FeishuChat::~FeishuChat()
    {
        Stop();
    }

    FeishuChat& FeishuChat::Instance()
    {
        static FeishuChat instance;
        return instance;
    }

    bool FeishuChat::IsEnabled() const
    {
        return enabled_;
    }

    bool FeishuChat::ShouldForwardSay() const
    {
        return enabled_ && forwardSay_;
    }

    bool FeishuChat::ShouldForwardYell() const
    {
        return enabled_ && forwardYell_;
    }

    bool FeishuChat::ShouldForwardEmote() const
    {
        return enabled_ && forwardEmote_;
    }

    bool FeishuChat::ShouldForwardGuild() const
    {
        return enabled_ && forwardGuild_;
    }

    bool FeishuChat::ShouldForwardChannel() const
    {
        return enabled_ && forwardChannel_;
    }

    bool FeishuChat::IsChannelFiltered(std::string const& channelName) const
    {
        for (std::string const& filter : filteredChannels_)
        {
            if (channelName.find(filter) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    void FeishuChat::QueueChat(Player* player, uint32 chatType, std::string const& msg)
    {
        FeishuMessage* message = new FeishuMessage(player, chatType, msg);

        if (useBatching_ && batcher_)
        {
            LOG_INFO("module", "[ModFeishuChat] Queueing chat message (type={}) from '{}' to batcher", chatType, player ? player->GetName() : "?");
            batcher_->Enqueue(message);
            return;
        }

        if (client_)
        {
            LOG_INFO("module", "[ModFeishuChat] Sending chat message (type={}) from '{}'", chatType, player ? player->GetName() : "?");
            client_->Enqueue(FeishuMessageBuilder::BuildSingle(*message, messageFormat_, secret_, useInteractiveCard_));
        }

        delete message;
    }

    void FeishuChat::QueueChat(Player* player, uint32 chatType, std::string const& msg, Channel* channel)
    {
        FeishuMessage* message = new FeishuMessage(player, chatType, msg, channel);

        if (useBatching_ && batcher_)
        {
            LOG_INFO("module", "[ModFeishuChat] Queueing channel chat message (type={}) from '{}' to batcher", chatType, player ? player->GetName() : "?");
            batcher_->Enqueue(message);
            return;
        }

        if (client_)
        {
            LOG_INFO("module", "[ModFeishuChat] Sending channel chat message (type={}) from '{}'", chatType, player ? player->GetName() : "?");
            client_->Enqueue(FeishuMessageBuilder::BuildSingle(*message, messageFormat_, secret_, useInteractiveCard_));
        }

        delete message;
    }

    void FeishuChat::Start()
    {
        Stop();
        ReloadConfig();

        if (!enabled_)
        {
            return;
        }

        LOG_INFO("module", "[ModFeishuChat] Starting Feishu chat forwarding...");

        client_ = new FeishuWebhookClient(webhookUrl_, secret_, timeoutSeconds_, maxQueueSize_);
        client_->Start();

        if (useBatching_)
        {
            LOG_INFO("module", "[ModFeishuChat] Starting message batcher (window={}ms, max={})", batchWindowMs_, batchMaxSize_);
            batcher_ = new FeishuMessageBatcher(
                [this](std::vector<FeishuMessage*>&& messages) { FlushBatch(std::move(messages)); },
                batchWindowMs_,
                batchMaxSize_);
            batcher_->Start();
        }
    }

    void FeishuChat::Stop()
    {
        if (batcher_)
        {
            batcher_->Stop();
            delete batcher_;
            batcher_ = nullptr;
        }

        if (client_)
        {
            client_->Stop();
            delete client_;
            client_ = nullptr;
        }
    }

    void FeishuChat::Update()
    {
        // Worker and batcher threads handle sending; world thread can poll status here if needed.
    }

    void FeishuChat::ReloadConfig()
    {
        enabled_ = sConfigMgr->GetOption<bool>("FeishuChat.Enabled", false);
        forwardSay_ = sConfigMgr->GetOption<bool>("FeishuChat.ForwardSay", true);
        forwardYell_ = sConfigMgr->GetOption<bool>("FeishuChat.ForwardYell", true);
        forwardEmote_ = sConfigMgr->GetOption<bool>("FeishuChat.ForwardEmote", true);
        forwardGuild_ = sConfigMgr->GetOption<bool>("FeishuChat.ForwardGuild", true);
        forwardChannel_ = sConfigMgr->GetOption<bool>("FeishuChat.ForwardChannel", true);
        webhookUrl_ = sConfigMgr->GetOption<std::string>("FeishuChat.WebhookUrl", "");
        secret_ = sConfigMgr->GetOption<std::string>("FeishuChat.Secret", "");
        messageFormat_ = sConfigMgr->GetOption<std::string>("FeishuChat.MessageFormat", "[{type}] {player} (Lv{level} {class}): {message}");
        useInteractiveCard_ = sConfigMgr->GetOption<bool>("FeishuChat.UseInteractiveCard", false);
        useBatching_ = sConfigMgr->GetOption<bool>("FeishuChat.UseBatching", false);
        batchWindowMs_ = sConfigMgr->GetOption<int32>("FeishuChat.BatchWindowMs", 1000);
        batchMaxSize_ = static_cast<size_t>(sConfigMgr->GetOption<int32>("FeishuChat.BatchMaxSize", 20));
        timeoutSeconds_ = sConfigMgr->GetOption<int32>("FeishuChat.HttpTimeoutSeconds", 5);
        maxQueueSize_ = static_cast<size_t>(sConfigMgr->GetOption<int32>("FeishuChat.MaxQueueSize", 1000));

        filteredChannels_.clear();
        std::string filters = sConfigMgr->GetOption<std::string>("FeishuChat.FilteredChannels", "Crb,LFGForwarder,TCForwarder,LFGShout,xtensionxtooltip2,QuickHealMod");
        size_t start = 0;
        size_t end = filters.find(',');
        while (end != std::string::npos)
        {
            filteredChannels_.emplace_back(filters.substr(start, end - start));
            start = end + 1;
            end = filters.find(',', start);
        }
        if (start < filters.length())
        {
            filteredChannels_.emplace_back(filters.substr(start));
        }
    }

    void FeishuChat::FlushBatch(std::vector<FeishuMessage*>&& messages)
    {
        if (messages.empty() || !client_)
        {
            for (FeishuMessage* msg : messages)
            {
                delete msg;
            }
            messages.clear();
            return;
        }

        nlohmann::json payload = FeishuMessageBuilder::BuildBatch(messages, messageFormat_, secret_, useInteractiveCard_);
        if (!payload.is_null())
        {
            client_->Enqueue(std::move(payload));
        }

        for (FeishuMessage* msg : messages)
        {
            delete msg;
        }
        messages.clear();
    }
}
