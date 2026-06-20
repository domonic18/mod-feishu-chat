#include "FeishuChat.h"

#include "Channel.h"
#include "Guild.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"

namespace ModFeishuChat
{
    class FeishuChatPlayerScripts : public PlayerScript
    {
    public:
        FeishuChatPlayerScripts() : PlayerScript("ModFeishuChatPlayerScripts", {
            PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
            PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT,
            PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT
        })
        { }

        void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& /*lang*/, std::string& msg) override
        {
            FeishuChat& chat = FeishuChat::Instance();

            if (type == CHAT_MSG_SAY && chat.ShouldForwardSay())
            {
                LOG_INFO("module", "[ModFeishuChat] Captured SAY message from '{}': {}", player ? player->GetName() : "?", msg);
                chat.QueueChat(player, type, msg);
            }
            else if (type == CHAT_MSG_YELL && chat.ShouldForwardYell())
            {
                LOG_INFO("module", "[ModFeishuChat] Captured YELL message from '{}': {}", player ? player->GetName() : "?", msg);
                chat.QueueChat(player, type, msg);
            }
            else if (type == CHAT_MSG_EMOTE && chat.ShouldForwardEmote())
            {
                LOG_INFO("module", "[ModFeishuChat] Captured EMOTE message from '{}': {}", player ? player->GetName() : "?", msg);
                chat.QueueChat(player, type, msg);
            }
        }

        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Guild* /*guild*/) override
        {
            FeishuChat& chat = FeishuChat::Instance();

            if (type == CHAT_MSG_GUILD && chat.ShouldForwardGuild())
            {
                LOG_INFO("module", "[ModFeishuChat] Captured GUILD message from '{}': {}", player ? player->GetName() : "?", msg);
                chat.QueueChat(player, type, msg);
            }

            return true;
        }

        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Channel* channel) override
        {
            if (!channel)
            {
                return true;
            }

            FeishuChat& chat = FeishuChat::Instance();

            std::string const channelName = channel->GetName();
            if (chat.IsChannelFiltered(channelName))
            {
                LOG_DEBUG("module", "[ModFeishuChat] Ignoring filtered channel '{}' message from '{}'", channelName, player ? player->GetName() : "?");
                return true;
            }

            if (chat.ShouldForwardChannel())
            {
                LOG_INFO("module", "[ModFeishuChat] Captured CHANNEL '{}' message from '{}': {}", channelName, player ? player->GetName() : "?", msg);
                chat.QueueChat(player, type, msg, channel);
            }

            return true;
        }
    };

    void AddFeishuChatPlayerScripts()
    {
        new FeishuChatPlayerScripts();
    }
}
