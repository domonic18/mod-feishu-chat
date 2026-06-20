#include "FeishuChat.h"

#include "ScriptMgr.h"

namespace ModFeishuChat
{
    class FeishuChatWorldScripts : public WorldScript
    {
    public:
        FeishuChatWorldScripts() : WorldScript("ModFeishuChatWorldScripts", {
            WORLDHOOK_ON_STARTUP,
            WORLDHOOK_ON_SHUTDOWN,
            WORLDHOOK_ON_UPDATE,
            WORLDHOOK_ON_AFTER_CONFIG_LOAD
        })
        { }

        void OnStartup() override
        {
            FeishuChat::Instance().Start();
        }

        void OnShutdown() override
        {
            FeishuChat::Instance().Stop();
        }

        void OnUpdate(uint32 /*diff*/) override
        {
            FeishuChat::Instance().Update();
        }

        void OnAfterConfigLoad(bool reload) override
        {
            if (!reload)
            {
                return;
            }

            FeishuChat::Instance().Stop();
            FeishuChat::Instance().Start();
        }
    };

    void AddFeishuChatWorldScripts()
    {
        new FeishuChatWorldScripts();
    }
}
