# Session Model

The session layer is the orchestration boundary for `FusionDesk`. It owns lifecycle, policy evaluation, network attachment, module composition, health state, and diagnostics.

The session layer does not capture screens, render frames, encode media, read devices, authenticate sockets, or implement NAT traversal. Those responsibilities belong to modules, network transports, or services.

## Session Types

```text
ClientSession
AgentSession
AuthSession
RelaySession
StandaloneSession
```

`ClientSession` and `AgentSession` are the first implementation targets. `AuthSession` coordinates login and capability negotiation. `RelaySession` is reserved for tunnel/relay work. `StandaloneSession` is for module tests and local validation.

## Runtime Ownership

```text
RuntimeHost
  -> SessionManager
    -> Session
      -> PolicyEngine
      -> NetworkManager
      -> ModuleHost
      -> DiagnosticsSink
```

Ownership rules:

```text
RuntimeHost owns process startup and product profile selection.
SessionManager owns session creation, lookup, reconnect, and teardown.
SessionMainline owns the minimum startup sequence above them: create a role session, bind ready channels supplied by link adapters, mount ProductProfile modules, build LinkChannelBindingReport, start ModuleHost, and return one report. It can also continue an existing session after Qt or tunnel adapters have bound concrete channels.
Session owns one remote interaction lifecycle.
NetworkManager owns sockets and channel groups for that session.
ModuleHost owns module attach, start, stop, detach.
PolicyEngine owns allow/deny decisions.
DiagnosticsSink owns structured session and module events.
ProductProfile minimum channel specs enter through SessionCreateOptions and are registered in the session NetworkManager before module mount.
Channel spec registration is not readiness; transport adapters still bind concrete channels and mark them ready.
```

## Session Lifecycle

```text
Created
Authorizing
Authorized
Connecting
NetworkReady
MountingModules
Running
Reconnecting
Draining
Stopping
Stopped
Failed
```

Transitions:

```text
Created -> Authorizing
Authorizing -> Authorized
Authorized -> Connecting
Connecting -> NetworkReady
NetworkReady -> MountingModules
MountingModules -> Running
Running -> Reconnecting
Running -> Draining
Running -> Stopping
Reconnecting -> NetworkReady
Draining -> Stopping
Stopping -> Stopped
any state -> Failed
```

The session must fail closed. If policy, license, module dependency, or mandatory channel setup fails, the affected module is denied or stopped. The whole session only fails when a required base capability fails.

## Session Context

Minimum session context:

```text
session id
user id
tenant id
client device id
agent device id
local platform
remote platform
session role
requested feature set
licensed feature set
policy feature set
allowed feature set
transport mode
security mode
policy version
```

This context is immutable after authorization except for reconnect counters, health state, and runtime feature toggles.

## Authorization Loop

```text
requested features
  -> license grant
  -> enterprise policy
  -> device policy
  -> user role
  -> peer capability
  -> allowed features
```

The session stores both requested and allowed features. Modules read only the effective decision produced by `PolicyEngine`.

Policy output:

```text
allowed
denied reason
effective feature mask
transport constraints
audit requirements
module configuration overrides
```

## Module Composition

The session does not hard-code module constructors. Product profiles select module manifests.

Example profile:

```text
remote-desktop-suite
  display.screen
  input.keyboard
  input.mouse
  filesystem.redirect
  printer.redirect
  audio.desktop
  audio.microphone
```

Composition sequence:

```text
load module manifests
resolve role-specific module variants
resolve dependencies
register required network channels
attach modules
check required channel readiness
evaluate policy for each module
start allowed modules through ModuleHost
activate module ingress for successfully running modules
record denied modules
```

Module denial is not an exceptional state. It is a normal policy result and must be visible in diagnostics.

Current G4 loop:

```text
feature request or profile module
  -> Session
  -> ModuleHost role/platform gate
  -> ModuleHost dependency gate
  -> ChannelRegistry readiness gate
  -> PolicyEngine module authorization
  -> module start
  -> ModuleIngressRegistry route activation
  -> DiagnosticsSink event
```

Failure paths:

```text
unsupported role or platform -> reject before attach
missing required module -> deny start with MissingDependency
required channel not ready -> deny start with MissingDependency
policy deny -> deny start with policy reason
ingress activation failure -> unregister partial routes, stop module, publish diagnostics
```

## Runtime Feature Toggle

Dynamic enable/disable must flow through the session:

```text
control packet
  -> Session
  -> PolicyEngine recheck
  -> ModuleHost start or stop
  -> Network ingress register or unregister
  -> DiagnosticsSink event
```

Modules must not directly toggle other modules.

## Runtime Feature Pumps

Feature runtime pumps sit above started modules and below apps. They do not own
protocol payload schemas and they do not bypass ModuleHost.

Current rule:

```text
PC shell or runtime/qt creates adapter dependencies.
RuntimeHost injects those dependencies into role-scoped modules.
ModuleHost starts modules only after policy and required-channel gates pass.
LinkChannelBindingReport is the runtime/session report that explains module-required channel readiness before ModuleHost start.
SessionRuntimeDiagnosticsSnapshot is the runtime/session reader that combines SessionSnapshot, LinkChannelBindingReport, mounted/running module counts, blocked-channel count, and session-scoped diagnostics after or before start.
runtime/feature/FeatureRuntimeService polls pure adapter interfaces and calls module APIs.
modules/input remains the owner of FDIN packet construction.
runtime/feature/FeatureRuntimePolicy authorizes and audits operations before module APIs send traffic.
```

The first implementation drives client input polling, can own input capture
open/close when it is the active pump owner, and can process policy/audit before
module send. Clipboard is deferred to a later data-redirection rebuild.
Concrete global/raw input capture adapters and enterprise content policy mapping
remain later work.

## Session Runtime Diagnostics

`SessionRuntimeDiagnosticsSnapshot` is the stable query surface for product
UI/service diagnostics. It is intentionally above `ChannelRegistry` and
`ModuleHost` internals, and below Qt/app code.

It contains:

```text
session id and session state
SessionSnapshot
LinkChannelBindingReport
linkReady
blockedChannels
mountedModules
runningModules
display capture status rows
display codec status rows
session-scoped DiagnosticEvent values
messages
```

`ok` means the snapshot could be built for a valid session and the snapshot
itself has no construction messages. It does not mean the link is healthy;
callers must read `linkReady` and `blockedChannels` for channel health.

The snapshot must not create sockets, mount modules, start modules, or mutate
policy. UI/service code should consume it instead of parsing PC shell stderr or
walking lower-level registries directly.
The current PC shell exposes this reader with `--print-session-diagnostics` as
line-oriented stdout for smoke scripts and future UI/service integration.
For display sessions, `buildDisplayProductDiagnostics` is the pure runtime
summary reader above this snapshot. It folds session, link/channel, display
module, capture, and codec rows into `DisplayProductDiagnosticsSnapshot`, with
`ok`, `warning`, `degraded`, or `blocked` health and a usable flag for future
PC UI/service owners. `buildDisplayProductHealthPresentation` then turns that
snapshot into stable product-facing status/action/capture/codec state codes so
the UI/service layer can localize and react without parsing shell text.

## Reconnect Model

Reconnect preserves session identity and module state where possible.

Reconnect stages:

```text
mark network degraded
pause realtime senders
keep module logical state
create new socket group
rebind channels
replay active ingress routes for running modules
request keyframe for display
resume modules
publish reconnect result
```

Realtime modules such as display should request a fresh keyframe after reconnect. Non-realtime modules such as filesystem and future clipboard should resume after channel readiness.

Reconnect boundary:

```text
ChannelRegistry owns degraded, ready, and rebound channel state.
NetworkRouter owns subscription tokens and packet dispatch.
ModuleIngressRegistry owns which running-module subscriptions are active.
Session coordinates pause, replay, module notification, and diagnostics.
Stopped or denied modules must not be replayed after reconnect.
```

## Android Lifecycle

Android Client sessions must handle app and surface lifecycle without leaking platform details into core.

Lifecycle events:

```text
Activity created
Activity resumed
Activity paused
Activity destroyed
Surface created
Surface changed
Surface destroyed
Network changed
```

Rules:

```text
Activity pause does not automatically disconnect the session.
Surface destroyed pauses rendering and keeps the session logical state.
Surface recreated reattaches the display renderer and requests a keyframe.
Network change enters reconnect flow through SessionManager.
Native controller close must detach modules before releasing JNI and Surface references.
```

## Diagnostics Contract

Every session event should include:

```text
session id
module id when applicable
channel id when applicable
severity
event code
message
monotonic timestamp
policy version when applicable
```

Required event groups:

```text
session lifecycle
policy decisions
network channel state
module lifecycle
packet routing errors
reconnect events
display capture/render health
```

## First Delivery Gate

The session layer is ready for the first display module when:

```text
ClientSession and AgentSession contexts exist.
NetworkManager can expose a video channel group.
PolicyEngine can authorize display.screen.
ModuleHost can attach server and client display modules.
ModuleIngressRegistry can register VIDEO and PAYLOAD_ACK routes.
ModuleHost activates ingress only after module start succeeds.
ModuleHost unregisters ingress when modules stop.
Diagnostics record module start, channel readiness, and packet routing.
```
## Reconnect Note

`ReconnectRequest` now carries optional affected channel keys, and `Session::reconnect` marks those channels degraded before the session returns to `Running`. This is still not full production reconnect orchestration; it only records the first explicit reconnect channel-state step.
Reconnect now also emits `display.keyframe_requested` when that behavior is enabled.
It now also passes through `Draining` and emits `module.reconnect_paused` / `module.reconnect_resumed` diagnostics while reconnect completes.
`ModuleHost` now dispatches optional `IReconnectAwareModule` pause/resume hooks for running modules whose channel bindings match the affected reconnect channels. Pause runs in stop order; resume runs in start order. Display client resume can request a fresh keyframe through the existing PAYLOAD_ACK Request/Response path.
`ReconnectRequest` can now also carry already-created replacement channels. `Session::reconnect` uses `NetworkManager::rebindChannel` to swap the logical channel binding and mark it ready after module pause. runtime/qt can prepare local Qt TCP replacement channels from connect profiles or accepted sockets, expose a one-call API that delegates the actual rebind back to Session, and close/remove the old active Qt transport after that one-call reconnect succeeds. PC shell delayed `--reconnect-profile` now routes through QtReconnectRuntimeService and ReconnectCoordinator in client-replacement mode. `PeerProfileService` now handles control-channel peer profile exchange below runtime/connection; Session still consumes only already-created replacement channels and remains uninvolved in profile serialization, service dispatch, socket creation, PC UI reconnect startup wiring, and cross-process teardown.
After channel rebind, `Session::reconnect` asks `ModuleHost` to replay active ingress routes for running modules whose consumed channel bindings match the affected channels. Replay results are recorded in `SessionSnapshot.lastReconnect.replayedIngress` and failed replay marks the reconnect report as not ok.
`SessionSnapshot.lastReconnect` now carries a structured reconnect report with per-channel degrade/rebind results, module pause/resume reports, ingress replay reports, reconnect count, reason, and fresh-state intent. Diagnostics remain the event stream; the snapshot report is the queryable reconnect result.
`ReconnectRuntimeServiceSnapshot::diagnostics` now aggregates the service-level reconnect view: orchestration plan, replacement executor stages, optional `Session::lastReconnectReport`, teardown dispatch state, pending request count, and timeout expiry. UI/runtime consumers should read that report instead of reconstructing reconnect status from scattered diagnostics text.
`QtRuntimeTransportManager` can now load a reconnect profile after initial startup and drive reconnect from the same TCP profile shape, and runtime listeners can treat a second connection on an already bound channel as reconnect adoption instead of a fresh startup bind.

## Service-Backed Reconnect Planning

`runtime/connection` now has a pure C++ reconnect orchestration plan. The request combines peer profile exchange input, affected `ChannelKey` values, a reconnect reason, and the fresh-keyframe intent. The result is a client side plan, an agent side plan, and an explicit `teardownAfterSuccessfulRebind` list.
`runtime/qt` now consumes the local TCP form of that plan through `QtReconnectOrchestrator`. It starts reconnect-aware agent listeners and client replacement reconnects, but `Session::reconnect` remains the only code path that marks degraded channels, rebinds logical channels, replays ingress, and records `lastReconnect`.

Rules:

```text
SessionManager remains the reconnect entry point.
Session::reconnect remains the only logical rebind point.
The planning layer does not create sockets, concrete IChannel instances, JSON files, or Qt objects.
Runtime or transport adapters translate side plans into replacement channels.
Modules only observe pause, ingress replay, resume, and fresh-state requests.
Stopped or policy-denied modules are not replayed.
```

Failure semantics:

```text
empty degraded channel list -> plan rejected
unknown degraded channel -> plan rejected
degraded channel without replacement profile -> plan rejected
duplicate degraded channel -> plan rejected
replacement preparation failure -> do not call Session::reconnect
Session rebind failure -> lastReconnect.ok is false and old logical binding is not treated as successfully replaced
ingress replay failure -> lastReconnect.ok is false
old concrete transport teardown failure -> runtime/adapter diagnostic; core session does not own concrete socket close
```
