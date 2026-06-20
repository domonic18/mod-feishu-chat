#ifndef _MOD_FEISHU_CHAT_MESSAGE_BUILDER_H_
#define _MOD_FEISHU_CHAT_MESSAGE_BUILDER_H_

#include "nlohmann/json.hpp"

#include "FeishuMessage.h"

namespace ModFeishuChat
{
    class FeishuMessageBuilder
    {
    public:
        static nlohmann::json Build(FeishuMessage const& msg,
                                    std::string const& format,
                                    std::string const& secret);

    private:
        static std::string FormatMessage(FeishuMessage const& msg, std::string const& format);
        static std::string ChatTypeToString(uint32 chatType);
    };
}

#endif // _MOD_FEISHU_CHAT_MESSAGE_BUILDER_H_
