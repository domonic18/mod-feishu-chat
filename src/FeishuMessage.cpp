#include "FeishuMessage.h"

namespace ModFeishuChat
{
    FeishuMessage::FeishuMessage(Player* player, uint32 chatType, std::string const& msg)
        : playerName(player ? player->GetName() : ""),
        level(player ? player->GetLevel() : 0),
        classId(player ? player->getClass() : 0),
        raceId(player ? player->getRace() : 0),
        chatType(chatType),
        channelName(""),
        text(msg),
        timestamp(std::time(nullptr))
    {
        if (player)
        {
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetZoneId());
            zone = area ? area->area_name[LocaleConstant::LOCALE_enUS] : "";
        }
    }

    FeishuMessage::FeishuMessage(Player* player, uint32 chatType, std::string const& msg, Channel* channel)
        : FeishuMessage(player, chatType, msg)
    {
        if (channel)
        {
            channelName = channel->GetName();
        }
    }
}
