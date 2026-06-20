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
                                              │ FeishuMessageBatcher│
                                              │  (batch + flush)    │
                                              └──────────┬──────────┘
                                                         │
                                                         ▼
                                              ┌─────────────────────┐
                                              │ FeishuWebhookClient │
                                              │   (worker thread)   │
                                              │  keep-alive client  │
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
    ├── FeishuMessageBatcher.h/.cpp # 消息批量聚合与定时刷新
    ├── FeishuSignature.h/.cpp # HMAC-SHA256 + Base64 签名
    ├── FeishuMessage.h        # 消息数据结构与构造
    ├── FeishuMessageBuilder.h/.cpp  # 组装飞书 JSON payload（text/interactive）
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
    std::time_t timestamp;
};
```

### 3.2 `FeishuSignature`

静态工具类，计算飞书 webhook 自定义机器人签名。

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
- 使用 `Acore::Crypto::HMAC_SHA256` 计算摘要。
- `HMAC_SHA256` 返回 `std::array<uint8, 32>`，需转换为 `std::vector<uint8>` 后调用 `Acore::Encoding::Base64::Encode`。

### 3.3 `FeishuMessageBuilder`

将 `FeishuMessage` 转换为 `nlohmann::json`。

```cpp
class FeishuMessageBuilder
{
public:
    static nlohmann::json BuildText(FeishuMessage const& msg);
    static nlohmann::json BuildInteractive(FeishuMessage const& msg);
    static nlohmann::json BuildInteractiveBatch(std::vector<FeishuMessage> const& msgs);

private:
    static std::string FormatMessage(FeishuMessage const& msg, std::string const& format);
    static std::string ChatTypeToString(uint32 chatType);
    static std::string ChatTypeToColor(uint32 chatType);
};
```

#### 3.3.1 纯文本消息

`BuildText` 使用配置模板替换占位符后生成纯文本 JSON：

```json
{
  "msg_type": "text",
  "content": { "text": "..." }
}
```

若配置了 `Secret`，则在 JSON 顶层添加 `timestamp` 和 `sign`。

#### 3.3.2 带颜色卡片消息（推荐）

飞书 `text` 类型不支持字体颜色；`interactive` 消息卡片支持 `lark_md` 与 `<font color='...'>` 标签。因此为保持魔兽世界聊天风格，使用 `interactive` 类型：

```json
{
  "msg_type": "interactive",
  "card": {
    "config": { "wide_screen_mode": true },
    "elements": [
      {
        "tag": "div",
        "text": {
          "tag": "lark_md",
          "content": "[SAY] Player (Lv80 Warrior): Hello"
        }
      }
    ]
  }
}
```

颜色映射（与魔兽世界默认聊天颜色保持一致）：

| 聊天类型 | 飞书颜色 | 说明 |
|---|---|---|
| SAY | 默认（黑色） | 普通白字，不加 `<font>` |
| YELL | `red` | 大喊 |
| EMOTE | `#FF8C00` / 橙色 | 表情 |
| GUILD | `green` | 公会 |
| CHANNEL | `#8B4513` / 棕色 | 世界/综合频道 |
| WHISPER | `#FF69B4` / 粉色 | 密语（暂不转发） |

示例：

```json
{
  "tag": "lark_md",
  "content": "<font color='green'>Player (Lv80 Warrior): 大家好</font>"
}
```

#### 3.3.3 批量卡片消息

当启用批量发送时，将多条消息合并为一条 `interactive` 卡片，每条消息作为一个 `div` 元素，或拼接为多行 `lark_md`：

```json
{
  "msg_type": "interactive",
  "card": {
    "config": { "wide_screen_mode": true },
    "elements": [
      { "tag": "div", "text": { "tag": "lark_md", "content": "<font color='green'>A: hi</font>" } },
      { "tag": "div", "text": { "tag": "lark_md", "content": "B: hello</font>" } }
    ]
  }
}
```

### 3.4 `FeishuMessageBatcher`

解决“每产生一条消息就发起一次 HTTP 请求”的问题。通过小窗口聚合消息，降低请求频率。

```cpp
class FeishuMessageBatcher
{
public:
    void Start();
    void Stop();
    void Enqueue(FeishuMessage* msg);

private:
    void Flush();
    void WorkerThread();

    std::mutex mutex_;
    std::vector<std::unique_ptr<FeishuMessage>> buffer_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_;

    int batchWindowMs_;
    size_t batchMaxSize_;
    std::chrono::steady_clock::time_point nextFlush_;
};
```

行为：

- 新消息入队时加入 `buffer_`。
- 后台线程循环等待，直到满足以下任一条件：
  - 距离上次刷新达到 `BatchWindowMs`；
  - `buffer_` 大小达到 `BatchMaxSize`；
  - 模块停止时强制刷新。
- 刷新时将 `buffer_` 中所有消息移出，构建批量 `interactive` 卡片，交给 `FeishuWebhookClient` 一次发送。

默认建议：

- `BatchWindowMs = 1000`（1 秒内的聊天合并为一条消息）。
- `BatchMaxSize = 20`（避免单条消息过长）。

如果批量开启后 buffer 为空，则不发送。

### 3.5 `FeishuWebhookClient`

运行在工作线程中的 HTTPS 客户端。

```cpp
class FeishuWebhookClient
{
public:
    void Start();
    void Stop();
    void Enqueue(FeishuMessage* msg);
    void EnqueueBatch(std::vector<std::unique_ptr<FeishuMessage>> msgs);
    void PollErrors(); // 由 world 线程调用，打印失败日志

private:
    void WorkerThread();
    bool Send(nlohmann::json const& payload);

    std::thread worker;
    ProducerConsumerQueue<nlohmann::json> queue; // 队列元素改为 JSON payload
    std::atomic<bool> running;
    std::string webhookUrl;
    std::string secret;
    int timeoutSeconds;
    httplib::Client cli; // 复用 client，启用 keep-alive
};
```

实现要点：

- 工作线程循环从队列弹出**已构建好的 JSON payload**。
- 使用 `httplib::Client cli(host)` 初始化一次，设置 `cli.set_keep_alive(true)`，复用 TCP/TLS 连接。
- 解析 `webhookUrl` 得到 host 与 path（`httplib::Client` 可接受完整 URL）。
- 调用 `cli.Post(path, body, "application/json")`。
- 根据返回状态码处理：2xx 成功；429 进行指数退避重试；4xx/5xx 记录错误并丢弃。

### 3.6 `FeishuChat`

单例管理器，负责配置读取与生命周期。

```cpp
class FeishuChat
{
public:
    static FeishuChat& Instance();

    bool IsEnabled() const;
    bool ShouldForwardSay() const;
    // ...

    void QueueChat(Player* player, uint32 chatType, std::string const& msg);
    void QueueChat(Player* player, uint32 chatType, std::string const& msg, Channel* channel);

    void Start();
    void Stop();
    void Update();

private:
    FeishuChat();
    FeishuWebhookClient* client;
    FeishuMessageBatcher* batcher;

    bool useBatching_;
    bool useInteractive_;
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
void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& /*lang*/, std::string& msg) override
{
    if (type == CHAT_MSG_SAY && FeishuChat::Instance().ShouldForwardSay())
        FeishuChat::Instance().QueueChat(player, type, msg);
    else if (type == CHAT_MSG_YELL && FeishuChat::Instance().ShouldForwardYell())
        FeishuChat::Instance().QueueChat(player, type, msg);
    else if (type == CHAT_MSG_EMOTE && FeishuChat::Instance().ShouldForwardEmote())
        FeishuChat::Instance().QueueChat(player, type, msg);
}

bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Guild* /*guild*/) override
{
    if (type == CHAT_MSG_GUILD && FeishuChat::Instance().ShouldForwardGuild())
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
| `FeishuChat.UseInteractiveCard` | 是否使用带颜色的 interactive 卡片（0=text, 1=interactive） |
| `FeishuChat.UseBatching` | 是否启用批量发送 |
| `FeishuChat.BatchWindowMs` | 批量聚合窗口（毫秒） |
| `FeishuChat.BatchMaxSize` | 单次批量最大消息数 |
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

### 7.3 带颜色卡片消息示例

由于颜色已经标识聊天类型，interactive 卡片会自动移除消息模板中的 `[{type}]` 前缀。

```json
{
  "msg_type": "interactive",
  "card": {
    "config": { "wide_screen_mode": true },
    "elements": [
      {
        "tag": "div",
        "text": {
          "tag": "lark_md",
          "content": "<font color='green'>PlayerOne (Lv80 Warrior): 大家好</font>"
        }
      }
    ]
  }
}
```

### 7.4 批量卡片消息示例

```json
{
  "msg_type": "interactive",
  "card": {
    "config": { "wide_screen_mode": true },
    "elements": [
      {
        "tag": "div",
        "text": { "tag": "lark_md", "content": "<font color='green'>A: hi</font>" }
      },
      {
        "tag": "div",
        "text": { "tag": "lark_md", "content": "B: hello" }
      },
      {
        "tag": "div",
        "text": { "tag": "lark_md", "content": "<font color='#8B4513'>C: LFG</font>" }
      }
    ]
  }
}
```

### 7.5 签名算法

```text
stringToSign = timestamp + "\n" + secret
signature = base64(hmac-sha256(stringToSign))
```

实现位于 `FeishuSignature.cpp`。

## 8. 线程模型

- **world 线程**：PlayerScript 钩子中构造 `FeishuMessage` 并入队；WorldScript `OnUpdate` 中轮询错误日志。
- **batcher 线程**（可选）：聚合小窗口内的消息，触发刷新。
- **worker 线程**：从队列取 JSON payload，执行 HTTPS POST，记录结果。
- `ProducerConsumerQueue` 提供线程安全的 SPSC 队列（`src/common/Utilities/ProducerConsumerQueue.h`）。

## 9. 批量发送设计理由

当前实现中，每条聊天消息都会触发一次 HTTP 请求，在高峰时段可能导致：

1. 网络连接频繁建立/关闭，增加延迟与 CPU 消耗。
2. 飞书 webhook 限流（默认单机器人 20 次/秒）。
3. 飞书群内消息刷屏，影响管理员阅读。

**最佳实践**：采用**时间窗口 + 数量上限**的批量策略。

- 默认 1 秒窗口内，最多聚合 20 条消息。
- 达到上限立即发送，避免延迟过大。
- 复用 `httplib::Client` 并保持长连接，减少 TLS 握手。
- 批量卡片中每条聊天作为独立 `div`，颜色与格式不变。

## 10. 颜色设计理由

魔兽世界默认聊天颜色规则：

| 类型 | 颜色 |
|---|---|
| SAY | 白色/默认 |
| YELL | 红色 |
| EMOTE | 橙色 |
| GUILD | 绿色 |
| PARTY | 蓝色 |
| RAID | 橙色 |
| CHANNEL | 棕色/土黄 |
| WHISPER | 粉色 |

使用 `interactive` 卡片的 `lark_md` + `<font color='...'>` 可还原上述风格，提升管理员阅读体验。`text` 类型无法满足颜色需求，因此增加 `UseInteractiveCard` 开关：

- `UseInteractiveCard = 1`：使用带颜色卡片（推荐）。
- `UseInteractiveCard = 0`：使用纯文本，兼容不需要颜色的场景。

## 11. 风险与应对

| 风险 | 应对 |
|---|---|
| 网络异常导致 worldserver 卡顿 | 所有网络操作限制在 worker 线程；设置 HTTP 超时。 |
| 消息队列无限增长 | 设置 `MaxQueueSize`，超出时丢弃并告警。 |
| 飞书 429 限流 | 批量发送降低 QPS；worker 线程指数退避重试 3 次后丢弃。 |
| webhook URL / secret 泄露 | 日志中只打印域名，不打印完整 URL 与 secret。 |
| 编译依赖问题 | `httplib.h` 与 `nlohmann/json.hpp` 均随模块提供，不依赖外部包。 |
| 玩家隐私 | 不转发账号名、IP、密语/队伍/团队内容；公会频道根据配置可转发。 |
| 批量延迟过大 | 窗口默认 1 秒，上限 20 条，超过立即 flush。 |
| interactive 卡片格式错误 | 使用 `lark_md` 标准语法，开发阶段用 curl 验证。 |

## 12. 可选需求：飞书反向密语架构

### 12.1 需求

管理员在飞书群中发送命令消息，例如：

```
/whisper PlayerOne 请不要在公共频道发广告
```

worldserver 解析该命令后，以 GM 身份向游戏内玩家 `PlayerOne` 发送密语。

### 12.2 架构变化

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

### 12.3 实现要点

- **事件订阅**：需要在飞书开放平台配置机器人的事件订阅地址，且该地址必须公网可访问。
- **鉴权**：仅允许配置的管理员 open_id 发送命令；可配置 `FeishuChat.AdminOpenIds` 列表。
- **HTTP 服务**：在 worldserver 中启动一个独立的 HTTP listener（如使用 `httplib::Server`），处理 `/feishu/callback`。
- **安全性**：回调地址必须验证 Feishu 请求签名（与发送签名算法不同，事件订阅使用 `X-Lark-Signature`）。
- **命令解析**：识别 `/whisper <player> <message>`，查找在线玩家并发送密语。

### 12.4 为什么不纳入 MVP

- 需要公网入口和 TLS 证书，部署复杂度远高于单向 webhook。
- 飞书事件订阅与自定义机器人 webhook 是两套不同的机制。
- MVP 的目标是替代 `mod-chat-transmitter` 的 Discord 推送功能，反向通信可作为后续增强。

## 13. 后续优化方向

1. **关键词告警**：在模块中配置敏感词列表，命中时单独 @ 管理员。
2. **命令反向通道（飞书→游戏密语）**：通过 Feishu bot 事件订阅接收群聊命令，解析后由 worldserver 向指定玩家发送密语。需要公网可访问的回调地址与严格鉴权，复杂度较高，作为可选需求。
3. **消息合并**：同类型消息可合并为一行，进一步降低请求频率。
4. **多 webhook 支持**：支持按聊天类型路由到不同飞书群。

## 14. 参考文件

- `modules/mod-chat-transmitter/src/PlayerScripts.cpp`
- `modules/mod-chat-transmitter/src/WorldScripts.cpp`
- `modules/mod-chat-transmitter/src/ChatTransmitter.h`
- `src/common/Cryptography/HMAC.h`
- `src/common/Encoding/Base64.h`
- `src/common/Utilities/ProducerConsumerQueue.h`
- [飞书 interactive 卡片文档](https://open.feishu.cn/document/uAjLw4CM/ukzMukzMukzM/feishu-cards/card-components/content-components/markdown)
