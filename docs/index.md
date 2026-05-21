# FusionDesk 文档

FusionDesk 是一个模块化远程桌面运行时，目标是把显示、输入、剪切板、文件传输、策略、诊断、传输和平台适配拆到清晰边界中。

当前仓库处于工程推进阶段：核心协议、网络、Session、模块装配、PC shell、显示 MVP、剪切板重定向基础和 Linux/X11 剪切板适配正在逐步收敛。本文档站用于把使用方式、开发边界和架构文档集中发布到 GitHub Pages。

## 你可以从这里开始

| 目标 | 入口 |
| --- | --- |
| 本地构建和跑测试 | [构建与测试](getting-started.md) |
| 启动 PC client/agent shell | [PC Shell 使用](usage/pc-shell.md) |
| 理解剪切板重定向 | [剪切板重定向](features/clipboard.md) |
| 维护文档站发布 | [GitHub Pages 文档发布](deployment/github-pages.md) |
| 阅读完整架构 | [架构索引](architecture/README.md) |

## 架构边界

FusionDesk 的基本依赖方向是：

```text
apps -> runtime -> core
runtime -> modules
runtime -> adapters
modules -> core interfaces
adapters -> framework / transport / codec
platform -> OS services
```

核心约束：

- `include/fusiondesk/core` 和 `src/core` 保持纯 C++。
- Qt、Win32、AppKit、X11、FUSE 等具体依赖只能出现在 apps、runtime adapter、platform 或 adapter 层。
- 功能行为由模块拥有，网络只负责通道、路由、队列、压力和重连。
- 剪切板内容读取使用请求/响应和延迟读取，不把大对象提前塞进普通剪切板事件。

## 当前重点

- PC 端 client/agent 启动主线和多通道本地 TCP profile。
- `display.screen` 的采集、编码、解码、渲染和诊断路径。
- `clipboard.redirect` 的跨平台格式映射、延迟读取、文件 promise 和 Linux X11 endpoint。
- GitHub Actions 的构建、测试、发布包和文档发布。
