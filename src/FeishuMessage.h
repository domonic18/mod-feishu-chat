#ifndef _MOD_FEISHU_CHAT_MESSAGE_H_
#define _MOD_FEISHU_CHAT_MESSAGE_H_

#include <ctime>
#include <string>

#include "Channel.h"
#include "Player.h"

namespace ModFeishuChat
{
    struct FeishuMessage
    {
        FeishuMessage(Player* player, uint32 chatType, std::string const& msg);
        FeishuMessage(Player* player, uint32 chatType, std::string const& msg, Channel* channel);

        std::string playerName;
        uint8       level;
        uint8       classId;
        uint8       raceId;
        std::string zone;
        uint32      chatType;
        std::string channelName;
        std::string text;
        std::time_t timestamp;
    };
}

#endif // _MOD_FEISHU_CHAT_MESSAGE_H_
