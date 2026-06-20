#include "FeishuMessageBuilder.h"

#include "FeishuSignature.h"
#include "SharedDefines.h"

namespace ModFeishuChat
{
    nlohmann::json FeishuMessageBuilder::BuildSingle(FeishuMessage const& msg,
                                                     std::string const& format,
                                                     std::string const& secret,
                                                     bool useInteractive)
    {
        std::string text = FormatMessage(msg, useInteractive ? StripTypePrefix(format) : format);

        if (useInteractive)
        {
            return BuildInteractivePayload({ WrapInteractiveLine(text, msg.chatType) }, secret);
        }

        return BuildTextPayload(text, secret);
    }

    nlohmann::json FeishuMessageBuilder::BuildBatch(std::vector<FeishuMessage*> const& msgs,
                                                    std::string const& format,
                                                    std::string const& secret,
                                                    bool useInteractive)
    {
        if (msgs.empty())
        {
            return {};
        }

        std::string const& effectiveFormat = useInteractive ? StripTypePrefix(format) : format;

        if (useInteractive)
        {
            std::vector<std::string> lines;
            lines.reserve(msgs.size());
            for (FeishuMessage const* msg : msgs)
            {
                if (msg)
                {
                    lines.emplace_back(WrapInteractiveLine(FormatMessage(*msg, effectiveFormat), msg->chatType));
                }
            }
            return BuildInteractivePayload(lines, secret);
        }

        std::string text;
        for (FeishuMessage const* msg : msgs)
        {
            if (msg)
            {
                if (!text.empty())
                {
                    text += "\n";
                }
                text += FormatMessage(*msg, effectiveFormat);
            }
        }
        return BuildTextPayload(text, secret);
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

    std::string FeishuMessageBuilder::ChatTypeToColor(uint32 chatType)
    {
        switch (chatType)
        {
            case CHAT_MSG_YELL:
                return "red";
            case CHAT_MSG_EMOTE:
                return "orange";
            case CHAT_MSG_GUILD:
                return "green";
            case CHAT_MSG_CHANNEL:
                return "indigo";
            case CHAT_MSG_WHISPER:
                return "purple";
            case CHAT_MSG_PARTY:
                return "blue";
            case CHAT_MSG_RAID:
                return "orange";
            default:
                return "";
        }
    }

    std::string FeishuMessageBuilder::StripTypePrefix(std::string const& format)
    {
        std::string result = format;
        std::string::size_type pos = result.find("[{type}]");
        if (pos != std::string::npos)
        {
            result.erase(pos, 8);
            while (pos < result.length() && std::isspace(static_cast<unsigned char>(result[pos])))
            {
                result.erase(pos, 1);
            }
        }
        return result;
    }

    nlohmann::json FeishuMessageBuilder::BuildTextPayload(std::string const& text, std::string const& secret)
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
        payload["content"]["text"] = text;

        return payload;
    }

    nlohmann::json FeishuMessageBuilder::BuildInteractivePayload(std::vector<std::string> const& lines, std::string const& secret)
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

        payload["msg_type"] = "interactive";
        payload["card"]["config"]["wide_screen_mode"] = true;
        payload["card"]["elements"] = nlohmann::json::array();

        for (std::string const& line : lines)
        {
            nlohmann::json element;
            element["tag"] = "div";
            element["text"]["tag"] = "lark_md";
            element["text"]["content"] = line;
            payload["card"]["elements"].push_back(element);
        }

        return payload;
    }

    std::string FeishuMessageBuilder::WrapInteractiveLine(std::string const& text, uint32 chatType)
    {
        std::string color = ChatTypeToColor(chatType);
        if (color.empty())
        {
            return text;
        }

        return "<font color='" + color + "'>" + text + "</font>";
    }
}
