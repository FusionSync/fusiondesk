# RuntimeHost And SessionManager Design

This document turns the session model into implementable runtime classes.

## Runtime Ownership

```text
App shell
  -> RuntimeHost
    -> ProductProfile
    -> SessionManager
      -> Session
        -> NetworkManager
        -> PolicyEngine
        -> ModuleHost
        -> DiagnosticsSink
```

Apps may create `RuntimeHost` and submit user intents. Apps must not create feature modules or concrete channels directly.

## RuntimeHost

Responsibilities:

```text
load runtime options
load product profile
initialize logging and diagnostics
create session manager
create service registry
bridge event loop and thread executors
shut down sessions in order
```

Design contract:

```cpp
class RuntimeHost {
public:
    Result initialize(const RuntimeOptions& options);
    SessionManager& sessions();
    ServiceRegistry& services();
    DiagnosticsSink& diagnostics();
    ProductProfile profile() const;
    void shutdown(const ShutdownReason& reason);
};
```

`RuntimeHost` may have a Qt-backed implementation, but the core runtime contract must not expose Qt types.

## SessionManager

Responsibilities:

```text
create sessions
lookup sessions
coordinate reconnect
coordinate teardown
route app-level control requests to sessions
publish session inventory diagnostics
```

Design contract:

```cpp
class SessionManager {
public:
    SessionId createClientSession(const ClientSessionOptions& options);
    SessionId createAgentSession(const AgentSessionOptions& options);
    ISessionController* find(SessionId id);
    Result reconnect(SessionId id, const ReconnectRequest& request);
    Result close(SessionId id, const StopReason& reason);
    std::vector<SessionSnapshot> snapshots() const;
};
```

SessionManager owns session objects. Modules never own sessions.

## Session Base

Responsibilities:

```text
state transitions
authorization loop
network setup
module composition
feature toggles
reconnect coordination
stop order
diagnostics
```

Session variants:

```text
ClientSession
AgentSession
AuthSession
RelaySession
StandaloneSession
```

Initial implementation targets:

```text
ClientSession
AgentSession
StandaloneSession for tests
```

## Session Context V1

Minimum fields:

```text
session id
trace id
tenant id
user id
client device id
agent device id
local platform
remote platform
local CPU arch
remote CPU arch
session role
transport mode
security mode
requested feature set
licensed feature set
policy feature set
allowed feature set
peer capability set
policy version
protocol version
created monotonic timestamp
```

Runtime-mutating fields must be separated from immutable authorization context:

```text
reconnect counter
health state
dynamic feature toggles
channel readiness
module runtime state
```

## Startup Sequence

```text
RuntimeHost.initialize
  load ProductProfile
  create SessionManager
  register service providers
  register module factories

SessionManager.createClientSession
  allocate SessionId
  create SessionContext
  create PolicyContext
  create NetworkManager
  register ProductProfile minimum channel specs when supplied by SessionCreateOptions
  create ModuleHost with NetworkManager.router and NetworkManager.registry
  create DiagnosticsSink scope

Session.start
  Authorizing
  authorize requested features
  Connecting
  open required socket group
  NetworkReady
  register required channels
  MountingModules
  resolve role-specific module graph from ProductProfile and ModuleCatalog
  attach allowed modules
  check required channel readiness
  start modules through policy gates
  activate ingress for running modules
  Running
```

Current implementation note:

```text
RuntimeHost exposes ProductProfile defaults and can mount required profile modules into a running session through ModuleComposer.
SessionMainline now provides the pure C++ minimum startup frame above RuntimeHost and SessionManager. It creates a role session, binds adapter-created IChannel instances, marks ChannelRegistry readiness, mounts ProductProfile modules, starts them through ModuleHost gates, and returns channel/link/mount/start reports for PC shell or service owners.
SessionMainline also has a continuation path for already-created sessions. PC shell uses it to create/start the role session first, let QtRuntimeTransportManager bind or listen concrete transports, then mount/start ProductProfile modules through the same mainline instead of open-coding SessionManager and ModuleHost calls.
LinkChannelBindingReport exists under runtime/session as the product-level startup report above ChannelRegistry snapshots. It marks module-required logical channels as registered, listening, bound, ready, degraded, failed, or blocked before ModuleHost start.
SessionRuntimeDiagnosticsSnapshot exists under runtime/session as the stable product diagnostics reader above SessionSnapshot and LinkChannelBindingReport. It exposes session state, link readiness, blocked-channel count, mounted/running module counts, and session-scoped DiagnosticEvent values without Qt, app, or shell-stderr dependencies.
DisplayProductDiagnosticsSnapshot/buildDisplayProductDiagnostics is the display-specific reader above SessionRuntimeDiagnosticsSnapshot. DisplayProductHealthPresentation/buildDisplayProductHealthPresentation adds stable product-facing status/action/capture/codec state codes on top of that reader. Together they let product UI/service code present display health without calling display module internals or parsing PC shell stdout.
PC shell `--print-session-diagnostics` emits that reader as stable stdout records at session-created, transport/listen-profile-applied, profile-mounted, profile-started, reconnect-complete, blocked-start, and exit phases.
SessionCreateOptions can carry ProductProfile minimumChannels into SessionManager so NetworkManager has the required specs before modules are mounted.
minimumChannels registration does not mark channels ready; transport adapters must bind concrete channels and mark readiness.
QtSessionTransportConnector exists under runtime/qt and can create/adopt Qt TCP transports from profile entries, then drive QtChannelBinder to mark channels ready.
QtRuntimeTransportManager exists under runtime/qt and owns QtSessionTransportConnector instances and QTcpServer listeners per RuntimeHost sessionId.
QtRuntimeTransportManager can prepare reconnect replacement channels from Qt TCP connect profiles or accepted sockets and can drive Session reconnect in one call, while leaving ChannelRegistry rebind and ready state to Session reconnect. After a successful one-call reconnect it closes and removes the previous active Qt transport for that channel.
QtRuntimeTransportManager can also load a reconnect profile after initial startup and trigger reconnect from the same JSON transport shape.
If a runtime listener accepts a second connection for an already bound channel, runtime/qt treats it as reconnect adoption instead of a fresh startup bind.
runtime/qt can generate matching client-connect and agent-listen profile pairs, serialize and save them to JSON, load Qt TCP transport profile JSON files, resolve tcpChannels and tcpListenChannels against known ChannelSpec entries, and apply connect/listen profiles through manager calls.
QtTcpPeerProfileCoordinator exists under runtime/qt and can generate a local multi-endpoint TCP peer profile plan into separate client-connect and agent-listen JSON files for PC shell startup. `fusiondesk_pc_profile_plan` exposes that local path as a startup-script CLI; PC shell startup can now invoke the FDPP service owner after control bootstrap, but this does not replace production PC UI service invocation or reconnect service execution.
runtime/connection now provides the pure C++ ChannelSpec-backed peer connection plan resolver used before Qt-specific TCP endpoint validation, plus `PeerProfileService` as the production-facing control-channel Request/Response boundary for service-backed peer profile exchange. Tunnel/P2P planning can feed the same contract without moving socket creation into Session.
General-purpose profile loading remains explicit-session-id based; PC shell startup uses a single-session binding path so the file does not need to know the RuntimeHost-generated session id.
Listen profile application reports listeningChannels, not channel readiness; ChannelRegistry ready state appears only after a concrete accepted transport is adopted.
`NetworkRouter::submitIncoming` uses a snapshot of matching handlers so reconnect replay or subscription changes during packet handling do not invalidate iteration.
QtTimerBridge exists under runtime/qt and can drive RequestTracker timeout expiry from QTimer without Qt entering core.
QtEventLoopBridge exists under runtime/qt and can post callbacks, process events, and run until a predicate through QCoreApplication without Qt entering core.
PC client and agent shells create QCoreApplication, initialize RuntimeHost, create role-specific sessions through SessionMainline, optionally apply `--transport-profile` or `--listen-profile`, and support `--smoke` exit.
PC client and agent shells can explicitly mount role-specific display adapter dependencies with `--mount-display`; `--start-display` waits for required module channels with `--wait-channels-ms` using LinkChannelBindingReport before calling SessionMainline module start.
PC shells support `--run-ms` for bounded smoke/e2e event-loop execution after startup.
PC shells support `--require-display-frame` to fail bounded runs when display agent/client counters do not show sent/rendered frame progress.
PC shells support `--reconnect-profile`, `--reconnect-after-ms`, and `--reconnect-reason` to drive a reconnect after startup from the same loaded transport-profile shape.
runtime/feature now provides FeatureRuntimeService as the pure C++ owner for feature pumping above started modules. It polls IInputCapture and calls InputClientModule send APIs.
PC shells can start that owner with `--pump-profile-modules`; the QTimer that repeatedly calls `pumpOnce` stays in the PC shell Qt boundary.
runtime/feature now also exposes `FeatureRuntimePolicy`. The service can authorize/audit feature operations before module APIs emit traffic and can own the input capture open/close lifecycle when it is the runtime pump owner. Clipboard is deferred to a later data-redirection rebuild.
Tests prove RuntimeHost-mounted display modules can render the first frame and complete keyframe request/response over Qt TCP through JSON-loaded QtRuntimeTransportManager profile data with VIDEO on main_screen and PAYLOAD_ACK on small_data; tests also prove an agent listener can accept and adopt an inbound Qt TCP channel into ChannelRegistry ready state, coordinator-generated multi-endpoint profile files can mark distinct control/video channels ready, runtime/qt can prepare or directly drive reconnect replacement channels through Session::reconnect while closing the old active Qt transport after rebind, and the PC shell can execute a delayed reconnect profile after startup.
ReconnectRuntimeService snapshots now include `diagnostics`, a single pure C++ lifecycle report combining coordinator plan/replacement stages, optional Session rebind report, teardown pending state, and timeout expiry for runtime/UI readers. PC shell `--print-reconnect-diagnostics` exposes that report at reconnect completion/failure and exit lifecycle phases.
PC shell `--transport-profile`, `--listen-profile`, required-channel-gated `--start-display`, `--reconnect-profile`, `--print-session-diagnostics`, `--print-reconnect-diagnostics`, fusiondesk_pc_profile_plan generated bounded two-process control/small_data/main_screen startup, and client rendered-frame smoke are implemented for the current shell session; production PC UI invocation of `PeerProfileService`, peer coordination service execution, production UI diagnostics consumption, and UI lifecycle orchestration remain pending.
Core RuntimeHost/Session startup remains transport-agnostic and does not auto-load Qt profiles.
Session creates an empty ModuleHost and injects its NetworkRouter and ChannelRegistry.
ModuleHost gates only modules that have already been added to it.
ModuleHost activates ingress after successful module start and unregisters it before stop.
Session.start does not yet automatically instantiate every ProductProfile module family.
```

## Stop Sequence

```text
mark session Stopping
unregister module ingress
stop realtime modules
stop data redirection modules
close network channels
flush diagnostics
detach modules
release platform handles
mark session Stopped
```

Realtime modules stop before network close so they can stop producing packets cleanly.

## Reconnect Sequence

```text
detect socket failure
mark affected channels degraded
publish diagnostics
pause realtime senders
create replacement socket
re-init affected channels
rebind ChannelRegistry
replay active ingress subscriptions for running modules
notify modules
display requests keyframe
resume allowed modules
publish reconnect result
```

Reconnect is channel-based. The whole session should not restart when only one socket fails.

## Dynamic Feature Toggle

All runtime enable/disable flows go through Session:

```text
control packet or app request
  -> Session.setFeatureEnabled
  -> PolicyEngine recheck
  -> ModuleHost start or stop
  -> ModuleIngressRegistry register or unregister
  -> DiagnosticsSink publish
```

Modules must not directly start or stop other modules.

G4 to G5 runtime sequence:

```text
ProductProfile requests display.screen.
ModuleComposer asks DisplayModuleFactory to resolve display.screen for the session role.
DisplayModuleFactory creates display.screen.agent for AgentSession.
DisplayModuleFactory creates display.screen.client for ClientSession.
ModuleHost applies role, platform, dependency, channel, and policy gates.
ModuleIngressRegistry activates VIDEO or PAYLOAD_ACK routes only after start succeeds.
ModuleComposer topologically orders selected module dependencies.
ModuleHost preserves add order so composition order is preserved during start/stop.
ModuleHost exposes structured ModuleSnapshot values.
SessionSnapshot carries ModuleHost module snapshots for runtime diagnostics.
```

## Threading Model

Core contracts are thread-aware but not tied to Qt.

Recommended executors:

```text
main executor: app and UI callbacks
network executor: socket IO and packet parsing
media executor: encode/decode and frame scheduling
bulk executor: filesystem, printer, clipboard file payloads
diagnostics executor: buffered event publishing
```

Rules:

```text
session state transitions are serialized
module lifecycle calls are serialized per session
network ingress can arrive from network executor but dispatches through router contract
render surface callbacks return to the UI or render executor selected by platform adapter
```

## Product Profile

A product profile selects modules and feature defaults.

Example:

```text
remote-desktop-display-mvp
  required:
    display.screen
  optional:
    diagnostics.base
  denied until later:
    clipboard.redirect
    filesystem.redirect
    printer.redirect
    audio.desktop
    audio.microphone
```

Profile data should include:

```text
profile id
required modules
optional modules
default feature mask
minimum channel set
platform allowlist
policy bundle id
diagnostics level
```

## Diagnostics

Required runtime events:

```text
runtime.initialized
session.created
session.state_changed
policy.features_authorized
policy.module_denied
network.channel_ready
network.channel_pressure_changed
network.reconnect_started
network.reconnect_finished
module.attached
module.started
module.stopped
module.failed
display.first_frame
display.keyframe_requested
display.keyframe_sent
```

Diagnostics must include `session id`, `trace id`, `module id` when applicable, `channel key` when applicable, and monotonic timestamp.
## Peer Profile Exchange

runtime/connection now also provides a pure C++ peer profile exchange pair builder that turns a resolved plan into client/agent profile pair data without Qt or sockets.
`PeerProfileService` is the production-facing service boundary for remote profile exchange. It encodes and decodes `FDPP` payloads inside control-channel `PacketEnvelope` Request/Response messages, validates request correlation fields, ignores unrelated control payloads, resolves through the pure C++ planner, and returns terminal Response/Error packets. RuntimeHost and Session remain transport-agnostic; PC shell and UI startup still need wiring to start and call the service automatically.
## Reconnect Note

Session reconnect now accepts affected channel keys and marks those channels degraded before resuming the session, but it still does not perform production socket replacement.
Reconnect now also emits `display.keyframe_requested` when the session chooses to request a fresh frame after channel degradation.
It also uses `Draining` as a reconnect transition and publishes module pause/resume diagnostics around the network rebound step.
`ModuleHost` now owns the module notification fan-out for reconnect: it pauses optional reconnect-aware running modules in reverse module order and resumes them in start order, filtered by affected channel bindings. Session still owns the orchestration point and does not directly walk modules.
Session reconnect can now consume already-created replacement `IChannel` instances and call `NetworkManager::rebindChannel` before returning the session to `Running`. runtime/qt can prepare the first concrete Qt TCP replacement channels from connect profiles or accepted sockets and can call Session reconnect for those replacements while keeping the actual rebind in Session.
`ModuleHost` now replays active ingress routes for running modules after channel rebind, filtered by affected channel bindings. `SessionSnapshot.lastReconnect.replayedIngress` records the replayed module id, token count, diagnostics, and replay success.
`SessionSnapshot` now includes `lastReconnect`, a structured report for the latest reconnect attempt. It records overall success, per-channel degrade/rebind reports, ingress replay reports, pause/resume module reports, reconnect count, reason, and fresh-state intent so callers do not need to infer reconnect outcome from diagnostics text.
## Peer Coordination Note

`runtime/connection` now also has a pure C++ peer coordination planner that keeps profile exchange and reconnect hints in one place before runtime/qt turns them into startup files. `fusiondesk_peer_coordination_tests` covers the core planner.

## Service Reconnect Orchestration Plan

The first service-backed reconnect contract is a pure planning layer under `runtime/connection`. It resolves a service-selected peer profile exchange plus degraded channels into:

```text
client replacement tcpChannels
agent replacement tcpListenChannels
affected degraded ChannelKey list
teardownAfterSuccessfulRebind ChannelKey list
reason
requestDisplayKeyframe flag
```

Runtime flow:

```text
app/UI reconnect intent
  -> RuntimeHost service or coordinator
  -> runtime/connection reconnect orchestration plan
  -> runtime/qt or future tunnel runtime prepares replacement IChannel instances
  -> SessionManager::reconnect
  -> Session::reconnect performs degrade, module pause, NetworkManager rebind, ingress replay, and module resume
  -> runtime/adapter closes old concrete transports only after successful session rebind
```

Current implementation includes the common planning contract, local Qt execution path, pure old-transport teardown command/envelope/dispatch/handler/terminal-response summary contract, the QtRuntimeTransportManager close-target adapter, the first runtime service owner frame, the runtime reconnect diagnostics report, the PC shell diagnostics print surface, and the `PeerProfileService` control-channel exchange boundary. A production service registry, production PC UI service invocation, production PC UI diagnostics consumption, and UI lifecycle orchestration are still pending.
`TunnelReconnectExecutor` now defines the future relay/direct/P2P replacement executor contract below the same ReconnectCoordinator boundary. It preserves the Session-owned logical rebind rule; concrete tunnel negotiation, socket creation, policy selection, and service-registry ownership are still pending.
ReconnectCoordinator is now the pure C++ frame for the production service
owner to call: it resolves the plan, invokes replacement executor interfaces,
hands client reconnect to the runtime executor, and dispatches teardown through
a teardown executor. ReconnectRuntimeService owns that common frame together
with RequestTracker and ReconnectTeardownService. QtReconnectRuntimeService
wires the common owner to QtReconnectExecutor and QtTimerBridge. RuntimeHost/PC
UI still need to route command-line/UI reconnect events through this owner.

Qt execution path:

```text
ReconnectOrchestrationPlan
  -> QtReconnectRuntimeService
  -> ReconnectRuntimeService
  -> ReconnectCoordinator
  -> QtReconnectExecutor
  -> QtReconnectOrchestrator
  -> QtRuntimeTransportManager::listenReconnectTcpChannels for agent listeners
  -> QtRuntimeTransportManager::reconnectTcpChannels for client replacements
  -> listener accept calls SessionManager::reconnect for the agent side
  -> runtime/qt commits and closes old concrete transports only after Session rebind succeeds
```

The reconnect-aware listener stores the planned reason and fresh-keyframe flag. This keeps diagnostics and `SessionSnapshot.lastReconnect` aligned with the service plan instead of reporting a generic listener replacement reason.

Qt transport polling must iterate over `QtSessionTransportConnector::transports()` snapshots. Polling a Qt transport can process pending Qt events, including listener accepts that trigger reconnect and replace the connector's active transport list. Runtime code must not hold live internal transport-vector iterators across Qt event processing.

Post-rebind old-transport teardown uses a separate pure runtime/connection contract. `ReconnectTeardownCommand` carries the target ChannelKey as data plus `messageId`, `correlationId`, and `timeoutMs`. The first wire mapping sends it as a normal control-channel `PacketEnvelope` request with an `FDRT` payload carrying the target channel and reason. Future service/runtime execution must dispatch that request to the peer-side handler and wait for terminal `Response` or `Error`; `Ack Accepted` is not final completion.

Production dispatch must supply response message ids from the session
direction's allocator and surface invalid control-route configuration as a
diagnostic or setup failure instead of silently relying on helper defaults.
`ReconnectTeardownDispatcher` provides the current pure C++ dispatch boundary:
it sends request packets through `NetworkRouter`, registers pending requests in
`RequestTracker`, records `Ack`/`Progress` as interim responses, and summarizes
terminal `Response`/`Error` packets. `ReconnectTeardownHandler` provides the
peer-side pure C++ handler boundary: it subscribes to the control route, decodes
FDRT requests, ignores unrelated control payloads, rejects malformed requests,
calls `IReconnectTeardownCloseTarget`, and sends terminal Response/Error. The
QtRuntimeTransportManager implements that close-target interface for Qt TCP
sessions. ReconnectTeardownService now binds the dispatcher, response
subscriptions, peer handler, and RequestTracker timeout expiry behind one pure
C++ execution boundary, and QtTimerBridge can drive that timeout expiry without
Qt entering runtime/connection. The remaining production work is binding this
service into the full remote reconnect coordinator and UI lifecycle reporting.
