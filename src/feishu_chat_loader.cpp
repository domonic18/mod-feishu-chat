#include "Log.h"

#include "FeishuChat.h"

void Addmod_feishu_chatScripts()
{
    LOG_INFO("module", "[ModFeishuChat] Initializing...");
    ModFeishuChat::FeishuChat::Instance();
}
