#ifndef _MOD_FEISHU_CHAT_H_
#define _MOD_FEISHU_CHAT_H_

#include <string>
#include <vector>

#include "Player.h"

namespace ModFeishuChat
{
    class FeishuMessage;
    class FeishuMessageBatcher;
    class FeishuWebhookClient;

    class FeishuChat
    {
    public:
        static FeishuChat& Instance();

        bool IsEnabled() const;
        bool ShouldForwardSay() const;
        bool ShouldForwardYell() const;
        bool ShouldForwardEmote() const;
        bool ShouldForwardGuild() const;
        bool ShouldForwardChannel() const;
        bool IsChannelFiltered(std::string const& channelName) const;

        void QueueChat(Player* player, uint32 chatType, std::string const& msg);
        void QueueChat(Player* player, uint32 chatType, std::string const& msg, Channel* channel);

        void Start();
        void Stop();
        void Update();

    private:
        FeishuChat();
        ~FeishuChat();
        FeishuChat(FeishuChat const& other) = delete;
        FeishuChat& operator=(FeishuChat const& other) = delete;

        void ReloadConfig();
        void FlushBatch(std::vector<FeishuMessage*>&& messages);

        bool enabled_;
        bool forwardSay_;
        bool forwardYell_;
        bool forwardEmote_;
        bool forwardGuild_;
        bool forwardChannel_;
        std::string webhookUrl_;
        std::string secret_;
        std::string messageFormat_;
        bool useInteractiveCard_;
        bool useBatching_;
        int batchWindowMs_;
        size_t batchMaxSize_;
        int timeoutSeconds_;
        size_t maxQueueSize_;
        std::vector<std::string> filteredChannels_;

        FeishuWebhookClient* client_;
        FeishuMessageBatcher* batcher_;
    };
}

#endif // _MOD_FEISHU_CHAT_H_
