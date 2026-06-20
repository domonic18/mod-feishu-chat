# mod-feishu-chat 开发实现文档

## 1. 总体架构

`mod-feishu-chat` 采用**单例管理器 + 独立工作线程 + 线程安全队列**的架构，与 `mod-chat-transmitter` 类似，但将传输层从 WebSocket + 外部 bot 服务简化为直接 HTTPS POST 到飞书 webhook。

```
┌──────────────┐      PlayerScript hooks        ┌─────────────┐
│ 游戏内聊天事件 │ ──────── enqueue message ───▶ │ FeishuChat  │
└──────────────┘                                 │  (singleton) │
                                                  └──────┬──────┘
                                                         │
                                    ProducerConsumerQueue│
                                                         ▼
                                              ┌─────────────────────┐
                                              │ FeishuWebhookClient │
                                              │   (worker thread)   │
                                              └──────────┬──────────┘
                                                         │ HTTPS POST
                                                         ▼
                                              ┌─────────────────────┐
                                              │ 飞书自定义机器人 webhook │
                                              └─────────────────────┘
```

## 2. 模块目录结构

```
modules/mod-feishu-chat/
├── README.md
├── mod-feishu-chat.cmake
├── conf/
│   └── FeishuChat.conf.dist
├── docs/
│   ├── requirements.md
│   └── development.md
├── libs/
│   ├── nlohmann/json.hpp      # JSON 序列化
│   └── httplib.h              # 单头 HTTPS 客户端
└── src/
    ├── feishu_chat_loader.cpp # 模块入口
    ├── FeishuChat.h/.cpp      # 单例管理器
    ├── FeishuWebhookClient.h/.cpp  # 工作线程/HTTPS 客户端
    ├── FeishuSignature.h/.cpp # HMAC-SHA256 + Base64 签名
    ├── FeishuMessage.h        # 消息数据结构与构造
    ├── FeishuMessageBuilder.h/.cpp  # 组装飞书 JSON payload
    ├── PlayerScripts.cpp      # 聊天捕获 PlayerScript
    └── WorldScripts.cpp       # 生命周期 WorldScript
```

## 3. 核心类设计

### 3.1 `FeishuMessage`

数据载体，从 `Player*` 构造。

```cpp
struct FeishuMessage
{
    std::string playerName;
    uint8       level;
    uint8       classId;
    uint8       raceId;
    std::string zone;
    uint32      chatType;
    std::string channelName;
    std::string text;
    uint64_t    timestamp;
};
```

### 3.2 `FeishuSignature`

静态工具类，计算飞书签名。

```cpp
class FeishuSignature
{
public:
    static void Compute(std::string const& secret,
                        std::string& outTimestamp,
                        std::string& outSign);
};
```

实现要点：

- 时间戳为秒级 Unix 时间，字符串类型。
- 使用 `Acore::Crypto::HMAC_SHA256::GetDigestOf(...)` 计算摘要。
- `HMAC_SHA256` 返回 `std::array<uint8, 32>`，需转换为 `std::vector<uint8>` 后调用 `Acore::Encoding::Base64::Encode`。

### 3.3 `FeishuMessageBuilder`

将 `FeishuMessage` 转换为 `nlohmann::json`。

```cpp
class FeishuMessageBuilder
{
public:
    static nlohmann::json BuildText(FeishuMessage const& msg);
};
```

`BuildText` 使用配置模板替换占位符后生成纯文本 JSON：

```json
{
  "msg_type": "text",
  "content": { "text": "..." }
}
```

若配置了 `Secret`，则在 JSON 顶层添加 `timestamp` 和 `sign`。

### 3.4 `FeishuWebhookClient`

运行在工作线程中的 HTTPS 客户端。

```cpp
class FeishuWebhookClient
{
public:
    void Start();
    void Stop();
    void Enqueue(FeishuMessage* msg);
    void PollErrors(); // 由 world 线程调用，打印失败日志

private:
    void WorkerThread();
    bool Send(FeishuMessage const& msg);

    std::thread worker;
    ProducerConsumerQueue<FeishuMessage*> queue;
    std::atomic<bool> running;
    std::string webhookUrl;
    std::string secret;
    int timeoutSeconds;
};
```

实现要点：

- 工作线程循环从队列弹出消息。
- 使用 `httplib::Client cli(host)` 或 `httplib::SSLClient`，设置超时。
- 解析 `webhookUrl` 得到 host 与 path（`httplib::Client` 可接受完整 URL）。
- 调用 `cli.Post(path, body, "application/json")`。
- 根据返回状态码处理：2xx 成功；429 进行指数退避重试；4xx/5xx 记录错误并丢弃。

### 3.5 `FeishuChat`

单例管理器，负责配置读取与生命周期。

```cpp
class FeishuChat
{
public:
    static FeishuChat& Instance();

    bool IsEnabled() const;
    bool ShouldForwardSay() const;
    bool ShouldForwardYell() const;
    bool ShouldForwardEmote() const;
    bool ShouldForwardGuild() const;
    bool ShouldForwardChannel() const;
    bool IsChannelFiltered(std::string const& channelName) const;

    void QueueChat(Player* player, uint32 chatType, std::string const& msg);
    void QueueChat(Player* player, uint32 chatType, std::string const& msg, Channel* channel);

    void Start();
    void Stop();
    void Update();

private:
    FeishuChat();
    FeishuWebhookClient* client;
};
```

## 4. 生命周期与 Hook 注册

### 4.1 `WorldScripts.cpp`

```cpp
class FeishuChatWorldScripts : public WorldScript
{
public:
    FeishuChatWorldScripts() : WorldScript("ModFeishuChatWorldScripts", {
        WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_SHUTDOWN,
        WORLDHOOK_ON_UPDATE,
        WORLDHOOK_ON_AFTER_CONFIG_LOAD
    }) {}

    void OnStartup() override { FeishuChat::Instance().Start(); }
    void OnShutdown() override { FeishuChat::Instance().Stop(); }
    void OnUpdate(uint32) override { FeishuChat::Instance().Update(); }
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
        {
            FeishuChat::Instance().Stop();
            FeishuChat::Instance().Start();
        }
    }
};
```

### 4.2 `PlayerScripts.cpp`

与 `mod-chat-transmitter/src/PlayerScripts.cpp` 逻辑一致，但改为入队到 `FeishuChat`。

```cpp
bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg) override
{
    if (type == CHAT_MSG_SAY && FeishuChat::Instance().ShouldForwardSay())
        FeishuChat::Instance().QueueChat(player, type, msg);
    else if (type == CHAT_MSG_YELL && FeishuChat::Instance().ShouldForwardYell())
        FeishuChat::Instance().QueueChat(player, type, msg);
    else if (type == CHAT_MSG_EMOTE && FeishuChat::Instance().ShouldForwardEmote())
        FeishuChat::Instance().QueueChat(player, type, msg);
    else if (type == CHAT_MSG_GUILD && FeishuChat::Instance().ShouldForwardGuild())
        FeishuChat::Instance().QueueChat(player, type, msg);
    return true;
}

bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Channel* channel) override
{
    if (FeishuChat::Instance().IsChannelFiltered(channel->GetName()))
        return true;

    if (FeishuChat::Instance().ShouldForwardChannel())
        FeishuChat::Instance().QueueChat(player, type, msg, channel);

    return true;
}
```

## 5. 配置说明

详见 `conf/FeishuChat.conf.dist`。关键配置项：

| 配置项 | 说明 |
|---|---|
| `FeishuChat.Enabled` | 总开关 |
| `FeishuChat.WebhookUrl` | 飞书自定义机器人完整 webhook URL |
| `FeishuChat.Secret` | 签名校验密钥，为空则不签名 |
| `FeishuChat.ForwardSay/Yell/Emote/Guild/Channel` | 各聊天类型开关 |
| `FeishuChat.FilteredChannels` | 逗号分隔的过滤频道子串 |
| `FeishuChat.MessageFormat` | 文本消息模板 |
| `FeishuChat.HttpTimeoutSeconds` | HTTPS 超时 |
| `FeishuChat.MaxQueueSize` | 队列上限 |

## 6. 构建与集成

### 6.1 CMake

`mod-feishu-chat.cmake`：

```cmake
target_include_directories(modules PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/libs"
)

target_compile_definitions(modules PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)
```

说明：

- `modules` 目标已经链接 `game-interface`，间接获得 `common` 的 OpenSSL 链接。
- `CPPHTTPLIB_OPENSSL_SUPPORT` 启用 `httplib.h` 的 HTTPS 支持。
- 不需要额外 `target_link_libraries`。

### 6.2 编译选项

在 CMake 配置时保持默认模块构建：

```bash
cmake .. -DMODULES=static -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### 6.3 运行时配置

将 `conf/FeishuChat.conf.dist` 复制为 `conf/FeishuChat.conf` 并填入 webhook URL 与 secret。

## 7. 飞书 API 说明

### 7.1 自定义机器人 webhook

- 方法：`POST`
- URL：`https://open.feishu.cn/open-apis/bot/v2/hook/{hook_key}`
- Content-Type：`application/json`

### 7.2 文本消息示例

```json
{
  "timestamp": "1715000000",
  "sign": "base64_encoded_signature",
  "msg_type": "text",
  "content": {
    "text": "[SAY] PlayerOne (Lv80 Warrior): Hello world!"
  }
}
```

### 7.3 签名算法

```text
stringToSign = timestamp + "\n" + secret
signature = base64(hmac-sha256(stringToSign))
```

实现位于 `FeishuSignature.cpp`。

## 8. 线程模型

- **world 线程**：PlayerScript 钩子中构造 `FeishuMessage` 并入队；WorldScript `OnUpdate` 中轮询错误日志。
- **worker 线程**：从队列取消息，构建 JSON，执行 HTTPS POST，记录结果。
- `ProducerConsumerQueue` 提供线程安全的 SPSC 队列（`src/common/Utilities/ProducerConsumerQueue.h`）。

## 9. 风险与应对

| 风险 | 应对 |
|---|---|
| 网络异常导致 worldserver 卡顿 | 所有网络操作限制在 worker 线程；设置 HTTP 超时。 |
| 消息队列无限增长 | 设置 `MaxQueueSize`，超出时丢弃并告警。 |
| 飞书 429 限流 | worker 线程指数退避重试 3 次后丢弃。 |
| webhook URL / secret 泄露 | 日志中只打印域名，不打印完整 URL 与 secret。 |
| 编译依赖问题 | `httplib.h` 与 `nlohmann/json.hpp` 均随模块提供，不依赖外部包。 |
| 玩家隐私 | 不转发账号名、IP、密语/队伍/团队内容；公会频道根据配置可转发。 |

## 10. 可选需求：飞书反向密语架构

### 10.1 需求

管理员在飞书群中发送命令消息，例如：

```
/whisper PlayerOne 请不要在公共频道发广告
```

worldserver 解析该命令后，以 GM 身份向游戏内玩家 `PlayerOne` 发送密语。

### 10.2 架构变化

当前 MVP 为**单向推送**：worldserver → Feishu webhook。
反向密语需要**双向通信**：Feishu 事件 → worldserver。

```
┌──────────────┐   群聊命令    ┌─────────────────┐
│   飞书群聊    │ ───────────▶ │ Feishu bot 事件  │
└──────────────┘              │   subscription  │
                              └────────┬────────┘
                                       │ callback POST
                                       ▼
                              ┌─────────────────┐
                              │ worldserver HTTP │
                              │    endpoint     │
                              └────────┬────────┘
                                       │ parse & send whisper
                                       ▼
                              ┌─────────────────┐
                              │   游戏内玩家     │
                              └─────────────────┘
```

### 10.3 实现要点

- **事件订阅**：需要在飞书开放平台配置机器人的事件订阅地址，且该地址必须公网可访问。
- **鉴权**：仅允许配置的管理员 open_id 发送命令；可配置 `FeishuChat.AdminOpenIds` 列表。
- **HTTP 服务**：在 worldserver 中启动一个独立的 HTTP listener（如使用 `httplib::Server`），处理 `/feishu/callback`。
- **安全性**：回调地址必须验证 Feishu 请求签名（与发送签名算法不同，事件订阅使用 `x-lark-signature`）。
- **命令解析**：识别 `/whisper <player> <message>`，查找在线玩家并发送密语。

### 10.4 为什么不纳入 MVP

- 需要公网入口和 TLS 证书，部署复杂度远高于单向 webhook。
- 飞书事件订阅与自定义机器人 webhook 是两套不同的机制。
- MVP 的目标是替代 `mod-chat-transmitter` 的 Discord 推送功能，反向通信可作为后续增强。

## 11. 后续优化方向

1. **关键词告警**：在模块中配置敏感词列表，命中时单独 @ 管理员。
2. **命令反向通道（飞书→游戏密语）**：通过 Feishu bot 事件订阅接收群聊命令，解析后由 worldserver 向指定玩家发送密语。需要公网可访问的回调地址与严格鉴权，复杂度较高，作为可选需求。
3. **消息合并**：短时间内多条消息合并为一条飞书消息，降低请求频率。
4. **多 webhook 支持**：支持按聊天类型路由到不同飞书群。

## 12. 参考文件

- `modules/mod-chat-transmitter/src/PlayerScripts.cpp`
- `modules/mod-chat-transmitter/src/WorldScripts.cpp`
- `modules/mod-chat-transmitter/src/ChatTransmitter.h`
- `src/common/Cryptography/HMAC.h`
- `src/common/Encoding/Base64.h`
- `src/common/Utilities/ProducerConsumerQueue.h`
