# FusionDesk

FusionDesk is a modular remote desktop platform for enterprise remote access,
device control, display streaming, input injection, and data redirection.

The project is built around a pure C++ core and a set of replaceable runtime,
module, transport, platform, and framework adapters. Display, input,
clipboard, file transfer, policy, diagnostics, codec, and future tunnel
features are designed as independently versioned capabilities instead of being
hardwired into one desktop application.

> Status: FusionDesk is under active engineering development. The core runtime,
> module contracts, Windows PC shell, display MVP, and clipboard redirection
> foundation are available for review and testing; public APIs and packages are
> not stable yet.

## Why FusionDesk

- **Modular feature runtime**
  Capabilities are loaded through manifests, role/platform metadata, policy
  gates, channel bindings, and module-owned protocol validation. New features
  can be added without taking ownership of session startup or transport code.

- **Multi-channel transport model**
  Control, realtime, small-data, large-data, and auxiliary traffic can be
  scheduled independently with priority, queue policy, pressure reporting,
  request/response correlation, ACK paths, timeout handling, and reconnect
  orchestration.

- **Cross-platform adapter boundary**
  Core logic is kept free of Qt and operating-system APIs. Platform-specific
  capture, input, clipboard, drag/drop, codecs, and UI integration live behind
  adapters so Windows, Linux, macOS, Android, and embedded targets can evolve
  without contaminating the core contracts.

- **Data redirection as first-class runtime behavior**
  Clipboard and drag/drop are modeled as transferable objects with format
  metadata, lazy content reads, streamable file payloads, policy enforcement,
  and audit-friendly diagnostics. Text, rich text, images, files, and drag
  coordinates share one module-level flow.

- **Enterprise policy and diagnostics**
  Session, module, network, display, and clipboard paths expose stable
  diagnostics for automation, product UI, CI smoke tests, and field triage.
  Policy decisions are explicit and sit above feature startup instead of being
  hidden inside adapters.

- **Product shell and SDK direction**
  The current PC agent/client shell proves the runtime path, while the Android
  controller direction is designed for embedding FusionDesk into another app or
  product surface.

## Current Capabilities

- C++17 core runtime for protocol, session, network, module, and policy.
- PC agent/client shells with CMake build and CTest smoke coverage.
- Display module MVP with raw-frame transport, runtime diagnostics, backend
  selection, source selection, reconnect recovery, and Windows GDI/DXGI/WGC
  adapter paths.
- Codec selection and negotiation contracts for raw, H.264, H.265, AV1, and
  hardware/software backend rollout.
- Clipboard redirection foundation with text, formatted text, image, file,
  remote file stream, drag preflight, object locking, reconnect, Windows native
  adapter, Qt adapter, and macOS adapter entry points.
- Multi-endpoint TCP profile planning and peer profile exchange for startup
  negotiation.
- Release packaging scripts for Windows and Linux engineering builds.

## Architecture

FusionDesk keeps product startup thin and pushes behavior into runtime-owned
contracts:

```text
apps
  -> runtime
    -> session
      -> policy
      -> network
      -> modules
        -> adapters
          -> platform/framework/transport/codec implementation
```

The main invariant is that modules own feature payloads and compatibility,
while the mainline owns lifecycle, policy, routing, scheduling, and diagnostics.

Repository layout:

```text
apps/          PC agent/client shells and product-facing startup code
adapters/      Qt, transport, codec, and framework adapters
bindings/      External package and language binding surfaces
cmake/         Build helpers and cross-toolchain entries
docs/          Architecture, stage gates, runbooks, and design records
include/       Public FusionDesk C++ headers
modules/       Module-facing notes and future module package surfaces
platform/      OS-specific adapter implementation areas
runtime/       Runtime orchestration helpers and product services
src/           Core, runtime, module, adapter, and platform implementation
tests/         Unit, smoke, adapter, runtime, and PC two-peer tests
tools/         Release and validation scripts
```

Canonical design documents live in `docs/architecture/`. Start with:

- `docs/architecture/README.md`
- `docs/architecture/FUSIONDESK_ARCHITECTURE.md`
- `docs/architecture/NETWORK_MODEL.md`
- `docs/architecture/PROTOCOL_MESSAGE_PATTERN.md`
- `docs/architecture/MODULE_AND_INTERFACE_BLUEPRINT.md`
- `docs/architecture/DISPLAY_SCREEN_PIPELINE_DESIGN.md`
- `docs/architecture/CLIPBOARD_REDIRECTION_FOUNDATION.md`

## Build

Windows host build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Preset entry points:

```powershell
cmake --list-presets=all .
cmake --preset windows-host-release
cmake --build --preset windows-host-release
ctest --test-dir build/windows-host-release -C Release --output-on-failure
```

Cross presets are provided for Linux, Android, OpenHarmony/HarmonyOS, and
Rockchip board targets. They require the matching compiler, sysroot, SDK, or
NDK environment variables described in `cmake/toolchains/README.md`.

## Packaging

Windows engineering package:

```powershell
.\tools\package_windows_release.ps1 `
  -BuildDir build `
  -Configuration Release `
  -OutputDir artifacts\release `
  -Version dev `
  -Platform windows-x64
```

Linux engineering package:

```bash
VERSION=dev \
PLATFORM=linux-x86_64 \
BUILD_DIR=build \
CONFIGURATION=Release \
OUTPUT_DIR=artifacts/release \
bash tools/package_linux_release.sh
```

## Development Direction

The near-term work is focused on completing the product runtime loop:

- harden display capture, rendering, codec negotiation, and recovery;
- finish cross-platform clipboard and drag/drop adapters;
- extend data-redirection modules for large file and directory transfer;
- add production transport choices, tunnel, relay, and reconnect policy;
- mature Android controller embedding and PC product UI integration;
- keep architecture, diagnostics, tests, and packaging aligned at each stage.

## Related Open-Source Remote Desktop Projects

These projects are useful references for the broader remote desktop ecosystem:

- [RustDesk](https://github.com/rustdesk/rustdesk)
- [FreeRDP](https://github.com/FreeRDP/FreeRDP)
- [Apache Guacamole](https://guacamole.apache.org/)
- [MeshCentral](https://github.com/Ylianst/MeshCentral)
- [noVNC](https://github.com/novnc/noVNC)
- [Remmina](https://gitlab.com/Remmina/Remmina)
- [xrdp](https://github.com/neutrinolabs/xrdp)
