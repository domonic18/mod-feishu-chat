# mod-feishu-chat

AzerothCore module that forwards in-game chat messages (SAY / YELL / EMOTE / channel) to a Feishu (Lark) custom bot webhook.

## Why

The existing `mod-chat-transmitter` relies on Discord, which is hard to reach from some networks. Feishu is widely used in China and provides simple HTTPS webhooks, so this module sends chat events directly from the worldserver to a Feishu group chat without any intermediate service.

## Status

This module is currently at the **design / skeleton** stage. The directory structure, configuration, and documentation are in place; the C++ implementation is a working skeleton ready for review and completion.

See:

- `docs/requirements.md` — user requirements and acceptance criteria
- `docs/development.md` — technical design and architecture

## Requirements

- AzerothCore WotLK 3.3.5a (C++20)
- A Feishu group chat with a custom bot enabled
- Outbound HTTPS access to `open.feishu.cn` from the worldserver host

## Setup

1. In a Feishu group chat, add a **custom bot** and copy its webhook URL and signature secret.
2. Copy `conf/FeishuChat.conf.dist` to `conf/FeishuChat.conf` in your server config directory.
3. Edit the config:

```ini
FeishuChat.Enabled    = 1
FeishuChat.WebhookUrl = "https://open.feishu.cn/open-apis/bot/v2/hook/xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
FeishuChat.Secret     = "your-signature-secret"
```

4. Rebuild `worldserver` with `-DMODULES=static`.
5. Start the server and send a chat message in-game.

## Build

```bash
mkdir -p build && cd build
cmake .. -DMODULES=static -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc)
```

## License

Same as AzerothCore (AGPL / GPL as applicable by upstream).
