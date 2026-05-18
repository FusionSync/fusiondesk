# Platform Targets

`FusionDesk` is a cross-platform architecture. Qt is the primary application and platform abstraction framework, but Qt must not leak into the pure core contracts unless a component is explicitly a Qt adapter.

The first delivery order is:

```text
PC desktop clients and agents
Android client controller
Android embeddable controller library
future mobile or web-facing surfaces
```

## Platform Matrix

Primary PC targets:

```text
Windows x64
Windows arm64
Linux x86_64
Linux arm64 / aarch64
Linux loongarch64
Linux mips64el
macOS x86_64
macOS arm64
```

Embedded and domestic-platform targets:

```text
RK3568 Linux or Android images, aarch64
RK3588 Linux or Android images, aarch64
OpenHarmony or HarmonyOS arm64 controller/client targets
customer LoongArch64 Linux deployments
customer mips64el Linux deployments
```

Android targets:

```text
Android arm64-v8a
Android armeabi-v7a if required by customer devices
Android x86_64 for emulator and test
```

`LoongArch` is the canonical architecture family name. Build triplets and
artifact names should use `loongarch64`, not informal spellings.

## Support Tiers

Tier 1:

```text
Windows x64 PC client
Windows x64 PC agent
```

Tier 2:

```text
Linux x86_64 PC client/agent
Linux aarch64 PC client/agent
macOS universal client/agent
Android arm64-v8a controller/client
Android x86_64 emulator/test client
```

Tier 3:

```text
Windows arm64 client/agent
Linux loongarch64 client/agent
Linux mips64el client/agent
OpenHarmony or HarmonyOS arm64 controller/client
RK3568 and RK3588 appliance builds
Android armeabi-v7a customer-only client
```

Tier 3 targets must not add platform assumptions to core or modules. They enter
through toolchain files, platform adapters, Qt kits, and optional codec/render
adapters.

`CMakePresets.json` provides the first host/cross build entry points:
Windows host, Linux x86_64/aarch64/loongarch64/mips64el, RK3568/RK3588
aarch64, Android arm64-v8a/x86_64, and OpenHarmony/HarmonyOS arm64. The cross
toolchain files under `cmake/toolchains` require environment variables for
actual SDK, sysroot, NDK, or compiler paths.

Legacy architecture references:

```text
Windows uses x64 as the current production target.
Linux scripts already model x86_64 and arm64 through BUILD_ARCH.
macOS packaging currently uses x86_64 and should add arm64 when the Qt/toolchain path is ready.
Android should start with arm64-v8a.
```

## Product Roles by Platform

PC:

```text
Client: controller and renderer
Agent: controlled endpoint
Auth: enterprise authorization and broker-facing service
Tools: installer, printer, IDD controller, peripheral bridge, protector
```

Android:

```text
Android Qt Client: remote desktop controller application
Android Controller Library: embeddable library for native Android apps
Android JNI Binding: stable Java/Kotlin facade over native FusionDesk runtime
```

Android is initially client-only. Agent, Auth, printer, filesystem server, and peripheral bridge are not Android targets in the first Android phase.

## Display Role by Platform

Display role support starts from the product role, then selects capture and
render adapters.

```text
Windows PC:
  client render, agent capture, multi-monitor, future DXGI/WGC production capture

Linux PC:
  client render, agent capture, X11/PipeWire/DRM backend selection by session type

macOS PC:
  client render, agent capture, ScreenCaptureKit-first production direction

Android:
  controller/client render first, optional future controlled-agent capture through MediaProjection

OpenHarmony/HarmonyOS:
  controller/client first, capture only after device SDK and permission model are validated

RK3568/RK3588:
  aarch64 appliance targets using the Linux or Android display path; capture selector tags these backends as arm64 + RK3568/RK3588 while vendor hardware acceleration remains an adapter decision
```

## Qt Boundary

Allowed Qt usage:

```text
apps/*
runtime/qt/*
platform/* when implementing platform services
modules/* when the module implementation needs Qt multimedia, OpenGL, windowing, or event loop integration
adapters/qt/*
bindings/android when bridging to Android app lifecycle
```

Forbidden Qt usage:

```text
core/protocol
core/network interfaces
core/module manifest model
core/policy model
core/session context
```

Rationale:

```text
core must compile as a pure C++ library.
Qt adapters can bind core to Qt sockets, Qt event loops, Qt render surfaces, and Android lifecycle.
Native Android applications should not depend on private Qt UI classes to call the controller library.
```

## Android Client Forms

Two Android outputs are planned:

```text
fusiondesk_android_client
fusiondesk_android_controller_aar
```

`fusiondesk_android_client` is the Qt application. It owns its UI and ships as an APK or AAB.

`fusiondesk_android_controller_aar` is the embeddable library. It exposes Java/Kotlin APIs and loads native libraries internally.

## Android Library Contract

The native Android library exposes:

```text
create controller
destroy controller
connect session
disconnect session
attach render surface
resize render surface
send keyboard event
send pointer event
send touch event
query session state
subscribe diagnostics
```

The Java/Kotlin facade must not expose raw C++ pointers. It should use opaque handles or lifecycle-owned controller objects.

## Android Render Boundary

Android rendering should start with a Qt-backed render surface for speed of delivery.

Future render modes:

```text
Qt OpenGL render surface
Android SurfaceTexture bridge
Android HardwareBuffer path
MediaCodec decoder path
software fallback
```

Display decode/render internals remain in the display module. Android app code only attaches a render target and receives state callbacks.

## Directory Ownership

Platform code lives under:

```text
include/fusiondesk/platform
src/platform
```

Current PC platform display status:

```text
platform/windows/display contains WindowsGdiDisplayCapture, a compiled DXGI Desktop Duplication adapter path, a rollout-gated Windows Graphics Capture monitor/window adapter, and the WindowsDisplayCaptureBackendFactory aggregate.
PC agent startup creates capture through the aggregate factory, probes DXGI by default, falls back to GDI when needed, and passes the same factory into DisplayRuntimeService for auto-mode backend failover.
These remain platform targets, not core dependencies.
```

Current PC platform feature status:

```text
platform/windows/input contains WindowsInputInjector as the first dry-run input injection seam.
Clipboard platform endpoints are not part of the current FusionDesk slice.
PC shell startup can select input profile modules and inject these adapters through RuntimeHost.
These are platform targets, not core dependencies, and still represent dry-run/text-only seams rather than production OS behavior.
```

Android bindings live under:

```text
bindings/android
src/bindings/android
```

Android application and library products live under:

```text
apps/android/client_qt
apps/android/controller_library
```

Android packaging lives under:

```text
packaging/android
```

## First Android Phase

First Android phase includes:

```text
Qt Android client shell
controller library facade
session connect/disconnect
main screen render surface
keyboard/mouse/touch event injection API
diagnostics callback
```

Deferred:

```text
Android as controlled agent
filesystem redirection from Android local storage
camera redirection from Android camera
microphone redirection from Android microphone
background service mode
MDM policy integration
```
