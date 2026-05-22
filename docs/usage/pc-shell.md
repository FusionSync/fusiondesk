# PC Shell 使用

FusionDesk 当前提供三个 PC shell 可执行目标：

| 目标 | 用途 |
| --- | --- |
| `fusiondesk_pc_profile_plan` | 生成 client/agent 本地 TCP profile。 |
| `fusiondesk_pc_agent` | 启动 agent session，监听 profile 中声明的通道。 |
| `fusiondesk_pc_client` | 启动 client session，连接 agent profile。 |

完整参数分层、可见性和 GUI 配置模型见
[PC Shell 参数模型](pc-shell-options.md)。

## 生成本地 profile

```bash
fusiondesk_pc_profile_plan \
  --client-profile /tmp/fusiondesk-client.json \
  --agent-profile /tmp/fusiondesk-agent.json \
  --client-ready-prefix pc-client \
  --agent-ready-prefix pc-agent \
  --channel control=127.0.0.1:31001 \
  --channel small_data=127.0.0.1:31002 \
  --channel main_screen=127.0.0.1:31003 \
  --channel large_data=127.0.0.1:31004
```

## 启动 agent

```bash
fusiondesk_pc_agent \
  --listen-profile /tmp/fusiondesk-agent.json \
  --session-id 2 \
  --mount-profile-modules \
  --profile-module display.screen \
  --start-display \
  --wait-channels-ms 6000 \
  --run-ms 10000
```

## 启动 client

```bash
fusiondesk_pc_client \
  --transport-profile /tmp/fusiondesk-client.json \
  --session-id 1 \
  --mount-profile-modules \
  --profile-module display.screen \
  --start-display \
  --wait-channels-ms 6000 \
  --run-ms 10000
```

## 剪切板常用参数

```bash
--profile-module clipboard.redirect
--start-clipboard
--pump-clipboard
--clipboard-endpoint <auto|windows|macos|linux|qt>
--clipboard-owner-window-name <text>
--require-clipboard-text <text>
--clipboard-require-wait-ms <ms>
--print-clipboard-diagnostics
```

Linux X11 endpoint 示例：

```bash
fusiondesk_pc_agent \
  --listen-profile /tmp/fusiondesk-agent.json \
  --session-id 2 \
  --profile-module clipboard.redirect \
  --start-clipboard \
  --pump-clipboard \
  --clipboard-endpoint linux \
  --clipboard-no-receive \
  --print-clipboard-diagnostics \
  --run-ms 15000
```

```bash
fusiondesk_pc_client \
  --transport-profile /tmp/fusiondesk-client.json \
  --session-id 1 \
  --profile-module clipboard.redirect \
  --start-clipboard \
  --pump-clipboard \
  --clipboard-endpoint linux \
  --clipboard-no-announce \
  --require-clipboard-text "hello" \
  --clipboard-require-wait-ms 6000 \
  --print-clipboard-diagnostics \
  --run-ms 10000
```

## 诊断输出

`--print-clipboard-diagnostics` 会输出稳定的机器可读行，例如：

```text
clipboard.policy ...
clipboard.runtime ...
clipboard.module ...
clipboard.endpoint ...
```

这些输出可以用于 smoke test、CI 日志和后续产品 UI/service 读取。
