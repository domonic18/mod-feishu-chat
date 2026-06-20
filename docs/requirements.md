# mod-feishu-chat 需求文档

## 1. 项目背景

当前 AzerothCore 项目已集成 `mod-chat-transmitter`，希望通过 Discord 机器人实时查看游戏内聊天内容，用于服务器管理（例如发现广告、刷屏、违规言论）。

由于国内网络环境无法稳定访问 Discord API，导致 `chat-transmitter-bot` 无法连接 Discord，整个方案不可用。

飞书（Feishu / Lark）是国内最常用的企业协作与群聊工具之一，支持**自定义机器人 webhook**，无需部署额外服务，服务端可直接通过 HTTPS POST 推送消息到飞书群。因此，计划参考 `mod-chat-transmitter` 的聊天捕获逻辑，实现一个新的模块 `mod-feishu-chat`，将游戏内聊天内容同步到飞书机器人。

## 2. 目标

- 捕获游戏内 `SAY` / `YELL` / `EMOTE` / `GUILD` / 频道聊天内容。
- 过滤常见插件频道，避免无效信息刷屏。
- 通过飞书自定义机器人 webhook 直接推送消息，无需中间服务。
- 支持飞书机器人签名校验（可选）。
- 保证 worldserver 主线程不被网络 IO 阻塞。
- 输出可运行的模块骨架与完整的技术方案文档，供需求澄清与技术评审。

## 3. 用户故事

| 角色 | 需求 |
|---|---|
| 服务器管理员 | 我希望在飞书群里看到玩家在游戏中的喊话/表情/频道聊天，以便及时发现广告或违规行为。 |
| 服务器管理员 | 我希望只接收真实聊天内容，不接收插件同步频道（如 Crb、LFGForwarder）的噪音。 |
| 服务器管理员 | 我希望飞书机器人支持签名校验，防止 webhook 被恶意利用。 |
| 服务器管理员 | 我希望模块启用/禁用、聊天类型过滤、消息格式都可以通过配置文件调整。 |
| 服务器管理员 | 我希望飞书网络波动不会影响 worldserver 正常运行。 |

## 4. 功能需求

### FR-1 聊天捕获

模块应注册 `PlayerScript` 钩子：

- `PLAYERHOOK_CAN_PLAYER_USE_CHAT`：捕获 `SAY`、`YELL`、`EMOTE`、`GUILD`。
- `PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT`：捕获玩家频道聊天。

聊天钩子的处理方式参考 `mod-chat-transmitter/src/PlayerScripts.cpp`。

### FR-2 频道过滤

默认过滤以下插件/转发频道（可配置）：

```
Crb, LFGForwarder, TCForwarder, LFGShout, xtensionxtooltip2, QuickHealMod
```

匹配规则为**子串匹配**：频道名称只要包含上述任一子串，即跳过不转发。

### FR-3 聊天类型开关

通过配置项独立控制是否转发以下类型：

- `FeishuChat.ForwardSay`
- `FeishuChat.ForwardYell`
- `FeishuChat.ForwardEmote`
- `FeishuChat.ForwardGuild`
- `FeishuChat.ForwardChannel`

### FR-4 飞书消息推送

模块将捕获到的聊天内容组装为飞书自定义机器人可识别的 JSON 请求体，通过 HTTPS POST 推送到：

```
https://open.feishu.cn/open-apis/bot/v2/hook/{hook_key}
```

默认使用 `msg_type: text` 纯文本格式。

### FR-5 飞书签名校验

当配置 `FeishuChat.Secret` 非空时，按照飞书官方算法生成签名：

1. 获取当前 Unix 时间戳（秒）。
2. 构造字符串：`timestamp + "\n" + secret`。
3. 使用 HMAC-SHA256 计算摘要，密钥为 `secret`。
4. Base64 编码得到 `sign`。
5. 请求体中携带 `timestamp` 与 `sign`。

### FR-6 消息格式模板

支持可配置的文本模板，占位符包括：

| 占位符 | 含义 |
|---|---|
| `{player}` | 玩家角色名 |
| `{level}` | 玩家等级 |
| `{class}` | 玩家职业名 |
| `{race}` | 玩家种族名 |
| `{zone}` | 玩家所在区域名 |
| `{type}` | 聊天类型（SAY/YELL/EMOTE/GUILD/CHANNEL） |
| `{channel}` | 频道名称（仅频道聊天有效） |
| `{message}` | 聊天内容 |

默认模板：

```text
[{type}] {player} (Lv{level} {class}): {message}
```

### FR-7 模块启停与热重载

- 在 `WORLDHOOK_ON_STARTUP` 时启动后台工作线程。
- 在 `WORLDHOOK_ON_SHUTDOWN` 时安全停止工作线程。
- 在 `WORLDHOOK_ON_AFTER_CONFIG_LOAD` 且为 reload 时，停止并重新启动模块以加载新配置。

## 5. 非功能需求

| 编号 | 需求 | 说明 |
|---|---|---|
| NFR-1 | 非阻塞 | 所有 HTTPS 请求必须在独立工作线程中执行，worldserver 主线程只做入队。 |
| NFR-2 | 低耦合 | 模块不修改核心代码，仅通过 ScriptHook 与 Config 机制集成。 |
| NFR-3 | 容错性 | 网络超时、DNS 失败、飞书 429/500 等异常不得导致 worldserver 崩溃或卡顿。 |
| NFR-4 | 队列背压 | 消息队列设置上限，超限时丢弃最旧消息并记录警告日志。 |
| NFR-5 | 安全性 | 配置中的 webhook URL 与签名校验密钥不得打印到日志。 |
| NFR-6 | 可维护性 | 代码遵循 AzerothCore 风格（4 空格缩进、Allman 大括号、`{}` 格式化字符串等）。 |

## 6. 约束与假设

- 目标运行环境：AzerothCore WotLK 3.3.5a，C++20。
- 网络：worldserver 所在服务器能够访问 `open.feishu.cn`（国内通常可达）。
- 依赖：OpenSSL 已通过 `common` 目标链接；Boost.Asio 头文件可用；`httplib.h` 作为单头文件库随模块提供。
- 不依赖 `mod-eluna`、`mod-anticheat` 或 `mod-ale`。
- 本模块 MVP 只转发聊天消息，不处理从飞书到游戏的反向命令。反向密语为可选需求，见第 9 节。

## 7. 待确认问题

在正式编码前，建议评审以下问题：

1. **公会频道**：需要转发 `GUILD` 聊天（已通过 `CHAT_MSG_GUILD` 类型捕获）。队伍、团队、密语仍默认不转发。
2. **玩家身份信息**：仅附带**玩家角色名**，不附带账号名与最后登录 IP。
3. **消息格式**：采用纯文本 `msg_type: text`，暂不使用富文本卡片。
4. 消息队列上限与 HTTP 超时的默认值是否合理？
5. 是否需要在飞书消息中 `@` 指定管理员？可在模板中通过飞书 `@` 语法实现。

## 8. 验收标准

- [ ] 模块目录与文档已创建。
- [ ] `worldserver` 编译通过（静态模块）。
- [ ] 配置启用后，游戏内 SAY/YELL/EMOTE/GUILD/频道聊天能在 5 秒内出现在飞书群。
- [ ] 插件频道内容不会出现在飞书群。
- [ ] 关闭 `FeishuChat.Enabled` 后模块不产生任何网络请求。
- [ ] worldserver 运行期间，模拟飞书网络异常（如断开外网）不会导致崩溃或明显卡顿。

## 9. 可选需求（后续版本）

### OR-1 飞书反向密语

**需求描述**：管理员可以在飞书群中发送指定格式的消息，模块解析后将内容以游戏内密语形式发送给指定玩家。

**典型使用场景**：管理员发现玩家聊天违规后，直接在飞书群中回复 `"whisper 玩家名 请不要发广告"`，该内容即以 GM 身份密语给对应玩家。

**实现约束**：

- 需要 Feishu 机器人开启**事件订阅**或提供可接收回调的公网 HTTP 端点，MVP 版本不实现。
- 需要严格的身份鉴权（如仅允许指定飞书 open_id 发送命令）。
- 命令格式建议：
  ```
  /whisper <player> <message>
  ```
- 该功能会显著改变架构（从单向推送变为双向通信），建议在 MVP 稳定后再评估实现。
