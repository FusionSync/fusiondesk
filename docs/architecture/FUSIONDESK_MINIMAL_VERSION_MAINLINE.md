# FusionDesk Minimal Version Mainline

This document defines the minimum runnable product skeleton for the `FusionDesk`
rebuild. The current priority is the remote-desktop mainline, not deeper
clipboard, filesystem, printer, audio, camera, or peripheral behavior.

For exact build, smoke, diagnostics, and completion-gate commands, use
`FUSIONDESK_MINIMAL_VERSION_RUNBOOK.md`.

## Product Boundary

The minimum version must prove one closed product path:

```text
RuntimeHost
  -> SessionManager creates a role session
  -> SessionCreateOptions registers ProductProfile minimum ChannelSpec values
  -> concrete link/channel adapters bind IChannel instances
  -> ChannelRegistry marks required channels ready
  -> RuntimeHost mounts ProductProfile modules through ModuleComposer
  -> ModuleHost starts allowed modules through dependency, channel, and policy gates
  -> NetworkRouter carries module packets through the bound channel surface
  -> SessionSnapshot and diagnostics expose the running state
```

Everything outside that path is an attachment module. Attachment modules can
have shallow seams now, but production behavior must be added one module at a
time after the mainline is stable.

## Old Code Reference

The old code keeps a useful product lesson:

```text
CUserSession owns channel lookup by channel id and type.
AgentNetworkMgr/UserAuthNetworkMgr create predefined TCP channels.
SessionModuleHost registers module ingress against the session network.
Modules ask the session network to send; they do not own the socket set.
```

The new architecture keeps that shape but removes Qt and concrete TCP classes
from core contracts:

```text
ChannelRegistry replaces per-session channel arrays.
NetworkRouter replaces direct CTCPDataChannel signal wiring.
RuntimeHost + ModuleComposer replace ad hoc module creation.
SessionMainline is the pure C++ startup frame above those contracts.
QtRuntimeTransportManager and future tunnel factories remain adapter owners.
LinkChannelBindingReport replaces bool-only readiness checks at product startup,
so the runtime can say which logical channel is listening, bound, ready,
degraded, failed, or blocking a module.
```

## Current Minimal Code Frame

`runtime/session/SessionMainline` is the first pure C++ mainline owner. It does
not create sockets and does not parse Qt JSON profiles. It accepts already
created `IChannel` instances from link adapters, binds and marks them ready,
then mounts and starts the selected ProductProfile modules.

For PC shell startup, the mainline is split into two calls:

```text
SessionMainline::start(... mount=false, startModules=false)
  -> creates and starts the role session
QtRuntimeTransportManager applies transport/listen/FDPP profiles
SessionMainline::mountAndStart(... mount=true, startModules=false)
  -> mounts ProductProfile modules after adapter dependencies are available
wait for module-required channels with LinkChannelBindingReport
SessionMainline::mountAndStart(... mount=false, startModules=true)
  -> starts modules only after required channels are ready
```

The minimal ProductProfile start is intentionally all-or-nothing. If any
mounted required module channel is not ready, SessionMainline reports the
blocked modules and does not partially start the profile. Later attachment
module work can add module-by-module enable/disable flows without weakening
the minimum startup gate.

Inputs:

```text
RuntimeHost*
SessionRole
SessionCreateOptions
DisplayMvpDependencies for module dependency injection
SessionMainlineChannel list with IChannel + ChannelReadyInfo
LinkChannelBindingReportOptions with listening channel hints
start/mount/startModules switches
```

Outputs:

```text
session id and session pointer
channel bind/ready reports
LinkChannelBindingReport with registered/listening/bound/ready/degraded/blocked fields
ProfileMountReport
ModuleStartReport list
started/blocked module ids
messages and final ok flag
```

## Minimal Version Scope

P1 scope:

```text
display.screen over main_screen
control channel readiness
small_data readiness for input shell modules
large_data readiness for redirection shell modules
ProductProfile-driven multi-module loading
one high-level smoke for the whole mainline
PC shell migration to call SessionMainline instead of open-coding the same sequence
link/channel binding report before module start
```

Deferred attachment scope:

```text
production clipboard watching and file clipboard
production global/raw input capture and shortcut policy
filesystem, printer, audio, camera, gamepad, peripheral behavior
relay/direct/P2P socket implementations
Android AAR facade
production UI surfaces
```

## Stage Rule

Do not add more feature-module depth until this mainline is the normal startup
path for PC client and agent shells. New attachment work must reference old
module behavior first, then enter through module interfaces and policy gates.
