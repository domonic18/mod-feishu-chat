#include "FeishuChat.h"

#include "ScriptMgr.h"
#include "SharedDefines.h"

namespace ModFeishuChat
{
    class FeishuChatPlayerScripts : public PlayerScript
    {
    public:
        FeishuChatPlayerScripts() : PlayerScript("ModFeishuChatPlayerScripts", {
            PLAYERHOOK_CAN_PLAYER_USE_CHAT,
            PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT
        })
        { }

        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg) override
        {
            FeishuChat& chat = FeishuChat::Instance();

            if (type == CHAT_MSG_SAY && chat.ShouldForwardSay())
                chat.QueueChat(player, type, msg);
            else if (type == CHAT_MSG_YELL && chat.ShouldForwardYell())
                chat.QueueChat(player, type, msg);
            else if (type == CHAT_MSG_EMOTE && chat.ShouldForwardEmote())
                chat.QueueChat(player, type, msg);
            else if (type == CHAT_MSG_GUILD && chat.ShouldForwardGuild())
                chat.QueueChat(player, type, msg);

            return true;
        }

        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Channel* channel) override
        {
            if (!channel)
            {
                return true;
            }

            FeishuChat& chat = FeishuChat::Instance();

            if (chat.IsChannelFiltered(channel->GetName()))
                return true;

            if (chat.ShouldForwardChannel())
                chat.QueueChat(player, type, msg, channel);

            return true;
        }
    };

    void AddFeishuChatPlayerScripts()
    {
        new FeishuChatPlayerScripts();
    }
}
