# FusionDesk Minimal Version Runbook

This runbook freezes the current minimum runnable version for the `FusionDesk`
rebuild. It is the entry point for proving the product mainline before deeper
attachment modules resume.

## Objective

The minimum version proves this closed loop:

```text
PC shell
  -> RuntimeHost
  -> SessionMainline creates a ClientSession or AgentSession
  -> QtRuntimeTransportManager binds or listens concrete TCP profile channels
  -> LinkChannelBindingReport gates module-required logical channels
  -> SessionRuntimeDiagnosticsSnapshot exposes the same link/module state to runtime readers
  -> SessionMainline mounts ProductProfile modules
  -> SessionMainline starts modules through ModuleHost policy/dependency/channel gates
  -> display.screen sends first frame over main_screen
  -> client renders first frame
  -> reconnect can replace main_screen and request a fresh keyframe
```

This is a framework/product-mainline proof. It is not a production feature
completion claim for clipboard, input, filesystem, printer, audio, camera,
peripheral, Android, relay, direct P2P, or NAT traversal.

## Build Commands

From the repository root:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The first command is only needed when `build` does not already exist or the
generator/toolchain changes.

## Required Verification

The minimum completion gate is:

```powershell
ctest --test-dir build -C Release --output-on-failure
rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" include/fusiondesk/core src/core include/fusiondesk/modules src/modules include/fusiondesk/runtime/session src/runtime/session
rg -n "Source/|ThirdParty/" include/fusiondesk/core src/core include/fusiondesk/modules src/modules include/fusiondesk/runtime/session src/runtime/session
uv run --python 3.12 --with pyyaml python C:\Users\gaoxi\.codex\skills\.system\skill-creator\scripts\quick_validate.py .codex\skills\fusiondesk-architecture
```

The `rg` commands should produce no matches. Exit code `1` from `rg` is
expected when there are no matches.

## Canonical Smoke Set

These tests are the current minimum product proof:

```text
fusiondesk_session_mainline_tests
fusiondesk_pc_client_smoke
fusiondesk_pc_agent_smoke
fusiondesk_pc_agent_listen_profile_smoke
fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
fusiondesk_pc_two_peer_start_display_smoke
fusiondesk_pc_peer_profile_start_display_smoke
fusiondesk_pc_client_display_mount_smoke
fusiondesk_pc_agent_display_mount_smoke
fusiondesk_pc_product_session_controller_tests
```

What each test proves:

```text
fusiondesk_session_mainline_tests
  RuntimeHost -> SessionMainline -> channel bind/ready -> ProductProfile mount/start.
  Also covers blocked LinkChannelBindingReport, SessionRuntimeDiagnosticsSnapshot, and continuation after external channel binding.

fusiondesk_pc_client_smoke / fusiondesk_pc_agent_smoke
  PC shells are thin QCoreApplication entry points that initialize RuntimeHost and create role sessions through SessionMainline.

fusiondesk_pc_agent_listen_profile_smoke
  Agent can bind a no-sessionId listen profile to the RuntimeHost-created session without marking channels ready until accept.

fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
  ProductProfile start refuses to start display when required logical channels are not ready.

fusiondesk_pc_two_peer_start_display_smoke
  Real PC agent/client executables use generated no-sessionId control/small_data/main_screen profiles, gate startup on channel readiness, render first frame, run a delayed reconnect through QtReconnectRuntimeService, and print reconnect plus session diagnostics.

fusiondesk_pc_peer_profile_start_display_smoke
  Real PC agent/client executables bootstrap only control by JSON, then negotiate small_data/main_screen by FDPP through QtPeerProfileRuntimeService before display startup.

fusiondesk_pc_client_display_mount_smoke / fusiondesk_pc_agent_display_mount_smoke
  PC shells inject role-specific display adapter dependencies through RuntimeHost profile mounting.

fusiondesk_pc_product_session_controller_tests
  Product-level PC controller creates a RuntimeHost session through SessionMainline, owns Qt transport/FDPP peer-profile/reconnect/display runtime lifetime, can own the first optional QWidget display window lifecycle, exposes display health and display runtime snapshots directly, mounts display modules, and preserves required-channel start blocking.
```

## Manual Minimal Run

The fully automated two-process smoke is preferred because it allocates free
ports. For manual local inspection, choose unused ports and use the same shape:

```powershell
$build = Resolve-Path build\Release
$agentProfile = Join-Path $env:TEMP "fusiondesk_min_agent.json"
$clientProfile = Join-Path $env:TEMP "fusiondesk_min_client.json"

& "$build\fusiondesk_pc_profile_plan.exe" `
  --client-profile $clientProfile `
  --agent-profile $agentProfile `
  --client-ready-prefix min-client `
  --agent-ready-prefix min-agent `
  --channel control=127.0.0.1:48001 `
  --channel small_data=127.0.0.1:48002 `
  --channel main_screen=127.0.0.1:48003

Start-Process `
  -FilePath "$build\fusiondesk_pc_agent.exe" `
  -ArgumentList @("--listen-profile", $agentProfile, "--start-display", "--require-display-frame", "--wait-channels-ms", "5000", "--run-ms", "8000") `
  -WindowStyle Hidden

& "$build\fusiondesk_pc_client.exe" `
  --transport-profile $clientProfile `
  --start-display `
  --require-display-frame `
  --wait-channels-ms 5000 `
  --run-ms 4000
```

Expected result:

```text
client exits 0
agent exits 0 after bounded run
display first frame is observed by counters
no required-channel blocked report is printed
```

For reconnect inspection, use the canonical
`fusiondesk_pc_two_peer_start_display_smoke`; it already asserts the
`reconnect.diagnostics` and `session.diagnostics` lines.

## Startup Policy

The minimal ProductProfile start is all-or-nothing:

```text
If any mounted required module channel is not ready,
SessionMainline reports the blocked modules and does not partially start the profile.
```

This is deliberate for the minimum product path. Later attachment modules can
add module-by-module enable/disable and retry flows, but those flows must not
weaken the first startup gate.

## Definition Of Done

The minimal version is considered healthy only when all of these are true:

```text
Release build succeeds.
Full CTest passes.
source purity scan has no Qt/Source/ThirdParty match in core/modules/runtime/session.
PC shell session creation enters through SessionMainline.
PC shell profile module mount/start enters through SessionMainline continuation.
LinkChannelBindingReport is available before module start.
SessionRuntimeDiagnosticsSnapshot is available for ready and blocked link/channel states.
PC shell can print SessionRuntimeDiagnosticsSnapshot as stable stdout records.
PcProductSessionController can expose SessionRuntimeDiagnosticsSnapshot, QtPeerProfileRuntimeServiceSnapshot, typed display.codec.v1 inventory/FDPP/codec-create/dependency-mount helper results, DisplayRuntimeServiceSnapshot, and DisplayProductHealthPresentation directly for product UI/service callers.
display two-peer smoke proves first frame.
FDPP smoke proves control bootstrap can negotiate small_data/main_screen.
reconnect smoke proves main_screen replacement and fresh keyframe request path.
docs and FUSIONDESK_TODO_TRACKER.md reflect the real state.
```

## Not In This Minimum

The following remain out of scope:

```text
production PC UI screens and service process wiring
product UI display-health/rendering orchestration beyond the current DisplayProductDiagnosticsSnapshot/DisplayProductHealthPresentation and PcProductSessionController surfaces
hardware encoder/decoder/render backends
real clipboard watching, file clipboard, stream chunking, and content policy
global/raw input capture and shortcut policy
filesystem, printer, audio, camera, gamepad, and peripheral behavior
Android JNI/AAR facade
relay/direct/P2P concrete sockets
NAT traversal candidate gathering
enterprise tunnel selection policy
production packaging
```

## Next Gate

After this runbook is current, the next architectural step is not deeper
clipboard/input behavior. The next mainline step is wiring product UI/service
owners to consume the same session runtime diagnostics, display health
summaries, and reconnect reports that PC shell can already print without
parsing shell stderr.
