# 构建与测试

本文说明如何在本地构建 FusionDesk，并运行当前仓库的测试。

## 前置条件

通用依赖：

- CMake 3.20 或更高版本。
- 支持 C++17 的编译器。
- Git。

Windows 建议：

- Visual Studio 2022。
- Qt 5.15.x desktop kit。

Linux 建议：

- GCC 或 Clang。
- Ninja 或 Make。
- Qt 5.15.x。
- Linux 剪切板 endpoint 需要 `clipbus`，远程文件 promise 需要可选的 `fuse-promise`。

## Windows 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

也可以使用 preset：

```powershell
cmake --list-presets=all .
cmake --preset windows-host-release
cmake --build --preset windows-host-release
```

## Linux 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

如果系统中有多个 Qt 或第三方 clipboard 依赖，配置前先设置对应的 `PATH`、`CMAKE_PREFIX_PATH` 或 `PKG_CONFIG_PATH`。

## 常用验证

检查核心和模块没有引入 Qt：

```bash
rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" \
  include/fusiondesk/core src/core include/fusiondesk/modules src/modules
```

检查补丁格式：

```bash
git diff --check
```

构建文档站：

```bash
python -m pip install -r requirements-docs.txt
mkdocs build --strict
```

本地预览文档站：

```bash
mkdocs serve
```

打开 `http://127.0.0.1:8000/`。
