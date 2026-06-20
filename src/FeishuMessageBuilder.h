#ifndef _MOD_FEISHU_CHAT_MESSAGE_BUILDER_H_
#define _MOD_FEISHU_CHAT_MESSAGE_BUILDER_H_

#include <vector>

#include "nlohmann/json.hpp"

#include "FeishuMessage.h"

namespace ModFeishuChat
{
    class FeishuMessageBuilder
    {
    public:
        static nlohmann::json BuildSingle(FeishuMessage const& msg,
                                          std::string const& format,
                                          std::string const& secret,
                                          bool useInteractive);

        static nlohmann::json BuildBatch(std::vector<FeishuMessage*> const& msgs,
                                         std::string const& format,
                                         std::string const& secret,
                                         bool useInteractive);

    private:
        static std::string FormatMessage(FeishuMessage const& msg, std::string const& format);
        static std::string ChatTypeToString(uint32 chatType);
        static std::string ChatTypeToColor(uint32 chatType);
        static std::string StripTypePrefix(std::string const& format);

        static nlohmann::json BuildTextPayload(std::string const& text, std::string const& secret);
        static nlohmann::json BuildInteractivePayload(std::vector<std::string> const& lines, std::string const& secret);
        static std::string WrapInteractiveLine(std::string const& text, uint32 chatType);
    };
}

#endif // _MOD_FEISHU_CHAT_MESSAGE_BUILDER_H_
