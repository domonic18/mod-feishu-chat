#ifndef _MOD_FEISHU_CHAT_SIGNATURE_H_
#define _MOD_FEISHU_CHAT_SIGNATURE_H_

#include <string>

namespace ModFeishuChat
{
    class FeishuSignature
    {
    public:
        static void Compute(std::string const& secret,
                            std::string& outTimestamp,
                            std::string& outSign);
    };
}

#endif // _MOD_FEISHU_CHAT_SIGNATURE_H_
