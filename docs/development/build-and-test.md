# 构建、测试与边界

## 代码边界

FusionDesk 的关键边界是：

```text
apps -> runtime -> core
runtime -> modules
runtime -> adapters
modules -> core interfaces
adapters -> framework / transport / codec
platform -> OS services
```

提交代码时需要确认：

- core 不包含 Qt、Win32、AppKit、X11、FUSE 等具体平台 API。
- modules 只依赖 core contract 和模块自身的数据结构。
- runtime 负责生命周期、策略和服务编排。
- platform/adapters 负责 OS、Qt、transport、codec 等具体实现。
- apps 只做产品启动、参数解析、适配器装配和诊断输出。

## 推荐本地检查

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Qt 纯净扫描：

```bash
rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" \
  include/fusiondesk/core src/core include/fusiondesk/modules src/modules
```

补丁格式：

```bash
git diff --check
```

文档站：

```bash
python -m pip install -r requirements-docs.txt
mkdocs build --strict
```

## 测试分层

| 层级 | 目的 |
| --- | --- |
| core tests | 协议、网络、session、request tracker、module host 等纯 C++ 行为。 |
| runtime tests | display/clipboard runtime service、策略、重连、泵循环。 |
| adapter tests | Qt transport、Qt event loop、平台 adapter 边界。 |
| PC smoke | client/agent shell 的两端启动、profile、通道、剪切板和显示验证。 |

## 处理脏工作区

仓库允许存在未提交变更。修改前先用：

```bash
git status --short
```

不要回滚不相关文件。遇到同一文件已有变更时，先理解现有改动，再做最小范围补丁。
