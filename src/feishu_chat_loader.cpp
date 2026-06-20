#include "Log.h"

namespace ModFeishuChat
{
    extern void AddFeishuChatPlayerScripts();
    extern void AddFeishuChatWorldScripts();
}

void Addmod_feishu_chatScripts()
{
    LOG_INFO("module", "[ModFeishuChat] Initializing...");
    ModFeishuChat::AddFeishuChatPlayerScripts();
    ModFeishuChat::AddFeishuChatWorldScripts();
}
