<div align="center">

# FusionDesk

**A modular remote desktop runtime for display, input, clipboard, file transfer, policy, and diagnostics.**

[![Release](https://github.com/FusionSync/fusiondesk/actions/workflows/fusiondesk-release.yml/badge.svg)](https://github.com/FusionSync/fusiondesk/actions/workflows/fusiondesk-release.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white)
![Qt](https://img.shields.io/badge/UI-Qt-41CD52?logo=qt&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-supported-0078D4?logo=windows&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-target-FCC624?logo=linux&logoColor=black)
![Android](https://img.shields.io/badge/Android-planned-3DDC84?logo=android&logoColor=white)

</div>

FusionDesk is an enterprise-oriented remote desktop platform built around a pure C++ core and replaceable adapters. The goal is to make remote access features composable: display streaming, input, clipboard, file transfer, codecs, transport, policy, and diagnostics attach through stable runtime contracts instead of being locked inside one monolithic client.

> Active engineering project: runtime, module contracts, Windows PC shell, display MVP, and clipboard redirection foundations are available for review and testing. Public APIs and release packages are not stable yet.

## Highlights

| Area | What FusionDesk Provides |
| --- | --- |
| Runtime | Session lifecycle, module loading, policy gates, reconnect orchestration, and diagnostics in one product loop. |
| Modules | Feature behavior is owned by versioned modules with manifests, payload codecs, validators, and role/platform metadata. |
| Network | Multi-channel routing for control, realtime, small-data, large-data, and auxiliary traffic with priority and pressure handling. |
| Display | Raw-frame MVP, Windows GDI/DXGI/WGC capture paths, source selection, recovery, codec selection, and H.264 rollout contracts. |
| Clipboard | Text, rich text, images, files, drag preflight, lazy content reads, streamable file payloads, object locking, and reconnect. |
| Platforms | Core stays free of Qt and OS APIs; Qt, Windows, macOS, Linux, Android, codec, and transport details live behind adapters. |

## Architecture

```text
apps -> runtime -> session -> policy
                         -> network
                         -> modules -> adapters -> platform/framework/transport/codec
```

| Path | Purpose |
| --- | --- |
| `include/` | Public FusionDesk C++ headers. |
| `src/` | Core, runtime, module, adapter, and platform implementation. |
| `apps/` | PC agent/client shells and product startup code. |
| `tests/` | Unit, smoke, adapter, runtime, and two-peer tests. |
| `tools/` | Release and validation scripts. |
| `docs/architecture/` | Architecture, stage gates, runbooks, and ADRs. |

## Quick Start

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Presets are available for Windows host builds and cross-target entry points:

```powershell
cmake --list-presets=all .
cmake --preset windows-host-release
cmake --build --preset windows-host-release
```

## Packaging

```powershell
.\tools\package_windows_release.ps1 -BuildDir build -Configuration Release -OutputDir artifacts\release -Version dev -Platform windows-x64
```

```bash
VERSION=dev PLATFORM=linux-x86_64 BUILD_DIR=build CONFIGURATION=Release OUTPUT_DIR=artifacts/release bash tools/package_linux_release.sh
```

## Documentation

| Topic | Document |
| --- | --- |
| Architecture overview | [`docs/architecture/FUSIONDESK_ARCHITECTURE.md`](docs/architecture/FUSIONDESK_ARCHITECTURE.md) |
| Network model | [`docs/architecture/NETWORK_MODEL.md`](docs/architecture/NETWORK_MODEL.md) |
| Protocol pattern | [`docs/architecture/PROTOCOL_MESSAGE_PATTERN.md`](docs/architecture/PROTOCOL_MESSAGE_PATTERN.md) |
| Module blueprint | [`docs/architecture/MODULE_AND_INTERFACE_BLUEPRINT.md`](docs/architecture/MODULE_AND_INTERFACE_BLUEPRINT.md) |
| Display pipeline | [`docs/architecture/DISPLAY_SCREEN_PIPELINE_DESIGN.md`](docs/architecture/DISPLAY_SCREEN_PIPELINE_DESIGN.md) |
| Clipboard redirection | [`docs/architecture/CLIPBOARD_REDIRECTION_FOUNDATION.md`](docs/architecture/CLIPBOARD_REDIRECTION_FOUNDATION.md) |

## Roadmap Focus

- Harden display capture, rendering, codec negotiation, and recovery.
- Finish cross-platform clipboard and drag/drop adapters.
- Expand file and directory transfer for large-data redirection.
- Add production transport, tunnel, relay, and reconnect policy.
- Mature Android controller embedding and PC product UI integration.

## Related Projects

Useful references from the remote desktop ecosystem:

[RustDesk](https://github.com/rustdesk/rustdesk) ·
[FreeRDP](https://github.com/FreeRDP/FreeRDP) ·
[Apache Guacamole](https://guacamole.apache.org/) ·
[MeshCentral](https://github.com/Ylianst/MeshCentral) ·
[noVNC](https://github.com/novnc/noVNC) ·
[Remmina](https://gitlab.com/Remmina/Remmina) ·
[xrdp](https://github.com/neutrinolabs/xrdp)
