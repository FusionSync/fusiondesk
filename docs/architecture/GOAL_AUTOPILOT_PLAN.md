# Goal Autopilot Plan

This document defines how to run the `FusionDesk` rebuild with goal-driven automatic progress.

Use this when the user says:

```text
启动 goal
继续 goal
自动推进
继续 fusiondesk_core_display_mvp
```

The controller should continue through implementation, tests, verification, and documentation updates until the active goal is complete or a real blocker appears.

## Main Goal

```text
Complete fusiondesk_core_display_mvp:
build the new protocol, session, multi-channel network, module composition,
and display.screen first-frame loop.

The old Source tree is reference-only and must not become a runtime dependency.
```

## Goal Stack

Run goals in this order.

```text
G1 Protocol Core
G2 Network Core
G3 Session Runtime
G4 Module System
G5 Display MVP
G6 Real Transport And Qt Boundary
G7 Android Controller
G8 Tunnel And P2P
```

Do not jump to a later goal unless all required stage gates for earlier goals are met or explicitly waived.

## Goal State Machine

The autopilot derives the next action from three inputs:

```text
1. this document's current goal and exit evidence
2. FUSIONDESK_IMPLEMENTATION_BASELINE.md real implementation state
3. FUSIONDESK_STAGE_GATES.md required test and verification evidence
```

State transitions:

```text
NotStarted -> InProgress when the first contract or test for the goal lands.
InProgress -> Verifying when all goal work items have implementation evidence.
Verifying -> Complete only after CMake, CTest, purity scans, and skill validation pass.
Complete -> NextGoal after the implementation baseline and stage gates are updated.
Blocked when a product decision, missing SDK, or architecture violation prevents progress.
```

Completion rules:

```text
Do not treat target-state documentation as implementation evidence.
Do not mark a goal complete because a manifest exists if no runtime path uses it.
Do not enter G5 until G4 role-scoped display manifests and ingress lifecycle rules are explicit.
Do not enter G6 until G5 proves first-frame display through fake adapters.
```

## Closed-Loop Coding Mode

Default implementation mode is fast item closure, then stage audit. Do not keep
re-hardening one feature when the next bounded capability can be closed.

A closure item must have:

```text
one concrete capability or architecture contract
clear owned files/modules
one primary exit command or focused smoke
one documentation status update
```

Coding loop:

```text
1. implement the smallest useful item
2. run the focused build/test/smoke that proves that item
3. fix only failures that block that item
4. record status in the tracker or stage evidence
5. move to the next item
```

Rules:

```text
Do not run broad full-suite verification after every micro-change.
Do not repeatedly expand a feature's edge cases before its owner frame is closed.
If an optional adapter path fails after two focused repair attempts, isolate it behind its rollout gate, record a named follow-up, preserve the stable baseline, and continue.
Run the broad build, CTest, purity scans, and code review at stage end or before merge/commit.
Do not mark a stage complete until the broad audit passes or the missing evidence is explicitly recorded as a blocker.
```

H264 is retained. It is a near-term display codec closure lane, not something to
delete or permanently defer. Raw Windows-Windows display remains the stable
fallback while H.264 is completed behind its validation gate.

## G1 Protocol Core

Purpose:

```text
Make all messages structured, validated, correlated, timeout-aware, and serializable.
```

Already started:

```text
PacketEnvelope v1
MessageKind
PacketPriority
ResponseStatus
PacketFlags
RequestTracker
ProtocolValidator
PacketCodec / PacketSerializer
wire header
big-endian wire rule
payload length validation
header magic
header version
header CRC
payload CRC
capability negotiation payload
negotiated protocol limits
capability denial reasons
capability smoke tests
CapabilityExchange envelope helper
negotiated validator and codec options
ChannelAllowlistValidator boundary
```

Next work:

```text
G1 is complete for the current core slice.
Continue with G2 network rebind and fake transport work.
```

Exit evidence:

```text
fusiondesk_core builds.
ProtocolValidator tests pass.
RequestTracker tests pass.
PacketCodec tests pass.
CapabilityNegotiation tests pass.
CapabilityExchange tests pass.
ChannelAllowlist tests pass.
bad packet length is rejected.
unsupported protocol version is rejected.
request/response correlation is preserved after encode/decode.
malformed capability payload is rejected.
incompatible protocol major version is rejected during negotiation.
payload, stream chunk, timeout, channel, and pending request limits are negotiated.
CapabilityExchange packets carry capability payloads in request/response envelopes.
ProtocolValidator and PacketCodec can be configured from negotiated limits.
unnegotiated channel type, packet type, or message kind is rejected before routing.
```

## G2 Network Core

Purpose:

```text
Create channel readiness, scheduling, pressure, and reconnect behavior above the protocol.
```

Already started:

```text
ChannelSpec
ChannelKey
default MVP channel specs
ChannelRegistry
ChannelAllowlistValidator integration
PriorityScheduler
NetworkManager skeleton
ITransportSocket
SocketGroup
NetworkManager transport boundary
FakeChannel smoke tests
FakeTransportSocket smoke tests
channel readiness state
channel degraded state
channel rebind boundary
channel pressure state
default packet priority mapping
subscription preservation across rebind
```

Work items:

```text
G2 is complete for the current core slice.
Continue with G3 session runtime.
```

Exit evidence:

```text
control, small_data, and main_screen channels register.
required channel readiness is testable.
VIDEO is realtime priority.
PAYLOAD_ACK is interactive priority.
bulk packets cannot block critical or realtime packets.
failed channel can be marked degraded.
subscriptions survive channel rebind in tests.
fusiondesk_channel_defaults_tests covers control, small_data, and main_screen default specs.
fusiondesk_channel_registry_tests covers readiness, degraded state, pressure, and packet allowlist behavior.
fusiondesk_priority_scheduler_tests covers priority ordering and realtime keep-latest video behavior.
fusiondesk_network_manager_tests covers default registration, ready-gated enqueue, priority flush through router, screen channel rebind, and subscription preservation.
fusiondesk_network_manager_transport_tests covers ChannelSpec.socketClass routing into SocketGroup, realtime socket failure isolation, and control socket independence.
fusiondesk_socket_group_tests covers fake transport socket registration, open, write, duplicate rejection, and isolated socket failure.
```

## G3 Session Runtime

Purpose:

```text
Create the orchestration loop that owns session lifecycle, network, policy, modules, and diagnostics.
```

Already started:

```text
SessionContext v1
SessionState transition table
DiagnosticsSink
Session base
ClientSession
AgentSession
SessionManager
RuntimeHost
SessionMainline startup and continuation frame
LinkChannelBindingReport startup gate
ProductProfile defaults
NetworkManager ownership
ProductProfile minimum channel specs passed into SessionCreateOptions
PolicyEngine ownership
ModuleHost ownership
session lifecycle diagnostics
feature authorization
session reconnect lifecycle
```

Work items:

```text
G3 is complete for the current core slice.
Continue with G4 module system.
```

Exit evidence:

```text
RuntimeHost can initialize.
SessionManager can create and close ClientSession and AgentSession.
Session can authorize feature set.
Session creates NetworkManager, PolicyEngine, ModuleHost, and DiagnosticsSink.
SessionManager registers ProductProfile minimum channel specs into NetworkManager without marking them ready.
SessionMainline can create/start a role session, then later mount/start ProductProfile modules on the same session after concrete transport binding.
LinkChannelBindingReport combines ChannelRegistry snapshots with module-required channels before ModuleHost start.
Session lifecycle emits diagnostics.
fusiondesk_session_manager_tests covers ClientSession, AgentSession, authorization, ModuleHost creation, reconnect, diagnostics, and close.
fusiondesk_runtime_host_tests covers RuntimeHost initialize, ProductProfile defaults, SessionManager access, and shutdown.
```

## G4 Module System

Purpose:

```text
Make feature modules attach through manifest, role, policy, channel, and lifecycle contracts.
```

Already started:

```text
ModuleRuntime carries SessionContext, NetworkRouter, and ChannelRegistry.
Session creates ModuleHost with the session-owned ChannelRegistry.
ModuleHost rejects unsupported session role before attach.
ModuleHost rejects unsupported local platform before attach.
ModuleHost checks required module dependencies before start.
ModuleHost checks required channel readiness before start.
ModuleHost still routes policy authorization through IPolicyEngine before start.
display.screen.agent and display.screen.client role manifests.
ModuleCatalog role filtering for remote desktop suite.
ModuleHost activates ModuleIngressRegistry after successful module start.
ModuleHost unregisters module ingress before stop.
ModuleFactory and ModuleComposer contract.
ModuleComposer preserves ProductProfile order where dependencies do not constrain it, de-duplicates concrete module ids, topologically orders dependencies, and reports missing/dependency/cycle failures.
DisplayModuleFactory resolves display.screen into role-specific display agent/client modules.
RuntimeHost mounts display profile modules through ModuleComposer.
ModuleHost exposes structured ModuleSnapshot values.
SessionSnapshot carries ModuleHost module snapshots.
ModuleHost preserves addModule order for start, stop, manifest export, and snapshots.
fusiondesk_module_host_tests covers role, platform, dependency, channel readiness, allow-start paths, structured snapshots, and add-order startup.
fusiondesk_module_catalog_tests covers display agent/client manifest split and role filtering.
fusiondesk_module_ingress_tests covers manifest route registration, unregister, and re-register replacement.
fusiondesk_module_factory_tests covers alias resolution, de-duplication, missing factories, create failures, and dependency failure reporting.
```

Work items:

```text
ModuleManifest v2
role-scoped channel bindings
dependency resolution
required channel check before start
ModuleIngress lifecycle tied to start/stop
module diagnostics snapshot is present for current state evidence
```

Next work:

```text
Replace feature skeleton internals with real module contracts as modules are rebuilt.
Continue to G5 Display MVP for the current first-frame slice.
```

Current limitations:

```text
RuntimeHost can mount ProductProfile required modules through ModuleComposer; display.screen has real MVP behavior, input has first contract modules, clipboard is deferred to a later data-redirection rebuild, and the remaining non-display feature modules still have lifecycle-only skeleton factories.
Session.start still creates the ModuleHost; RuntimeHost mountProfileModules is the explicit profile-to-module entry point for the current slice.
ModuleComposer sorts dependencies before dependents; ModuleHost preserves the composition add order when starting modules.
```

Exit evidence:

```text
display.screen agent and client manifests are distinct or role-scoped.
ModuleHost resolves dependencies.
ModuleHost denies unsupported platform or role.
ModuleIngress unregisters on stop.
ModuleHost and SessionSnapshot expose structured module state snapshots.
module start cannot bypass policy or channel readiness.
fusiondesk_module_host_tests passes.
fusiondesk_module_catalog_tests passes.
fusiondesk_module_ingress_tests passes.
```

## G5 Display MVP

Purpose:

```text
Prove the first complete remote desktop path through FusionDesk.
```

Already started:

```text
DisplayAgentModule
DisplayClientModule
IDisplayCapture
IVideoEncoder
IVideoDecoder
IDisplayRenderer
DisplayPixelFormat, DisplayCaptureOpenOptions, and DisplayRenderSurface pure C++ seam types
RawFrameEncoder and RawFrameDecoder
fake capture/encoder/decoder/renderer in integration test
in-process bridge channel between agent and client NetworkRouter
VIDEO first-frame Event flow
PAYLOAD_ACK Request/Response keyframe recovery flow
display diagnostics events for first frame and keyframe recovery
basic display counter snapshots for captured, encoded, sent, dropped, received, decoded, rendered, client dropped, frame-gap, decode error, render error, and keyframe recovery counts
raw frame payload schema with width, height, stride, pixel format, frame id, keyframe flag, and timestamp
video pressure/drop behavior for congested delta frames
keyframe preservation while video channel is congested
display outbound packets stamp SessionContext sessionId and traceId so PacketCodec validation accepts real wire traffic
RuntimeHost profile-driven display module mount
DisplayModuleFactory profile-driven module creation
ModuleComposer-based profile mount path
fusiondesk_display_mvp_tests covers first frame, keyframe request, response, second keyframe render, pressure, diagnostics, basic counters, and profile-driven mount.
fusiondesk_display_frame_codec_tests covers raw frame schema roundtrip and invalid payload rejection.
fusiondesk_packet_codec_tests covers VIDEO Event NoResponseRequired + KeyFrame wire roundtrip.
CTest test targets undefine NDEBUG so Release test assertions execute.
```

Work items:

```text
real display capture and render adapters after Qt boundary work starts
```

Minimum vertical slice:

```text
1. fake capture, fake encoder, fake decoder, fake renderer
2. role-scoped display.screen.agent and display.screen.client manifests
3. AgentSession and ClientSession both start display modules through ModuleHost
4. agent emits VIDEO Event through the video channel
5. client receives VIDEO through ModuleIngressRegistry and renders first frame
6. client sends PAYLOAD_ACK Request for keyframe recovery
7. agent sends Response and emits a keyframe VIDEO Event
8. diagnostics record first_frame, keyframe_requested, and keyframe_sent
```

Exit evidence:

```text
Agent session starts display.screen.agent through ModuleHost.
Client session starts display.screen.client through ModuleHost.
Agent emits VIDEO event.
Client receives VIDEO through NetworkRouter.
Client renderer records first frame.
Client sends keyframe request with messageId/correlationId/timeout.
Agent responds with ResponseStatus::Ok or Error.
fusiondesk_display_mvp_tests passes.
```

## G6 Real Transport And Qt Boundary

Purpose:

```text
Connect the new core to real Qt transport and event loop code without leaking Qt into core.
```

Already started:

```text
optional fusiondesk_qt_adapters CMake target
optional fusiondesk_qt_runtime CMake target
qmake-derived Qt prefix discovery for CMake
PacketCodec frame inspection for TCP stream parsing
QtTcpTransportSocket implements ITransportSocket
QtTcpTransportSocket can connect, adopt accepted QTcpSocket instances, detach adopted sockets from their Qt parent before ownership transfer, write bytes, and deliver received bytes through an adapter callback
QtPacketChannel implements IChannel on top of QtTcpTransportSocket and PacketCodec
QtChannelBinder registers Qt transport sockets into SocketGroup, binds QtPacketChannel, and marks ChannelRegistry ready
QtSessionTransportConnector creates client-side Qt TCP transports from profiles and adopts server-side accepted QTcpSocket instances
QtRuntimeTransportManager owns QtSessionTransportConnector instances and QTcpServer listeners per RuntimeHost sessionId
Qt TCP transport profiles can be loaded from JSON files in runtime/qt and resolved against known ChannelSpec entries for tcpChannels and tcpListenChannels
runtime/connection can resolve pure C++ peer connection plans against known ChannelSpec entries before concrete transport adapters add endpoint-specific validation
runtime/connection can exchange peer profile plans/results through PeerProfileService over the control channel with PacketEnvelope Request/Response and FDPP payloads, without JSON, Qt, or sockets
runtime/connection can own caller-side peer profile exchange through PeerProfileRuntimeService, including FDPP request dispatch, response subscriptions, RequestTracker completion, timeout expiry, and snapshots without Qt, JSON, sockets, or Session state
runtime/qt can apply FDPP peer profile exchange results through QtPeerProfileRuntimeService, using QtRuntimeTransportManager for agent listen profiles and client connect profiles while keeping apps thin
runtime/connection can resolve pure C++ reconnect orchestration plans for service-selected degraded channels without Qt, sockets, or Session calls
runtime/connection can own a reconnect runtime service frame that composes ReconnectCoordinator, RequestTracker, ReconnectTeardownService, and teardown executor without Qt
runtime/qt can consume a reconnect orchestration plan through QtReconnectExecutor, which adapts the pure IReconnectReplacementExecutor boundary to QtReconnectOrchestrator, starts agent replacement listeners, initiates client replacement reconnect, and still delegates logical rebind to Session
runtime/qt can start a QtReconnectRuntimeService that owns QtReconnectExecutor plus QtTimerBridge and delegates common reconnect work to ReconnectRuntimeService
runtime/qt can generate matching client-connect/agent-listen peer profile pairs and serialize/save them to the canonical JSON profile shape
QtTcpPeerProfileCoordinator can generate, validate, and save local multi-endpoint TCP client-connect and agent-listen profile files from a channel connection plan
fusiondesk_pc_profile_plan can generate paired local client/agent profile JSON from named MVP channel plans for startup scripts
PC shell startup can bind no-sessionId Qt TCP transport profiles to the RuntimeHost session it just created
listen startup reports listeningChannels separately from readyChannels so a bound server socket is not confused with a ready feature channel
QtRuntimeTransportManager can load and apply Qt TCP transport connect/listen profile JSON files through startup-facing calls
QtTimerBridge wraps QTimer behind a standard C++ callback API and exposes monotonicNowUsec for request timeout ticks
QtEventLoopBridge wraps QCoreApplication post/process/runUntil behind a standard C++ callback API
PC client and agent QCoreApplication shells create RuntimeHost, create role-specific sessions, optionally apply --transport-profile or --listen-profile to the current shell session, and support --smoke exit
PC shell --start-display waits for required module channels with --wait-channels-ms and refuses display start if they are not ready
PC shell startup can use --peer-profile-service and --peer-profile-channel to negotiate non-bootstrap channels over FDPP after control-channel bootstrap
PC shell --print-reconnect-diagnostics exposes ReconnectRuntimeServiceSnapshot::diagnostics at reconnect completion/failure and exit lifecycle points
PC shell --run-ms supports bounded event-loop execution for smoke/e2e startup tests
PC shell --require-display-frame checks display sent/rendered counters after bounded startup runs
fusiondesk_qt_tcp_transport_tests proves PacketEnvelope Request/Response roundtrip through raw Qt TCP using PacketCodec
fusiondesk_qt_tcp_transport_tests also proves NetworkManager/SocketGroup/ChannelRegistry/NetworkRouter routing through QtChannelBinder and QtPacketChannel
fusiondesk_qt_tcp_transport_tests proves QtSessionTransportConnector profile-driven client connect and server adopt paths
fusiondesk_qt_tcp_transport_tests proves QtRuntimeTransportManager can apply no-sessionId profiles through the single-session path, listen from tcpListenChannels, accept a client TCP connection, adopt it into an agent session, mark ChannelRegistry ready, and route generic PayloadAck traffic.
fusiondesk_qt_tcp_transport_tests proves coordinator-generated multi-endpoint profile files can drive QtRuntimeTransportManager listen/connect and mark distinct control and video channels ready.
fusiondesk_qt_tcp_transport_tests proves RuntimeHost-mounted display agent/client sessions can render first frame and complete keyframe request/response over Qt TCP through JSON-loaded QtRuntimeTransportManager profiles with VIDEO on main_screen and PAYLOAD_ACK on small_data.
fusiondesk_peer_connection_plan_tests proves the pure C++ peer connection plan resolver accepts known ChannelSpec-backed plans and rejects missing, duplicate, unknown, or endpointless channel requests without Qt.
fusiondesk_peer_profile_service_tests proves the pure C++ control-channel PeerProfileService request/response envelope, FDPP payload roundtrip, service-side Response generation, and unrelated control payload ignore.
fusiondesk_peer_profile_runtime_service_tests proves PeerProfileRuntimeService can send FDPP profile requests, complete a loopback Response through RequestTracker, expire unanswered requests, and expose caller-side snapshots.
fusiondesk_qt_peer_profile_runtime_service_tests proves QtPeerProfileRuntimeService can exchange FDPP over a bootstrap control channel, auto-start agent listening for a requested main_screen channel, apply the client main_screen connect profile, and leave that requested channel ready on both session registries.
fusiondesk_reconnect_orchestration_plan_tests proves the pure C++ reconnect plan emits client and agent replacement intent for a service-selected degraded channel, preserves reason/keyframe/teardown intent, and rejects empty, duplicate, unknown, or unplanned degraded channels without Qt.
fusiondesk_qt_tcp_transport_tests proves QtReconnectRuntimeService starts the Qt timer/teardown owner frame and drives ReconnectRuntimeService -> ReconnectCoordinator -> QtReconnectExecutor -> QtReconnectOrchestrator to start the agent replacement listener, reconnect the client replacement channel, preserve plan reason/keyframe intent on both sessions, close old transports after rebind, and route PayloadAck over the replacement channel.
fusiondesk_qt_timer_bridge_tests proves QtTimerBridge can drive RequestTracker timeout expiry.
fusiondesk_qt_event_loop_bridge_tests proves posted callbacks can run through the Qt event loop bridge.
fusiondesk_qt_peer_profile_coordinator_tests proves local TCP connection-plan validation, ChannelSpec-backed requested channel resolution, multi-endpoint client/agent profile pairing, canonical JSON save/load roundtrip, no-sessionId shell profile shape, and missing/duplicate/unknown channel plus duplicate endpoint failure reporting without starting sockets.
fusiondesk_pc_client_smoke and fusiondesk_pc_agent_smoke prove thin PC shells can initialize RuntimeHost and start role-specific sessions.
fusiondesk_pc_agent_listen_profile_smoke proves PC agent --listen-profile can bind a no-sessionId profile to the current shell session.
fusiondesk_pc_profile_plan_smoke proves the profile plan CLI writes multi-channel client/agent profile JSON and rejects unknown channel names.
fusiondesk_pc_agent_start_display_requires_ready_channel_smoke proves PC agent --start-display keeps ModuleHost channel readiness gating intact.
fusiondesk_pc_two_peer_start_display_smoke proves real PC agent/client executables can use fusiondesk_pc_profile_plan generated no-sessionId control/small_data/main_screen listen/connect profiles, required-channel-gated --start-display, client rendered-frame verification, delayed reconnect through QtReconnectRuntimeService, and --print-reconnect-diagnostics output in one bounded startup smoke.
fusiondesk_pc_peer_profile_start_display_smoke proves real PC agent/client executables can use JSON only for control-channel bootstrap, then negotiate small_data/main_screen through QtPeerProfileRuntimeService FDPP before required-channel-gated display startup.
fusiondesk_platform_windows_display provides the first Windows GDI capture adapter seam.
fusiondesk_qt_display_adapters provides the first Qt QImage software render adapter seam when Qt Gui is available.
fusiondesk_windows_gdi_display_capture_tests covers adapter open/close construction.
fusiondesk_qt_image_display_renderer_tests covers decoded software frame rendering into QImage callbacks.
fusiondesk_pc_client_display_mount_smoke and fusiondesk_pc_agent_display_mount_smoke prove PC shells can mount role-specific display adapter dependencies through --mount-display.
```

Work items:

```text
production PC UI invocation of QtPeerProfileRuntimeService, production peer coordination service execution, connection orchestration, and reconnect peer-profile invocation around PC shell transport profile loading
production service execution over the reconnect orchestration plan and production PC UI reconnect lifecycle wiring
production CI wiring beyond local CTest source purity enforcement
```

Exit evidence:

```text
core has no Qt includes.
fusiondesk_source_purity_scan enforces current core/module Qt, Source, and ThirdParty purity through CTest.
Qt code lives under runtime/qt, adapters/qt, platform, bindings, or apps.
FakeTransport tests still pass.
Qt transport can send and receive PacketEnvelope through PacketCodec.
QtPacketChannel can route PacketEnvelope through NetworkRouter over Qt TCP.
QtChannelBinder can bind a ready Qt channel into NetworkManager.
QtSessionTransportConnector can drive the Qt channel bind/ready path from a runtime Qt transport profile.
QtRuntimeTransportManager can own the session connector lifecycle and drive first-frame/keyframe recovery over real Qt TCP for RuntimeHost-mounted display modules.
Qt runtime can load TCP transport profiles from JSON files and validate them against known ChannelSpec entries.
QtRuntimeTransportManager can apply loaded TCP transport connect/listen profiles from JSON files without app code creating individual connectors or QTcpServer listeners.
QtRuntimeTransportManager reports listener startup separately from ChannelRegistry ready state.
QtTcpPeerProfileCoordinator can prepare local profile files for PC shell startup without app code knowing the JSON schema.
fusiondesk_pc_profile_plan can prepare those local profile files from channel names for startup scripts.
QtTimerBridge can drive RequestTracker timeout expiry from the Qt event loop without Qt entering core.
QtEventLoopBridge can post callbacks and run until predicates through QCoreApplication without Qt entering core.
PC client and agent shells can create RuntimeHost, start sessions, and optionally apply Qt transport connect/listen profiles.
```

## G7 Android Controller

Purpose:

```text
Expose the Qt-based client controller as an Android-embeddable library.
```

Work items:

```text
JNI opaque handle
Java/Kotlin facade
controller create/close
connect/disconnect async calls
Surface attach/detach
Android lifecycle bridge
AAR packaging skeleton
```

Exit evidence:

```text
public API exposes Android types only.
Qt/JNI implementation details stay internal.
Surface destroyed pauses rendering without closing session.
Surface recreated requests keyframe.
arm64-v8a and x86_64 test ABIs are planned or configured.
```

## G8 Tunnel And P2P

Purpose:

```text
Add relay, NAT traversal, and direct connection below the network contract.
```

Work items:

```text
SocketFactory
RelaySocket
P2PTunnelSocket
hole punching negotiation
fallback selection
transport diagnostics
enterprise forced-relay policy
```

Exit evidence:

```text
modules do not change when tunnel transport is selected.
ChannelRegistry sees the same channel model for LAN, relay, and tunnel.
control channel survives fallback.
policy can forbid direct tunnel.
```

## Per-Goal Execution Loop

For every goal:

```text
1. Read .codex/skills/fusiondesk-architecture/SKILL.md.
2. Read docs/architecture/README.md.
3. Read only the goal-relevant architecture docs.
4. Confirm the current stage gate.
5. Implement the smallest contract needed next.
6. Add focused smoke or unit tests.
7. Update CMake/CTest.
8. Run verification.
9. Update docs if the contract changed.
10. Remove temporary build output.
11. Continue to the next goal item.
```

Doc update responsibility:

```text
Protocol changes update PROTOCOL_MESSAGE_PATTERN.md and FUSIONDESK_STAGE_GATES.md.
Network changes update NETWORK_MODEL.md and NETWORK_CHANNEL_REGISTRY_AND_SCHEDULER.md.
Session/runtime changes update SESSION_MODEL.md and RUNTIME_HOST_AND_SESSION_MANAGER_DESIGN.md.
Module changes update MODULE_AND_INTERFACE_BLUEPRINT.md and FUSIONDESK_IMPLEMENTATION_BASELINE.md.
Display changes update DISPLAY_MODULE_DESIGN.md and the stage gate evidence.
Every completed goal updates this document and the compact SKILL baseline.
```

## Standard Verification

Run before claiming completion:

```powershell
$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$env:Path = "$machinePath;$userPath"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" include/fusiondesk/core src/core
rg -n "Source/|ThirdParty/" include/fusiondesk/core src/core

uv run --python 3.12 --with pyyaml python C:\Users\gaoxi\.codex\skills\.system\skill-creator\scripts\quick_validate.py .codex\skills\fusiondesk-architecture
```

Expected:

```text
cmake build exits 0
ctest exits 0
Qt include scan has no matches
Source/ThirdParty scan has no matches
skill validation exits 0
```

Remove `build` after verification unless the user explicitly asks to keep build output.

## Blocker Rules

Stop automatic progress only when:

```text
the next step requires a product decision not in docs
a test failure cannot be diagnosed from local context
a required external SDK/toolchain is missing
the change would require using old Source as runtime dependency
the change would violate core purity
the user redirects the goal
```

Do not stop for ordinary implementation choices. Choose the conservative option that matches the architecture docs.

## Immediate Next Goal

The current next goal is:

```text
G6 Real Transport And Qt Boundary: start real transport and adapter seams without leaking Qt into core.
```

Current implemented behavior:

```text
ModuleHost denies unsupported role or platform before attach/start.
ModuleHost denies missing required module dependencies before start.
ModuleHost denies missing required channel readiness before start.
ModuleHost still applies policy authorization before starting a module.
display.screen.agent and display.screen.client manifests exist.
ModuleCatalog can select role-specific remote desktop manifests.
ModuleIngressRegistry is activated after module start and unregistered before stop.
DisplayAgentModule and DisplayClientModule exist.
fusiondesk_display_mvp_tests proves fake first-frame and keyframe request/response.
display modules publish first-frame and keyframe recovery diagnostics events.
display modules expose basic structured MVP counters/snapshots.
display capture diagnostics expose backend id, latest structured status, and captureErrors through module/runtime/session/PC shell snapshots.
display frame types carry width, height, stride, pixel format, frame id, keyframe flag, and timestamp for real adapter work.
RawFrameEncoder and RawFrameDecoder provide the first software-frame payload schema.
display agent drops delta frames when the video channel is congested.
display keyframe recovery still sends and renders keyframes while congested.
DisplayRuntimeService owns client first-frame timeout and reconnect fresh-frame retry, sending correlated keyframe Requests over small_data when rendered-frame progress stalls.
RuntimeHost can mount display.screen from ProductProfile into role-specific display modules.
RuntimeHost uses ModuleComposer and DisplayModuleFactory for the display profile mount.
RuntimeHost also registers FeatureModuleFactory skeletons for audio, microphone, filesystem, printer, keyboard, mouse, touch, gamepad, and camera profile module ids.
RuntimeHost now registers InputModuleFactory before generic FeatureModuleFactory, so input.mouse/input.keyboard resolve to role-specific contract modules instead of lifecycle skeletons.
fusiondesk_feature_contract_tests proves the first input contract loop with FDIN payloads through ModuleHost and NetworkRouter.
fusiondesk_module_factory_tests covers the composition boundary.
Display packets carry SessionContext sessionId/traceId and pass PacketCodec validation for VIDEO Event NoResponseRequired + KeyFrame traffic.
QtTcpTransportSocket exists under adapters/qt and can roundtrip PacketEnvelope bytes through PacketCodec.
QtPacketChannel exists under adapters/qt and can route PacketEnvelope through NetworkManager/NetworkRouter over Qt TCP.
QtChannelBinder exists under adapters/qt and can register SocketGroup, bind ChannelRegistry, and mark ready for QtPacketChannel.
QtSessionTransportConnector exists under runtime/qt and can create/adopt Qt TCP transports from a profile.
QtRuntimeTransportManager exists under runtime/qt and owns QtSessionTransportConnector instances plus QTcpServer listeners per RuntimeHost sessionId.
QtRuntimeTransportManager can prepare reconnect replacement channels from Qt TCP connect profiles or accepted sockets without marking them ready, drive Session::reconnect in one runtime-facing call, and close/remove the previous active Qt transport after successful one-call reconnect; Session::reconnect owns rebind and ready.
runtime/connection can resolve a service-backed reconnect orchestration plan into client replacement channels, agent replacement listeners, degraded-channel intent, keyframe intent, reason, and post-rebind teardown intent without creating sockets or calling Session.
QtReconnectExecutor can adapt the pure coordinator replacement-executor boundary to QtReconnectOrchestrator, which starts reconnect-aware listeners for the agent side and drives client-side replacement reconnect through QtRuntimeTransportManager.
ReconnectRuntimeService exposes `ReconnectRuntimeServiceSnapshot::diagnostics`, a pure C++ report that combines plan, replacement, Session rebind, teardown, pending request, and timeout state.
TunnelReconnectExecutor exists as the first pure relay/direct/P2P replacement executor contract below ReconnectCoordinator.
TunnelCandidateProfile negotiation exists for compatible client/agent relay/direct/LAN candidate selection.
ITunnelTransportFactory exists as the concrete tunnel socket adapter interface below candidate selection; factory requests carry the local candidate and selected peer candidate when built from a negotiation result.
runtime/qt can load TCP transport profiles from JSON files and resolve tcpChannels/tcpListenChannels against known ChannelSpec values.
runtime/connection can resolve ChannelSpec-backed peer connection plans without Qt.
runtime/connection has a pure C++ PeerProfileService that exchanges FDPP peer profile request/response payloads over the control channel without JSON, Qt, or sockets.
runtime/connection has a pure C++ PeerProfileRuntimeService that owns caller-side FDPP dispatch, response completion, timeout expiry, and snapshots without Qt, JSON, sockets, or Session state.
runtime/qt has a QtPeerProfileRuntimeService bridge that applies FDPP listen/connect results through QtRuntimeTransportManager without putting packet construction in PC apps.
QtRuntimeTransportManager can load and apply TCP transport connect/listen profiles from JSON files.
QtTcpPeerProfileCoordinator can generate, validate, and save local multi-endpoint TCP peer profile files from known ChannelSpec values and requested channel plans.
fusiondesk_pc_profile_plan can prepare paired client/agent profile files from named MVP channel plans for startup scripts.
fusiondesk_qt_tcp_transport_tests proves QtRuntimeTransportManager can apply no-sessionId profiles to the current runtime session, listen from tcpListenChannels, accept/adopt inbound Qt TCP connections into an agent session, mark coordinator-generated multi-endpoint control/video channels ready, prepare reconnect replacement channels, drive one-call Qt TCP reconnect, close/remove old active Qt transports after rebind, and carry the RuntimeHost-mounted display first-frame/keyframe recovery loop over JSON-loaded Qt TCP profile data through the one-call apply path.
QtTimerBridge exists under runtime/qt and can drive RequestTracker timeout expiry through QTimer.
fusiondesk_qt_timer_bridge_tests covers timer start/stop, deferred handler install, and RequestTracker timeout expiry.
QtEventLoopBridge exists under runtime/qt and can post callbacks, process events, and run until a predicate.
fusiondesk_qt_event_loop_bridge_tests covers posted callback execution and immediate predicate handling.
WindowsGdiDisplayCapture exists under platform/windows/display as the first PC capture adapter seam.
Production capture backend direction is documented in DISPLAY_SCREEN_PIPELINE_DESIGN.md: Windows DXGI/WGC with GDI fallback, Linux X11/PipeWire/DRM, macOS ScreenCaptureKit, Android plus HarmonyOS/OpenHarmony adapter-only future capture, and RK3568/RK3588 through Linux/Android display paths.
runtime/display has a pure DisplayCaptureBackendCapability selector for that matrix, including available/unavailableReason fallback handling and optional DisplayTargetArchitecture/DisplayTargetSocProfile filtering, covered by fusiondesk_display_capture_backend_selection_tests.
runtime/display has IDisplayCaptureBackendFactory, StaticDisplayCaptureBackendFactory, DisplayCaptureBackendFactoryRegistry, createSelectedDisplayCapture, unavailableDefaultDisplayCaptureBackendCapabilities, and createUnavailableDefaultDisplayCaptureBackendFactory for reusable platform capture factory composition and full-platform placeholder diagnostics.
PC shell uses WindowsDisplayCaptureBackendFactory through createSelectedDisplayCapture for agent --mount-display.
platform/windows/display has WindowsDisplayCaptureBackendFactory as the registry-backed aggregate insertion point for DXGI/WGC/GDI backend registration; DXGI now has a compiled Desktop Duplication adapter, a direct-create factory path, a default runtime probe before selector availability, static-desktop keyframe reuse from the latest successful frame, Windows software cursor overlay for GDI/DXGI BGRA frames with PC `--display-no-cursor` diagnostics control, `FUSIONDESK_ENABLE_DXGI_CAPTURE=0` as a disable override, and a manual `FUSIONDESK_VALIDATE_DXGI_CAPTURE=1` real-frame validation smoke. WGC now has a rollout-gated monitor/window adapter, `DisplayCaptureOpenOptions` sourceType/nativeSourceHandle support, manual `FUSIONDESK_VALIDATE_WGC_CAPTURE=1` monitor validation, and optional `FUSIONDESK_VALIDATE_WGC_WINDOW_CAPTURE=1` window/native-handle validation. GDI remains the concrete fallback.
CMakePresets.json and cmake/toolchains provide the first host/cross build entry points for Windows host, Linux x86_64/aarch64/loongarch64/mips64el, RK3568/RK3588, Android arm64-v8a/x86_64, and OpenHarmony/HarmonyOS arm64 without hard-coded SDK paths.
QtImageDisplayRenderer exists under adapters/qt/display as the first Qt software render adapter seam.
PC client and agent shells can explicitly mount role-specific display adapter dependencies with `--mount-display`.
PC client and agent shells create sessions and mount/start profile modules through SessionMainline.
PC shell `--start-display` uses LinkChannelBindingReport while waiting for required module channels.
runtime/session exposes SessionRuntimeDiagnosticsSnapshot for stable session, link/channel, module-count, and diagnostics reads without shell stderr parsing.
PC shell `--print-session-diagnostics` exposes the same snapshot as stable stdout records for blocked and ready lifecycle phases.
fusiondesk_pc_client and fusiondesk_pc_agent exist as thin QCoreApplication shells.
fusiondesk_pc_client_smoke and fusiondesk_pc_agent_smoke cover RuntimeHost initialization and role-specific session startup.
Release CTest executes assertions instead of compiling them out.
```

Remaining target behavior:

```text
MVP display counters/snapshots and diagnostics events are implemented for fake-ready first-frame, dropped-frame, stale-frame drop, frame-gap recovery, decode-failure recovery, geometry/format full-refresh promotion, captureErrors propagation, client first-frame/reconnect fresh-frame timeout recovery, and keyframe recovery paths; DXGI is now default-probed with GDI fallback and can reuse the latest successful frame for static-desktop keyframe/full-refresh requests, Windows GDI/DXGI can compose the Win32 cursor into BGRA frames by default, WGC has a rollout-gated monitor/window adapter, DisplayRuntimeService has a first switch-backend execution path through the configured factory, and runtime/display has first recovery cooldown plus repeated-failure promotion guardrails. Product-tuned recovery policy/UI, cursor sideband, and remaining concrete non-Windows capture adapter implementations remain pending.
PC shell startup can load and apply `--transport-profile` and `--listen-profile` through QtRuntimeTransportManager against the current shell session; core RuntimeHost/Session do not auto-load transport profiles.
PC shell `--start-display` waits for required module channels before ModuleHost start, a CLI-generated bounded two-process PC first-frame smoke exists with control/small_data/main_screen profile channels, a CLI can generate local startup profiles, delayed `--reconnect-profile` routes through QtReconnectRuntimeService in client-replacement mode, startup can negotiate small_data/main_screen through QtPeerProfileRuntimeService after control bootstrap, and --print-reconnect-diagnostics exposes reconnect lifecycle status from the common runtime snapshot; production PC UI invocation of that service, peer coordination service execution, PC UI reconnect lifecycle wiring, real cross-target CI workers/sysroots/SDKs, concrete non-GDI capture adapter implementations, hardware encode/decode, production renderer backends, and non-Windows display platform adapters are still pending.
Service-backed reconnect planning is now implemented as a pure C++ contract, Qt runtime can start and drive the local TCP replacement form of that plan through QtReconnectRuntimeService, and runtime/connection can plan correlated old-transport teardown commands, map them to control-channel `PacketEnvelope` requests, dispatch them through `NetworkRouter`, subscribe for Ack/Progress/Response/Error/StreamEnd, track terminal results through `RequestTracker`, handle peer-side FDRT requests through a pure close-target interface, and summarize terminal Response/Error results through ReconnectTeardownService. ReconnectCoordinator now provides the high-level service frame that sequences plan resolution, runtime replacement executor calls, client reconnect handoff, and teardown dispatch through pure executor interfaces. ReconnectRuntimeService owns the coordinator, RequestTracker, teardown service, teardown executor, and a reconnect diagnostics report snapshot; QtReconnectRuntimeService adds QtReconnectExecutor and QtTimerBridge without moving Qt into runtime/connection. PC shell delayed reconnect and reconnect diagnostics output now use this owner; production PC UI consumption of the diagnostics report, PC UI service-owner wiring, tunnel selection, hardware encode/decode, production renderer backends, and non-Windows display platform adapters are still pending.
Tunnel/P2P has a first executor contract, pure candidate profile negotiation, and a concrete transport factory interface; real relay sockets, direct P2P sockets, NAT traversal candidate gathering, and enterprise policy selection are still pending Gate P7 work.
```

## Current Minimal Mainline Increment

Feature-module depth is paused. The current P1 is the minimum product mainline:

```text
RuntimeHost -> SessionManager -> link/channel bind -> ChannelRegistry ready
-> ProductProfile multi-module mount -> ModuleHost start -> diagnostics/snapshot
```

`runtime/session/SessionMainline` is the pure C++ frame for this chain.
It accepts adapter-created `IChannel` instances, binds and marks them ready,
mounts ProductProfile modules through RuntimeHost, starts modules through
ModuleHost gates, and returns channel/link/mount/start reports. PC client/agent
startup now uses this owner for session creation plus profile module
mount/start. `runtime/session/SessionRuntimeDiagnosticsSnapshot` now provides a
stable runtime reader over SessionSnapshot, LinkChannelBindingReport,
mounted/running module counts, blocked-channel count, and session-scoped
diagnostics, and PC shell can print that reader through
`--print-session-diagnostics`; Qt profile loading and event-loop polling stay in the Qt/app
boundary. `FUSIONDESK_MINIMAL_VERSION_RUNBOOK.md` now freezes the current build,
smoke, diagnostics, definition-of-done, and out-of-scope gate. Clipboard,
input, filesystem, printer, audio, camera, gamepad,
peripheral, and tunnel feature depth remains attachment-module work and must
reference old module behavior before implementation resumes.
## Peer Profile Exchange Note

runtime/connection now also resolves a pure C++ peer profile exchange pair builder that turns resolved plans into client/agent profile pair data without Qt or sockets.
fusiondesk_peer_profile_exchange_tests covers the new pure C++ peer profile exchange pair builder.
Peer profile exchange has advanced from local pair building to a runtime/connection service boundary. `PeerProfileService` uses `PacketEnvelope` Request/Response on the control channel, carries `FDPP` payloads instead of JSON, ignores unrelated control payloads, and keeps JSON profile files limited to local PC startup and smoke tooling. `PeerProfileRuntimeService` owns the caller side of that boundary with RequestTracker-backed completion, timeout expiry, and snapshots for future PC/Qt orchestration. `QtPeerProfileRuntimeService` applies successful profile results through QtRuntimeTransportManager, and PC shell startup can now invoke it through --peer-profile-service / --peer-profile-channel after control bootstrap. fusiondesk_peer_profile_service_tests covers the responder boundary; fusiondesk_peer_profile_runtime_service_tests covers the caller-side owner; fusiondesk_qt_peer_profile_runtime_service_tests covers the Qt application bridge; fusiondesk_pc_peer_profile_start_display_smoke covers the real executable startup path.
## Peer Coordination Note

runtime/connection now also has a pure C++ peer coordination planner that combines peer profile exchange with reconnect hints for known channel plans.
fusiondesk_peer_coordination_tests covers the core planner.
## Service Reconnect Plan Note

runtime/connection now also has a pure C++ reconnect orchestration planner. It consumes peer profile exchange input plus degraded ChannelKey values and emits client/agent side replacement intent, reason, keyframe intent, and teardown-after-success intent without creating sockets or calling Session. fusiondesk_reconnect_orchestration_plan_tests covers the service-selected success path plus empty, duplicate, unknown, and unplanned degraded-channel rejection.
ReconnectCoordinator now adds the large service-level code frame above that planner. It runs the sequence through `IReconnectReplacementExecutor` and `IReconnectTeardownExecutor`, so Qt local TCP, future tunnel/P2P, and PeerProfileService-driven exchange can plug in without changing Session. ReconnectRuntimeService owns this coordinator plus teardown tracking behind a runtime start/run/expire/snapshot boundary. QtReconnectExecutor is the first concrete replacement-executor adapter. fusiondesk_reconnect_coordinator_tests covers the pure high-level frame with one smoke instead of many edge-case tests.
runtime/connection now also has a pure C++ reconnect teardown command/response/envelope/dispatch/service contract. It emits correlated old-transport teardown commands from post-rebind teardown intent, maps those commands to control-channel `PacketEnvelope` requests with the target channel carried in the `FDRT` payload, dispatches them through `NetworkRouter`, subscribes for interim and terminal responses, tracks pending state through `RequestTracker`, keeps `Ack Accepted` as interim only, expires timed-out requests through the same tracker path, and summarizes terminal Response/Error outcomes. fusiondesk_reconnect_teardown_ack_tests covers command generation, side-plan input, request envelope mapping, response envelope mapping, non-control route rejection, complete responses, incomplete or mismatched responses, failed responses, duplicate target channels, and non-terminal Ack rejection. fusiondesk_reconnect_teardown_dispatch_tests covers message-id assignment, send tracking, Ack-then-terminal completion, and local send failure synthesis. fusiondesk_reconnect_teardown_service_tests covers service start, response subscription, Ack interim handling, terminal completion, timeout expiry, peer-side handler routing, inactive dispatch rejection, and ReconnectRuntimeService stop-time cancellation of pending teardown requests.
ReconnectTeardownHandler now provides the peer-side pure C++ handler boundary. It subscribes to the control route, recognizes FDRT payloads, ignores unrelated control payloads, rejects malformed FDRT requests with ProtocolError, calls `IReconnectTeardownCloseTarget`, and returns terminal Response/Error. fusiondesk_reconnect_teardown_handler_tests covers successful close, close failure, malformed payload rejection, unrelated control payload ignore, stop/unsubscribe, and route normalization. QtRuntimeTransportManager implements `IReconnectTeardownCloseTarget` for Qt TCP reconnect teardown, and fusiondesk_qt_tcp_transport_tests covers the handler-to-manager adapter path.
## Qt Reconnect Orchestrator Note

runtime/qt now has QtReconnectRuntimeService, QtReconnectExecutor, and QtReconnectOrchestrator. QtReconnectRuntimeService owns the Qt-facing service loop with QtTimerBridge, delegates common lifecycle to ReconnectRuntimeService, and keeps Qt out of runtime/connection. QtReconnectExecutor implements the pure `IReconnectReplacementExecutor` contract for ReconnectCoordinator, while QtReconnectOrchestrator maps the local TCP plan to existing QtRuntimeTransportManager calls. The agent side starts reconnect-aware listeners that preserve plan reason and requestDisplayKeyframe when accepting a replacement, while the client side initiates replacement reconnect through Session::reconnect. QtSessionTransportConnector exposes transport snapshots so reconnect-time Qt event processing cannot invalidate active transport iteration. fusiondesk_qt_tcp_transport_tests covers the owner-driven local TCP execution path and post-rebind PayloadAck routing.
## Reconnect Rebind Note

The current automatic route can now advance from reconnect diagnostics into a pure core rebind step: `ReconnectRequest` may carry already-created replacement channels, and Session calls `NetworkManager::rebindChannel` after pausing reconnect-aware modules. runtime/qt can prepare those replacement channels from local Qt TCP connect profiles or accepted sockets and now exposes a one-call reconnect wrapper that still delegates rebind to Session. PC shell delayed reconnect now enters through QtReconnectRuntimeService and ReconnectCoordinator before reaching the Qt replacement executor, and the shell can print the same diagnostics snapshot with lifecycle phases. After successful one-call reconnect, runtime/qt closes and removes the old active Qt transport for the rebound channel, and QtRuntimeTransportManager can also acknowledge peer-side FDRT teardown requests through the close-target adapter. Session then replays running module ingress routes through ModuleHost while recording replay outcomes in `lastReconnect.replayedIngress`. The next automatic steps should still treat production remote service execution, PC UI reconnect lifecycle wiring, and tunnel selection as pending production work.
## Reconnect Report Note

Automatic progress can now rely on `SessionSnapshot.lastReconnect` for the latest reconnect outcome. It includes per-channel degrade/rebind status, ingress replay results, and module pause/resume reports, so the next goal steps can distinguish a successful one-call Qt reconnect from a failed Qt replacement channel or module ingress replay before adding production remote reconnect service execution.
