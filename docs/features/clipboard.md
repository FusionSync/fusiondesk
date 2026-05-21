# 剪切板重定向

FusionDesk 的剪切板模块是 `clipboard.redirect`。它不把平台剪切板直接暴露给 core，而是把平台对象包装成通用的 `TransferSourceBundle`，再通过 FDCL 协议在两端按需读取内容。

## 数据流

远端发生 copy 时：

```text
native clipboard owner
  -> platform endpoint snapshot
  -> TransferSourceBundle / FormatList
  -> clipboard.redirect module
  -> small_data channel
  -> peer clipboard.redirect module
  -> local platform endpoint publish promised targets
```

本地应用真正 paste 时：

```text
local target app requests native format
  -> local platform endpoint delayed render callback
  -> ClipboardRuntimeRemoteReader
  -> FDCL ReadFormatRequest
  -> remote module reads original owner/source
  -> FDCL ReadFormatResponse
  -> local endpoint transcodes when needed
  -> native clipboard response
```

这个设计保证普通文本、小图、小 HTML/RTF 可以直接走 inline response，大文件和文件夹走文件 promise 或流式读取。

## 格式策略

格式分两层：

- native format：系统剪切板实际看到的格式，例如 Windows `CF_UNICODETEXT`、`CF_DIB`，Linux `UTF8_STRING`、`text/uri-list`。
- canonical format：FusionDesk 内部用于表达转换策略的格式名，例如 `text/plain;charset=utf-8`、`text/html`、`image/png`、`application/x-fdcl-file-list`。

同系统两端优先原样透传。跨系统时，粘贴方根据目标系统需要执行转换。

## Windows

Windows endpoint 使用 Win32/OLE：

- 文本：`CF_UNICODETEXT`。
- HTML：`HTML Format`。
- RTF：`Rich Text Format`。
- PNG：registered `PNG`。
- 位图：`CF_DIB`、`CF_DIBV5`，跨平台时转换为 `image/png`。
- 文件：`CF_HDROP` 本地文件列表，远端文件使用 `IDataObject`/`IStream` 风格的 FileGroupDescriptor/FileContents 延迟流。

## macOS

macOS endpoint 使用 AppKit `NSPasteboard`：

- 普通内容通过 pasteboard item/data provider 延迟读取。
- 远端文件通过 `NSFilePromiseProvider` 发布。
- 当本地应用实际接收 promise 文件时，再通过 FDCL 读取远端文件内容。

## Linux X11

Linux endpoint 使用外部 `clipbus` X11/XCB backend，不使用 Qt `QClipboard`：

- 监听 X11 `TARGETS` 和 selection owner 变化。
- 支持 `UTF8_STRING`、`text/plain;charset=utf-8`、`text/html`、`text/rtf`、`image/png`。
- 延迟内容通过 X11 promised targets 提供。
- 大内容通过 X11 INCR streaming 路径喂给请求方。
- 远端文件通过可选 `fuse-promise` 暴露为本地 promise filesystem 路径，再发布 `text/uri-list`、GNOME copied-files 等 Linux 文件剪切板格式。

## 线程边界

Linux X11/clipbus 回调线程不能直接调用远端读取逻辑，因为远端读取需要驱动 FusionDesk 的 runtime pump 和网络事件处理。当前做法是：

- endpoint 定义纯 C++ `IClipboardCallbackDispatcher`。
- PC shell 用 Qt event loop 实现 dispatcher。
- core/modules 不依赖 Qt。
- 回调线程只投递任务或等待主线程执行任务。

这样既保持生产 Linux endpoint 不使用 `QClipboard`，也避免 X11 回调线程和 runtime 网络 pump 互相阻塞。
