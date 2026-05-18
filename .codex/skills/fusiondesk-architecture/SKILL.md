---
name: fusiondesk-architecture
description: >-
  Use when working on FusionDesk/HSR remote desktop architecture,
  module rebuild, Qt/C++ boundaries, session/network/policy design,
  Android controller library, CMake/qmake transition, or code review
  that must preserve the enterprise remote desktop architecture baseline.
---

# FusionDesk Architecture

Use this skill before designing or changing code in `E:\workspace\GIT_CODE\ASTUTE\Production-HSR2\HSR-Windows-src` when the work affects architecture, modules, runtime, network, policy, platform support, Android controller, or reference-based rebuild work informed by legacy `Source/`.

## Required Workflow

1. Read `docs/architecture/README.md`.
2. Read the smallest relevant canonical docs:
   - automatic execution: `GOAL_AUTOPILOT_PLAN.md`
   - startup method: `FUSIONDESK_PROJECT_INIT_METHOD.md`
   - current state: `FUSIONDESK_IMPLEMENTATION_BASELINE.md`
   - stages: `FUSIONDESK_STAGE_GATES.md`
   - module/interface design: `MODULE_AND_INTERFACE_BLUEPRINT.md`
   - session/runtime: `SESSION_MODEL.md`, `RUNTIME_HOST_AND_SESSION_MANAGER_DESIGN.md`
   - protocol: `PROTOCOL_MESSAGE_PATTERN.md`
   - network: `NETWORK_MODEL.md`, `NETWORK_CHANNEL_REGISTRY_AND_SCHEDULER.md`
   - display MVP: `DISPLAY_MODULE_DESIGN.md`
   - Qt/platform/Android: `QT_BOUNDARY.md`, `PLATFORM_TARGETS.md`, `ANDROID_CONTROLLER_LIBRARY.md`, `ABI_AND_TOOLCHAIN_MATRIX.md`
3. Treat legacy `Source/`, old `Build/`, old `Tools/`, and old `packaging/`
   as external-only reference material. They must not be reintroduced into
   this repository.
4. Preserve dependency direction:
   ```text
   apps -> runtime -> core
   runtime -> modules
   runtime -> adapters
   modules -> core interfaces
   adapters -> framework, transport, codec, or platform implementation
   platform -> OS services
   bindings -> external package surfaces
   ```
5. Keep `include/fusiondesk/core` and `src/core` pure C++.
6. Route feature behavior through session, network, module, and policy contracts.
7. Use the unified protocol envelope with request/response correlation for all non-fire-and-forget operations.
8. Before claiming completion, run the relevant verification command or clearly state what was not verified.

## Architecture Rules

- Modules own feature behavior.
- Network owns packet ingress, egress, channel readiness, priority, queue policy, pressure, and reconnect.
- Session owns lifecycle, module composition, feature toggles, reconnect orchestration, and diagnostics.
- Policy owns allow, deny, audit requirements, transport constraints, and module configuration overrides.
- Platform owns OS services.
- Adapters own Qt, concrete transport, codec, and OS integration.
- Apps are thin shells.

## Forbidden Moves

- Do not add Qt includes to core.
- Do not include legacy `Source/` headers from core.
- Do not let modules own concrete sockets.
- Do not let modules call app window private APIs.
- Do not hide policy decisions inside feature modules.
- Do not send request-like messages without `messageId`, `correlationId`, `timeoutMs`, and a response or error path.
- Do not put new code in `include/fusiondesk/module`, `network`, `policy`, `protocol`, `session`, or `src/module`, `network`, `policy`.
- Do not migrate old files by directory copy.
- Do not wrap old `Source` modules as the target implementation.
- Read old code only from an external archive or legacy repository for
  behavior, protocol, and platform reference.

## First Slice

Prefer `fusiondesk_core_display_mvp`:

```text
RuntimeHost
SessionManager
ClientSession
AgentSession
ChannelRegistry
PriorityScheduler
display.screen agent module
display.screen client module
fake transport tests
new display platform/framework adapters
```

Out of scope for the first slice:

```text
full tunnel/P2P
Android AAR production delivery
clipboard/filesystem/printer rebuild
codec rewrite
UI rewrite
```

## Verification Hints

Use these checks when relevant:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" include/fusiondesk/core src/core
rg -n "Source/|ThirdParty/" include/fusiondesk/core src/core
```

The `rg` purity checks should produce no matches.

## References

Use `references/baseline.md` for a compact summary when the full architecture docs are too large for the task.
