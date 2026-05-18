# ABI and Toolchain Matrix

This document fixes target platforms, CPU architectures, and first-pass build responsibilities for `FusionDesk`.

## Primary Matrix

```text
Windows x64
Windows arm64
Linux x86_64
Linux arm64 / aarch64
Linux loongarch64
Linux mips64el
macOS x86_64
macOS arm64
Android arm64-v8a
Android x86_64
OpenHarmony/HarmonyOS arm64
RK3568 Linux/Android aarch64
RK3588 Linux/Android aarch64
```

Conditional:

```text
Android armeabi-v7a
customer Linux vendor architectures beyond loongarch64 and mips64el
```

`armeabi-v7a` is not a default target. Add it only when a customer device fleet requires 32-bit Android support.

Architecture naming policy:

```text
use x86_64 for 64-bit Intel/AMD Linux and Android emulator artifacts
use x64 for Windows package naming when matching existing product convention
use arm64-v8a for Android ABI names
use aarch64 for Linux toolchain triplets and embedded board docs
use loongarch64 for LoongArch Linux targets
use mips64el for little-endian 64-bit MIPS Linux targets
```

## Qt Version Strategy

Current legacy build uses Qt 5.15.2.

`FusionDesk` policy:

```text
PC rebuild work can initially keep Qt 5.15.2 for compatibility.
Android should be evaluated on Qt 6 LTS before hard commitment.
Core must not depend on Qt version.
Adapters isolate Qt 5/Qt 6 differences.
```

If Qt 5.15.2 is used for Android, document the commercial/open-source distribution constraints before release packaging.

## Toolchain Targets

Windows:

```text
MSVC 2019/2022
CMake
Qt desktop kit
x64 only for first FusionDesk delivery
arm64 after the x64 client/agent path and Qt kit are stable
```

Linux:

```text
gcc or clang
CMake
Qt desktop kit
x86_64 and arm64
cross toolchain and sysroot for aarch64 board builds
cross toolchain and sysroot for loongarch64
cross toolchain and sysroot for mips64el
```

`CMakePresets.json` now defines host and cross-target preset entry
points. Cross toolchain files live under `cmake/toolchains` and require
environment variables for compiler, sysroot, SDK, NDK, or vendor toolchain
paths instead of repository hard-coded paths.

macOS:

```text
Apple Clang
CMake
Qt macOS kit
x86_64 and arm64
```

Android:

```text
Android NDK
CMake
Gradle
Qt Android kit
arm64-v8a first
x86_64 emulator/test
```

OpenHarmony/HarmonyOS:

```text
vendor SDK/NDK
CMake toolchain file
Qt kit only if the target distribution supports it
arm64 first
controller/client role first
```

RK3568/RK3588:

```text
aarch64 Linux or Android toolchain
vendor sysroot matching the deployed image
optional Rockchip MPP codec adapter
display capture backend capabilities tag these targets as arm64 plus RK3568/RK3588 for selector filtering
no direct dependency from core or display modules to vendor SDK headers
```

## Build Targets

Core:

```text
fusiondesk_core
```

Runtime:

```text
fusiondesk_runtime
fusiondesk_runtime_qt
```

Modules:

```text
fusiondesk_display
fusiondesk_input
fusiondesk_audio
fusiondesk_clipboard
fusiondesk_filesystem
fusiondesk_printer
fusiondesk_camera
fusiondesk_peripheral
```

Android:

```text
fusiondesk_android_client
fusiondesk_android_controller_aar
```

## CI Gates

Required first:

```text
fusiondesk_core builds on Ubuntu x86_64
Windows package still builds
Linux package still builds
```

Next:

```text
fusiondesk_core builds on Windows x64
fusiondesk_core builds on Linux arm64
Android arm64-v8a native library builds
Android x86_64 emulator native library builds
```

Later:

```text
macOS x86_64
macOS arm64
Windows arm64
Linux loongarch64
Linux mips64el
RK3568/RK3588 aarch64 sysroot build
OpenHarmony/HarmonyOS arm64 native library build
Android AAR packaging
Android APK/AAB packaging
```

The preset/toolchain entry points exist for those later targets, but actual CI
workers, validated sysroots, Qt kits, SDK/NDK installs, signing, and packaging
remain separate release gates.

## Artifact Names

```text
fusiondesk-core-<version>-<platform>-<arch>
fusiondesk-client-<version>-windows-x64
fusiondesk-client-<version>-windows-arm64
fusiondesk-client-<version>-linux-x86_64
fusiondesk-client-<version>-linux-arm64
fusiondesk-client-<version>-linux-loongarch64
fusiondesk-client-<version>-linux-mips64el
fusiondesk-client-<version>-macos-universal
fusiondesk-android-client-<version>-arm64-v8a
fusiondesk-android-controller-<version>.aar
fusiondesk-openharmony-controller-<version>-arm64
```
