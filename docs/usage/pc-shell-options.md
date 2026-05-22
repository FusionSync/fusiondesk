# PC Shell 参数模型

`fusiondesk_pc_client` 和 `fusiondesk_pc_agent` 现在通过 app 层的统一参数
registry 接收启动参数。这个 registry 的目标不是把所有开发参数暴露给用户，
而是把参数集中描述清楚，并为后续 GUI、文档和校验提供同一个来源。

当前实现入口：

```text
apps/pc/common/pc_option_registry.h
apps/pc/common/pc_option_registry.cpp
```

## 命令行帮助

普通帮助只展示用户、管理员、高级和诊断参数：

```bash
fusiondesk_pc_client --help
fusiondesk_pc_agent --help
```

完整帮助会展示开发和测试参数：

```bash
fusiondesk_pc_client --help-all
fusiondesk_pc_agent --help-all
```

## 启动参数参考

下面这些是产品侧可以讨论是否进入 GUI 的参数。开发联调和 CI 参数不在这里展开，
需要时使用 `--help-all` 查看。

### 用户参数

| 参数 | 类型/枚举值 | 说明 |
| --- | --- | --- |
| `--help` | flag | 输出 PC shell 参数帮助后退出。 |
| `--show-display-window` | flag | 客户端打开远程桌面窗口。 |
| `--display-source-type` | enum: `auto`、`monitor`、`screen`、`display`、`desktop`、`window`、`app`、`application`、`window_capture`、`virtual_display`、`virtual`、`vdisplay`、`mobile_projection`、`mobile`、`projection`、`media_projection`、`android_projection`、`unknown` | 选择共享内容类型。部分值是平台别名，最终由 display 运行时归一化。 |
| `--display-source-id` | integer | 选择屏幕、窗口或显示源编号。 |
| `--display-target-width` | integer | 指定目标采集宽度。 |
| `--display-target-height` | integer | 指定目标采集高度。 |
| `--display-scale-mode` | enum: `source`、`fit`、`stretch` | 远程画面缩放方式。 |
| `--display-no-cursor` | flag | 不采集或不显示远程鼠标指针。 |
| `--mount-input` | flag | 启用键盘和鼠标控制模块。 |
| `--clipboard-no-plain-text` | flag | 禁用纯文本剪切板传输。 |
| `--clipboard-no-html` | flag | 禁用 HTML 剪切板传输。 |
| `--clipboard-no-rtf` | flag | 禁用 RTF 剪切板传输。 |
| `--clipboard-no-image` | flag | 禁用图片剪切板传输。 |
| `--clipboard-no-file-list` | flag | 禁用文件列表剪切板传输。 |
| `--clipboard-no-file-contents` | flag | 禁用远程文件内容读取。 |
| `--clipboard-no-drag` | flag | 禁用剪切板拖放。 |
| `--clipboard-max-file-count` | unsigned integer | 一次剪切板 offer 允许的最大文件数。 |
| `--clipboard-max-single-file-bytes` | unsigned integer | 单个复制文件允许的最大字节数。 |

### 高级参数

| 参数 | 类型/枚举值 | 说明 |
| --- | --- | --- |
| `--display-fps` | integer | display pump 目标帧率。 |
| `--display-capture-backend` | string: `auto`、`gdi`、`dxgi`、`wgc` 或 adapter id | 强制选择采集后端，主要用于兼容性诊断。 |
| `--display-codec` | enum: `auto`、`raw`、`raw_bgra`、`bgra`、`raw_frame`、`h264`、`avc`、`h265`、`hevc`、`av1` | 首选显示编码族。 |
| `--display-codec-policy` | enum: `default`、`raw`、`raw-stable`、`windows-h264-production`、`h264-production`、`mf-h264-production`、`windows-h264-validation`、`h264-validation`、`mf-h264-validation` | display codec 发布/回退策略。 |
| `--display-codec-no-hardware` | flag | 禁用硬件编码候选。 |
| `--display-codec-prefer-software` | flag | 优先选择软件编码候选。 |
| `--clipboard-endpoint` | enum: `auto`、`windows`、`macos`、`linux`、`qt` | 选择本机剪切板 endpoint adapter。产品默认应使用 `auto`。 |

### 管理员策略参数

| 参数 | 类型/枚举值 | 说明 |
| --- | --- | --- |
| `--clipboard-policy-file` | path | 读取剪切板产品策略 JSON。 |
| `--clipboard-policy-export-file` | path | 导出当前生效的剪切板产品策略 JSON。 |
| `--clipboard-no-announce` | flag | 禁止向远端 announce 本地剪切板快照。 |
| `--clipboard-no-receive` | flag | 禁止接收远端剪切板 offer。 |
| `--clipboard-no-send-content` | flag | 禁止向远端发送剪切板内容。 |
| `--clipboard-no-write-local` | flag | 禁止把远端剪切板内容写入本地剪切板。 |
| `--clipboard-allow-custom-formats` | flag | 允许应用自定义剪切板格式。 |
| `--clipboard-max-inline-bytes` | unsigned integer | 剪切板 inline payload 最大字节数。 |
| `--clipboard-max-file-range-bytes` | unsigned integer | 单次远程文件 range read 最大字节数。 |
| `--clipboard-max-directory-depth` | unsigned integer | 本地目录展开为文件 offer 时允许的最大深度。 |
| `--clipboard-no-expand-directories` | flag | 不把本地目录展开成文件 offer。 |
| `--clipboard-runtime-audit` | flag | 记录允许通过的剪切板运行时操作。 |
| `--clipboard-runtime-max-audit-events` | unsigned integer | 保留的运行时审计事件数量上限。 |

### 诊断参数

| 参数 | 类型/枚举值 | 说明 |
| --- | --- | --- |
| `--print-session-diagnostics` | flag | 输出 session 运行时诊断。 |
| `--print-reconnect-diagnostics` | flag | 输出 reconnect 运行时诊断。 |
| `--print-display-runtime-diagnostics` | flag | 输出 display runtime 诊断。 |
| `--print-display-capture-plan` | flag | 输出 display capture 后端选择过程。 |
| `--print-display-sources` | flag | 输出 display source catalog。 |
| `--print-display-codec-plan` | flag | 输出 display codec 选择过程。 |
| `--print-module-inventory-diagnostics` | flag | 输出 module inventory 诊断。 |
| `--print-clipboard-diagnostics` | flag | 输出剪切板策略、运行时、模块和 endpoint 诊断。 |

## GUI 配置模型

GUI 不应该扫描 C++ 模块代码，也不应该理解 `mount/start/pump` 这类运行时
细节。它可以读取 app 层导出的 JSON 模型：

```bash
fusiondesk_pc_client --dump-gui-config-model
```

模型字段包含：

| 字段 | 说明 |
| --- | --- |
| `path` | GUI/配置文件使用的稳定配置路径，例如 `clipboard.files.maxFileCount`。 |
| `label` | 面向用户的字段名。 |
| `group` | GUI 分组。 |
| `visibility` | 可见性：`user`、`advanced`、`admin-policy`、`diagnostics`、`developer`、`test`。 |
| `owner` | 归属域，例如 `display.screen`、`clipboard.policy`。 |
| `cliOption` | 当前兼容的 CLI 参数名。 |
| `type` | 参数类型：`flag`、`string`、`integer`、`unsigned-integer`、`path`、`enum`。 |
| `enumValues` | 枚举参数的可接受值。 |
| `description` | 参数说明。 |

## 面向用户的配置分组

第一版 GUI 应主要使用这些配置路径，而不是直接暴露底层 CLI 名称。

### 远程画面

| 配置路径 | GUI 字段 | 当前 CLI |
| --- | --- | --- |
| `display.openWindow` | 打开远程桌面窗口 | `--show-display-window` |
| `display.shareSource.type` | 共享内容 | `--display-source-type` |
| `display.shareSource.id` | 选择屏幕或窗口 | `--display-source-id` |
| `display.resolution.width` | 自定义宽度 | `--display-target-width` |
| `display.resolution.height` | 自定义高度 | `--display-target-height` |
| `display.scaleMode` | 画面适配方式 | `--display-scale-mode` |
| `display.showRemoteCursor` | 显示远程鼠标指针 | `--display-no-cursor` |
| `display.quality.frameRate` | 流畅度 | `--display-fps` |

### 控制权限

| 配置路径 | GUI 字段 | 当前 CLI |
| --- | --- | --- |
| `input.enabled` | 允许键盘和鼠标控制 | `--mount-input` |

### 复制粘贴与文件

| 配置路径 | GUI 字段 | 当前 CLI |
| --- | --- | --- |
| `clipboard.content.allowText` | 允许文本 | `--clipboard-no-plain-text` |
| `clipboard.content.allowRichText` | 保留富文本格式 | `--clipboard-no-html` / `--clipboard-no-rtf` |
| `clipboard.content.allowImage` | 允许图片 | `--clipboard-no-image` |
| `clipboard.files.allowList` | 允许文件复制 | `--clipboard-no-file-list` |
| `clipboard.files.allowContents` | 允许文件内容传输 | `--clipboard-no-file-contents` |
| `clipboard.files.allowDragDrop` | 允许文件拖放 | `--clipboard-no-drag` |
| `clipboard.files.maxFileCount` | 一次最多复制文件数 | `--clipboard-max-file-count` |
| `clipboard.files.maxSingleFileBytes` | 单个文件大小上限 | `--clipboard-max-single-file-bytes` |

### 管理员策略

| 配置路径 | GUI 字段 | 当前 CLI |
| --- | --- | --- |
| `clipboard.direction.allowSend` | 允许复制到远端 | `--clipboard-no-announce` |
| `clipboard.direction.allowReceive` | 允许从远端复制 | `--clipboard-no-receive` |
| `clipboard.direction.allowContentSend` | 允许发送剪切板内容 | `--clipboard-no-send-content` |
| `clipboard.direction.allowLocalWrite` | 允许写入本地剪切板 | `--clipboard-no-write-local` |
| `clipboard.content.allowCustomFormats` | 允许应用自定义格式 | `--clipboard-allow-custom-formats` |
| `clipboard.audit.enabled` | 记录剪切板访问 | `--clipboard-runtime-audit` |
| `clipboard.policy.importPath` | 导入剪切板策略 | `--clipboard-policy-file` |
| `clipboard.policy.exportPath` | 导出当前剪切板策略 | `--clipboard-policy-export-file` |

## 参数可见性规则

| 可见性 | 用途 |
| --- | --- |
| `user` | 普通用户可以理解和选择的配置。 |
| `advanced` | 高级用户或支持人员用于兼容性、画质或平台问题的配置。 |
| `admin-policy` | 企业策略、审计和安全限制。 |
| `diagnostics` | 诊断输出，不属于持久产品配置。 |
| `developer` | 开发者、本地脚本、协议联调入口。 |
| `test` | CI、smoke、验证夹具。 |

GUI 默认只展示 `user`。企业管理界面可以展示 `admin-policy`。高级诊断页可展示
`advanced` 和 `diagnostics`。`developer` 和 `test` 不应出现在产品 UI。

## 边界

命令行只是 app 的一种输入方式，不是模块 API。

```text
argv / GUI / JSON / 服务端策略
  -> app 层统一解析和校验
  -> ProductProfile / SessionIntent / SecurityPolicy
  -> runtime 编排 session 和模块
  -> module 接收 typed config / policy
```

模块不应该解析 `argv`，也不应该知道用户从 GUI、CLI 还是服务端策略设置了参数。

## 参数校验

入口会在创建 Qt application 和启动 RuntimeHost 之前完成参数校验。

当前校验规则：

| 规则 | 行为 |
| --- | --- |
| 未注册的 `--xxx` 参数 | 直接失败，退出码为 `2`。 |
| 需要值但没有值 | 直接失败，退出码为 `2`。 |
| `integer` / `unsigned-integer` 类型错误 | 直接失败，退出码为 `2`。 |
| `enum` 值不在枚举集合内 | 直接失败，错误信息会列出可接受值。 |
| `--option=value` | 支持，等价于 `--option value`。 |

示例：

```bash
fusiondesk_pc_client --clipboard-endpoint plan9
```

输出会包含：

```text
invalid value for --clipboard-endpoint: plan9 (expected one of: auto|windows|macos|linux|qt)
Run fusiondesk-pc-client --help for supported options.
```
