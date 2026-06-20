#include "FeishuMessageBuilder.h"

#include "FeishuSignature.h"
#include "SharedDefines.h"

namespace ModFeishuChat
{
    nlohmann::json FeishuMessageBuilder::Build(FeishuMessage const& msg,
                                               std::string const& format,
                                               std::string const& secret)
    {
        nlohmann::json payload;

        if (!secret.empty())
        {
            std::string timestamp;
            std::string sign;
            FeishuSignature::Compute(secret, timestamp, sign);
            payload["timestamp"] = timestamp;
            payload["sign"] = sign;
        }

        payload["msg_type"] = "text";
        payload["content"]["text"] = FormatMessage(msg, format);

        return payload;
    }

    std::string FeishuMessageBuilder::FormatMessage(FeishuMessage const& msg, std::string const& format)
    {
        std::string result = format;

        auto replace = [&result](std::string const& placeholder, std::string const& value)
        {
            std::string::size_type pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos)
            {
                result.replace(pos, placeholder.length(), value);
                pos += value.length();
            }
        };

        replace("{player}", msg.playerName);
        replace("{level}", std::to_string(msg.level));
        replace("{class}", std::to_string(msg.classId));
        replace("{race}", std::to_string(msg.raceId));
        replace("{zone}", msg.zone);
        replace("{type}", ChatTypeToString(msg.chatType));
        replace("{channel}", msg.channelName);
        replace("{message}", msg.text);

        return result;
    }

    std::string FeishuMessageBuilder::ChatTypeToString(uint32 chatType)
    {
        switch (chatType)
        {
            case CHAT_MSG_SAY:
                return "SAY";
            case CHAT_MSG_YELL:
                return "YELL";
            case CHAT_MSG_EMOTE:
                return "EMOTE";
            case CHAT_MSG_GUILD:
                return "GUILD";
            case CHAT_MSG_CHANNEL:
                return "CHANNEL";
            default:
                return "UNKNOWN";
        }
    }
}
