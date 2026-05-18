# FusionDesk Stage Gates

This document converts the architecture into execution gates. A stage is not complete because code exists. It is complete when the exit evidence is present.

## Product Direction

The project is an enterprise remote desktop platform.

The architecture must support:

```text
PC full-platform client and agent
enterprise policy and audit
module-level feature licensing
multi-socket multi-channel network
future tunnel, relay, and P2P direct connection
Android Qt-based client controller
Android embeddable controller library
legacy behavior reference without runtime dependency
```

## Goal To Gate Mapping

Goal names in `GOAL_AUTOPILOT_PLAN.md` and gate names in this document are related but not one-to-one.

```text
G1 Protocol Core -> Gate P0 and Gate P2 protocol evidence
G2 Network Core -> Gate P0 and Gate P2 channel, scheduler, socket evidence
G3 Session Runtime -> Gate P1 runtime/session evidence
G4 Module System -> Gate P0/P1 module host evidence and Gate P3 pre-entry evidence
G5 Display MVP -> Gate P3 Display Screen MVP
G6 Real Transport And Qt Boundary -> Gate P3.5 transport adapter evidence and Gate P6 prerequisites
G7 Android Controller -> Gate P6 Android Controller
G8 Tunnel And P2P -> Gate P7 Tunnel And Direct Connection
```

## Gate Execution Policy

Gates are closed in batches of bounded coding items. A coding item should land
one capability, run one focused proof, update evidence, and then move forward.

Use this cadence:

```text
item implementation -> focused proof -> tracker update -> next item
batch complete -> full build -> full CTest -> purity scans -> code review
```

Focused proofs may be unit tests, one adapter smoke, one shell smoke, or one
manual opt-in validation gate. Repeated local hardening of a single feature is
not the default. If a non-baseline adapter is unstable after two focused repair
attempts, keep it behind a rollout or validation flag, record the failing gate,
and continue the next closure item without weakening the stable baseline.

The full audit remains mandatory before claiming a gate complete, committing a
major batch, or promoting an adapter from validation-only to default policy.

## Gate P0 - Core Foundation

Goal:

```text
Make FusionDesk core compile independently and define stable contracts.
```

Scope:

```text
protocol types
protocol envelope request/response semantics
protocol wire codec
capability negotiation payload and negotiated limits
session context
network router
channel interface
module manifest
module host
module ingress registry
policy engine
diagnostics event types
```

Exit evidence:

```text
fusiondesk_core configures and builds with CMake.
core has no Qt includes.
core has no Source or ThirdParty includes.
fusiondesk_source_purity_scan fails CTest if current core or modules include Qt, Source, or ThirdParty tokens.
NetworkRouter unit tests cover route match and no-match.
PacketEnvelope contains protocol version, message id, correlation id, responseTo, message kind, status, priority, timeout, sequence, and flags.
Request/response, ACK, error, stream, timeout, and correlation rules are documented.
PacketCodec tests cover roundtrip encode/decode, incomplete packet, bad magic, invalid header length, unsupported version, packet length mismatch, header CRC mismatch, payload CRC mismatch, payload-too-large, and validation failure.
PacketCodec tests cover fire-and-forget VIDEO Event with NoResponseRequired + KeyFrame flags.
CapabilityNegotiation tests cover payload roundtrip, malformed payload rejection, protocol version incompatibility, common channel/packet/message intersections, denial reasons, and negotiated protocol limits.
CapabilityExchange tests cover capability request/response envelopes, negotiated validator/codec options, and stream chunk limit checks.
ChannelAllowlist tests cover negotiated channel type, packet type, and message kind rejection before routing.
ChannelDefaults tests cover control, small_data, and main_screen default channel specs and default packet priority mapping.
ChannelRegistry tests cover ready, degraded, pressure, negotiated allowlist, and channel-spec allowlist behavior.
PriorityScheduler tests cover Critical before Bulk, realtime keep-latest delta frames, keyframe preservation, pressure, and drain.
NetworkManager tests cover default channel registration, ready-gated enqueue, priority flush through NetworkRouter, channel rebind, and subscription preservation.
NetworkManager transport tests cover ChannelSpec.socketClass writes through SocketGroup and realtime failure isolation from control.
SocketGroup tests cover fake transport socket registration, open, write, close, duplicate rejection, and isolated socket failure.
ModuleIngressRegistry tests cover manifest registration, unregister, re-register replacement, and ModuleHost start/stop lifecycle.
PolicyEngine tests cover allow, deny, unsupported role, unsupported platform.
ModuleHost tests cover unsupported role rejection, unsupported platform rejection, missing dependency rejection, required channel readiness rejection, policy-allowed start, all-or-nothing preflight when partial start is disabled, and independent start when partial start is enabled.
ModuleHost tests cover structured ModuleSnapshot state.
ModuleHost tests cover add-order startup so ModuleComposer ordering is preserved.
Module protocol tests cover ModuleManifest version declaration, peer module compatibility ranges, ModuleStartOptions peer version delivery, and module-owned Exchange Request/Response payload handling.
Module inventory exchange tests cover FDMI control-channel request/response, empty inventory receipt, malformed request-envelope rejection, non-Ok response rejection, caller-side runtime service terminal Response/Error completion, empty successful response completion, timeout expiry, manifest subset roundtrip, remote inventory to ModuleHost peerVersions, and incompatible remote version start rejection.
ModuleCatalog tests cover display.screen.agent/client role split and role-filtered remote desktop suite discovery.
ModuleFactory tests cover profile alias resolution, concrete module de-duplication, missing factories, create failures, ProductProfile version constraints, alias/concrete duplicate version-constraint bypass rejection, dependency ordering, dependency cycle reporting, and dependency failure reporting.
SessionMainline tests cover ModuleStartOptions peer version pass-through into ModuleHost start gates and remote module inventory persistence in SessionRuntimeDiagnosticsSnapshot.
```

Forbidden:

```text
no Qt in core
no platform macro branches in core except compile-time feature constants
no concrete socket classes in core
no feature module private headers in core
```

## Gate P1 - Runtime And Session

Goal:

```text
Create the orchestration loop that owns sessions and module composition.
```

Scope:

```text
RuntimeHost
SessionManager
ClientSession
AgentSession
SessionState machine
ProductProfile
ModuleComposition
DiagnosticsSink
```

Exit evidence:

```text
RuntimeHost can load a product profile.
SessionManager can create, find, reconnect, and close sessions.
ClientSession and AgentSession share base lifecycle rules.
Session creates NetworkManager, PolicyEngine, ModuleHost, and DiagnosticsSink.
SessionManager registers ProductProfile minimum channel specs into NetworkManager when SessionCreateOptions supplies them.
Session emits lifecycle diagnostics.
Session denies module start when policy or required dependency fails.
Session passes the owned ChannelRegistry into ModuleHost so required module channels are readiness-gated before start.
fusiondesk_session_manager_tests covers ClientSession, AgentSession, feature authorization, ProductProfile minimum channel specs, owned NetworkManager/PolicyEngine/ModuleHost, lifecycle diagnostics, reconnect, and close.
fusiondesk_runtime_host_tests covers RuntimeHost initialization, ProductProfile defaults, SessionManager access, profile channel spec handoff, session creation, and shutdown.
fusiondesk_session_mainline_tests covers the minimum RuntimeHost -> SessionManager -> channel bind/ready -> ProductProfile multi-module mount -> ModuleHost start path plus blocked link/channel report diagnostics before module start.
runtime/session exposes SessionRuntimeDiagnosticsSnapshot so future UI/service readers can consume session state, link/channel readiness, mounted/running module counts, and diagnostics without parsing shell stderr.
PC shell exposes that snapshot through --print-session-diagnostics for blocked and ready lifecycle phases.
FUSIONDESK_MINIMAL_VERSION_RUNBOOK.md defines the current minimum build, smoke, and out-of-scope completion gate.
fusiondesk_module_host_tests covers role, platform, dependency, channel readiness, and policy start gates.
fusiondesk_display_mvp_tests covers SessionSnapshot moduleSnapshots after profile-driven display mount.
fusiondesk_module_factory_tests covers ModuleComposer profile resolution, dependency ordering, dependency cycles, and dependency-failure reporting.
```

Forbidden:

```text
app shell must not create modules directly
module must not own session lifecycle
network must not own policy decisions
```

## Gate P2 - Network Channels

Goal:

```text
Implement channel readiness, priority, queue pressure, and reconnect hooks.
```

Scope:

```text
ChannelRegistry
PriorityScheduler
NetworkManager
default MVP channel specs
SocketGroup
ITransportSocket
PacketEnvelope v1
RequestTracker
ProtocolValidator
CapabilityNegotiator
CapabilityExchange
ChannelAllowlistValidator
SendOptions
ChannelPressure
reconnect rebind
```

Exit evidence:

```text
control, main_screen, small_data channels can be registered.
VIDEO packets are Realtime priority.
PAYLOAD_ACK packets are Interactive priority.
bulk packets cannot block critical or realtime packets.
video pressure state is visible to display sender.
channel failure can mark only affected channels degraded.
rebind replays module subscriptions.
responses and errors are correlated to original requests.
pending requests time out and publish diagnostics.
fusiondesk_request_tracker_tests covers RequestTracker ack, final response, and timeout behavior.
fusiondesk_protocol_validator_tests covers valid requests, missing timeout, missing responseTo, fire-and-forget events, and oversized payloads.
fusiondesk_packet_codec_tests covers PacketEnvelope wire serialization and malformed packet rejection.
fusiondesk_capability_negotiation_tests covers capability payload serialization and negotiated limits.
fusiondesk_capability_exchange_tests covers capability exchange envelopes and negotiated validator/codec options.
fusiondesk_channel_allowlist_tests covers negotiated channel, packet, and message allowlists.
fusiondesk_channel_defaults_tests covers default MVP channel specs and packet priority mapping.
fusiondesk_channel_registry_tests covers channel readiness, degraded state, pressure, and allowlist behavior.
fusiondesk_priority_scheduler_tests covers priority order and realtime video drop policy.
fusiondesk_network_manager_tests covers NetworkManager default registration, ready-gated enqueue, priority flush, channel rebind, and subscription preservation.
fusiondesk_network_manager_transport_tests covers NetworkManager transport writes through SocketGroup and socket-class failure isolation.
fusiondesk_socket_group_tests covers fake transport socket lifecycle and isolated socket failure.
fusiondesk_module_host_tests covers reconnect ingress replay for running modules without duplicate delivery.
fusiondesk_qt_tcp_transport_tests covers runtime/qt preparation of reconnect replacement channels from Qt TCP profiles and accepted sockets, one-call runtime/qt Session reconnect, old active Qt transport teardown, and traffic over the replacement channel.
```

Forbidden:

```text
network layer must not parse codec payload
control traffic must not depend on video channel health
bulk traffic must not share an unbounded queue with video
```

## Gate P3 - Display Screen MVP

Goal:

```text
Prove the first complete remote desktop path through FusionDesk.
```

Scope:

```text
display.screen manifest split by role
DisplayAgentModule
DisplayClientModule
IDisplayCapture
IVideoEncoder
IVideoDecoder
IDisplayRenderer
IRenderSurface
  new display platform and framework adapters
RawFrameEncoder and RawFrameDecoder
DisplayCodecCapability and DisplayCodecSelectionRequest
WindowsGdiDisplayCapture adapter seam
QtImageDisplayRenderer adapter seam
fake network integration test
```

Production codec closure lane:

```text
P3-H264 is required for production display and must remain in the plan.
P3-H264 is separate from the raw Windows-Windows baseline gate.
Raw remains the default fallback when product rollout policy is off or the host H.264 probe fails.
The H.264 adapter belongs in codec/platform adapters, not core or module internals.
No Qt, Source, or ThirdParty dependency may leak into core/modules while landing H.264.
```

P3-H264 exit evidence:

```text
MediaFoundation encoder/decoder objects pass a focused keyframe + independent-frame harness for the current Windows synchronous all-intra H.264 mode.
True P-frame delta compression passes the opt-in Windows MediaFoundation gate with decoder low-latency continuous-stream output handling, keyframe clean-point sample marking, and delayed-output frame metadata mapping.
Decoder reset/recreate handles sequence headers and reconnect-style fresh-frame recovery.
FDSF compressed payload metadata is preserved for frame id, keyframe flag, dimensions, timestamp, config bytes, and bitstream bytes.
PC opt-in Windows-Windows H.264 smoke passes the all-intra validation path and the true P-frame validation path over real agent/client processes, with the P-frame child-process environment explicitly propagated and a bounded window long enough for the delayed decoder pipeline.
ProductProfile display codec policy can promote MediaFoundation from validation-only to the default Windows codec candidate while preserving raw fallback.
Default raw startup remains green when H.264 validation flags are not set.
Codec plan diagnostics show why H.264 is selected, rejected, or falls back.
Session diagnostics expose the selected display codec, backend, rollout mode, fallback state, fallback reason, P-frame/delta-frame state, and delayed decoder counters for product UI/service consumers.
Runtime exposes a product display health summary and presentation helper that collapse session, link, module, capture, and codec state into ok/warning/degraded/blocked plus stable status/action/capture/codec state codes for UI/service consumers.
No temporary debug stdout, fake H.264 objects, or silent raw fallback in exact-H.264 mode.
```

Current fake-slice evidence:

```text
DisplayAgentModule and DisplayClientModule exist.
fusiondesk_display_mvp_tests starts agent/client display modules through ModuleHost.
fusiondesk_display_mvp_tests routes VIDEO over an in-process bridge channel.
fusiondesk_display_mvp_tests renders first frame through fake decoder/renderer.
fusiondesk_display_mvp_tests sends PAYLOAD_ACK Request and receives Response.
fusiondesk_display_mvp_tests renders the keyframe emitted after the request.
fusiondesk_display_mvp_tests asserts first-frame and keyframe diagnostics events.
fusiondesk_display_mvp_tests asserts basic structured display counters/snapshots for first-frame, dropped-frame, and keyframe recovery paths.
fusiondesk_display_mvp_tests asserts client stale-frame drop, delta frame-gap recovery request, decode-failure recovery request, and dropped/frame-gap/decode-error counters.
fusiondesk_display_mvp_tests asserts display clients can accept decoder `NeedsMoreInput` plus one-frame delayed output without treating codec latency as decode failure, while still using decoded frame ids for gap recovery.
fusiondesk_display_mvp_tests asserts DisplayAgentModule promotes captured source geometry, stride, or pixel-format changes to keyframe/full-refresh frames and reports captureGeometryOrFormatChanges.
fusiondesk_display_frame_codec_tests asserts raw frame metadata and pixel payload roundtrip.
fusiondesk_display_encoded_video_payload_tests asserts FDSF compressed-frame metadata/config/bitstream roundtrip, malformed payload rejection, stable codec/bitstream names, and raw decoder rejection of a compressed envelope.
fusiondesk_display_codec_selection_tests covers the pure runtime codec selector for current raw fallback, future H.264/H.265 adapter availability, hardware/software policy filters, zero-copy decode preference, codec parsing, and Rockchip architecture/SoC filtering.
fusiondesk_display_codec_negotiation_tests covers the pure two-peer codec negotiation contract, including H.264 selection when both peers support it, raw fallback when the decoder lacks H.264, exact-backend no-silent-fallback behavior, direction validation, and no-common-preference rejection.
fusiondesk_display_codec_backend_factory_tests covers the pure runtime codec factory path that creates the current RawFrameEncoder/RawFrameDecoder through selector-owned capabilities, unavailable default factory diagnostics, registry forwarding, exact unavailable adapter rejection, and selected-H.264 factory creation failure without silent raw fallback.
fusiondesk_windows_media_foundation_display_codec_tests covers the first Windows MediaFoundation H.264 codec capability/probe/preflight skeleton, including rollout-disabled unavailable capability diagnostics, registry fallback to raw, opt-in probe stability, disabled preflight without startup side effects, odd-size preflight rejection before MediaFoundation startup, BGRA-to-NV12 preflight conversion diagnostics, opt-in MFT/media-type preflight stability, direct factory encoder/decoder creation while the capability is policy-gated, validation-only FUSIONDESK_SELECT_MF_H264 selector enablement, ProductProfile/PC shell production policy enablement with P-slice output, repeated keyframe encode/decode recovery, decoder delayed-output metadata mapping, decoder low-latency P-frame output handling, a manual FUSIONDESK_VALIDATE_MF_H264_ENCODE gate for real H.264 bitstream output, FDSF wrapping, direct factory decode, and BGRA decoded output, plus a manual FUSIONDESK_VALIDATE_MF_H264_PFRAME gate for Annex B P-slice validation.
fusiondesk_pc_two_peer_start_display_smoke keeps the default raw reconnect path, FUSIONDESK_VALIDATE_PC_H264_DISPLAY turns it into an exact opt-in Windows-Windows H.264 first-frame plus reconnect fresh-frame gate, and FUSIONDESK_VALIDATE_PC_H264_PRODUCTION validates the product rollout path where normal default codec preference selects windows.media_foundation.h264 without exact validation-only CLI pinning; H.264 validation forces agent capture to GDI so the codec/display path is not blocked by DXGI static-desktop first-frame timeouts.
fusiondesk_display_color_conversion_tests covers pure CPU BGRA-to-NV12 and NV12-to-BGRA conversion for the future codec adapter path, including deterministic 2x2 BT.601 limited-range output, plane sizing, unsupported pixel format rejection, odd-dimension rejection, short-stride rejection, and truncated input rejection.
fusiondesk_display_mvp_tests drops congested delta frames and preserves keyframe recovery.
fusiondesk_display_mvp_tests mounts display.screen from RuntimeHost ProductProfile into role-specific modules.
RuntimeHost uses ModuleComposer and DisplayModuleFactory for that profile-driven display mount.
fusiondesk_display_capture_backend_selection_tests covers the pure production capture backend matrix selector for Windows DXGI/WGC/GDI, unavailable-backend fallback reasons, source/platform/architecture/SoC parsing and filtering, Wayland PipeWire permission, Android client render-only, RK Linux DRM selection, and generic Linux LoongArch64 allowance.
fusiondesk_display_capture_backend_factory_tests covers the reusable StaticDisplayCaptureBackendFactory and DisplayCaptureBackendFactoryRegistry path for unavailable capability slots, aggregate selection, rejected-message diagnostics, and full-platform unavailable default backend factory coverage.
fusiondesk_display_capture_platform_plan_tests covers the role-aware full-platform capture startup plan, including client render-only handling, probed-factory capability selection, and unavailable default-matrix diagnostics when a platform adapter factory is not linked.
fusiondesk_display_capture_backend_failover_tests covers the pure runtime backend failover planner and factory-create helper, including failed backend exclusion, DXGI to WGC/GDI fallback planning, explicit requested-backend blocking, emergency override, and selected backend creation.
fusiondesk_display_runtime_service_tests covers the first executable switch-backend recovery path through DisplayRuntimeService and DisplayAgentModule::replaceCapture, including a fresh keyframe after failover.
fusiondesk_display_runtime_service_tests covers client-mode first-frame timeout monitoring, including a `FirstFrameTimeout` keyframe Request sent over small_data when no frame has rendered and no keyframe request is pending.
fusiondesk_display_runtime_service_tests covers client-mode reconnect fresh-frame monitoring, including a `Reconnect` keyframe Request retry when a reconnect resume baseline does not advance after channel rebind.
fusiondesk_pc_two_peer_start_display_smoke covers PC client `--start-display` starting DisplayRuntimeService in client monitoring mode and emitting display runtime diagnostics without breaking the two-process rendered-frame path.
fusiondesk_display_capture_recovery_tests covers pure recovery cooldown and repeated recoverable-failure promotion policy, and fusiondesk_display_runtime_service_tests covers cooldown blocking plus promoted switch-backend execution through the service snapshot counters.
fusiondesk_display_runtime_service_tests covers DisplayRuntimeService basic capture health metrics for pump count, frame attempts, frame timestamps, last observed frame age, effective FPS x1000, consecutive frame misses, service-observed captured/encoded/sent byte counters, last frame byte sizes, and effective bitrate Kbps; fusiondesk_pc_two_peer_start_display_smoke covers the PC display.runtime diagnostics path that prints those fields.
PC agent --start-display passes the Windows aggregate capture factory and selection request into DisplayRuntimeService so the real shell path can use the same failover owner when backend auto mode is active.
PC shell --print-display-capture-plan emits stable capture plan diagnostics; fusiondesk_pc_agent_start_display_requires_ready_channel_smoke asserts the probed-factory source, requested source type, requested GDI backend, architecture, SoC, selected backend, candidate rows, --print-display-sources source-catalog ok/provider/rejected/message rows with source type/native handle, selected-source match fields, selected=1 source rows, session display byte counters, and the Windows window-capture guard that does not fall back to monitor-only DXGI/GDI while WGC rollout is not enabled.
PC shell --print-display-codec-plan emits stable codec plan diagnostics; fusiondesk_pc_agent_start_display_requires_ready_channel_smoke asserts local two-peer codec negotiation diagnostics, raw fallback negotiation selection, the rollout-gated MediaFoundation H.264 rejection row, selected codec id, and backend id.
SessionRuntimeDiagnosticsSnapshot carries display codec status rows from DisplayAgentSnapshot and DisplayClientSnapshot; PC shell --print-session-diagnostics emits session.diagnostics.display_codec rows with selected adapter, codec, backend, selectionMode, fallbackReason, deltaFrames, payload/error counters, and delayed decoder counters. fusiondesk_pc_agent_start_display_requires_ready_channel_smoke and fusiondesk_pc_two_peer_start_display_smoke assert this product diagnostics surface.
DisplayProductDiagnosticsSnapshot/buildDisplayProductDiagnostics and DisplayProductHealthPresentation/buildDisplayProductHealthPresentation provide the pure runtime display health reader/presenter for product UI/service code; PC shell --print-session-diagnostics also emits session.diagnostics.display_health rows with status, action, captureState, codecState, fallback, delay, and recovery warnings. fusiondesk_session_mainline_tests covers ready and blocked summaries plus presentation codes, fusiondesk_pc_agent_start_display_requires_ready_channel_smoke covers blocked shell health output, and fusiondesk_pc_two_peer_start_display_smoke covers the real agent/client health rows including H.264 production mode.
PcProductSessionController is the first PC product startup seam above the shell: it owns RuntimeHost, SessionMainline creation, QtRuntimeTransportManager, QtPeerProfileRuntimeService, typed display.codec.v1 inventory/FDPP/codec-create/dependency-mount helper flow, QtReconnectRuntimeService, DisplayRuntimeService, display pump timer lifecycle, optional QWidget display window lifecycle, profile mount/start continuations, and direct SessionRuntimeDiagnosticsSnapshot plus DisplayProductHealthPresentation reads for future PC UI/service callers. fusiondesk_pc_product_session_controller_tests covers agent startup, peer-profile owner activation and request entry, controller-level display.codec.v1 inventory building, H.264 negotiation, raw fallback, missing-extension failure, role request pinning, raw encoder/decoder creation, runtimeInfo publication, role-specific DisplayMvpDependencies build/mount, reconnect owner activation, display mount, optional QWidget window creation/status update/close, display runtime start/stop, health state transitions, and required-channel module-start blocking.
fusiondesk_windows_gdi_display_capture_tests covers the first Windows capture adapter open/close seam, selector-backed GDI capture factory creation, GDI/DXGI/WGC source catalog creation through the backend factory contract, GDI/DXGI/WGC source catalog topology shape, strict invalid source-id rejection with SourceNotFound, default-probed DXGI direct-create path, rollout-gated WGC monitor/window capture factory creation, FUSIONDESK_ENABLE_WGC_CAPTURE disabled/not-enabled/enabled-probed diagnostics, captureErrors diagnostics, Windows cursor-to-frame mapping for software cursor overlay, and the Windows aggregate capture factory fallback path.
fusiondesk_windows_wgc_display_capture_opt_in_tests skips by default and validates a real WGC monitor BGRA frame only when FUSIONDESK_VALIDATE_WGC_CAPTURE=1 is set.
fusiondesk_pc_agent_start_display_requires_ready_channel_smoke covers PC shell `--display-no-cursor` propagation into capture plan and session diagnostics through `includeCursor=0`.
cmake --list-presets=all . validates the host/cross-target preset set, and cmake --preset windows-host-release plus a preset target build validates the first host preset path.
fusiondesk_qt_image_display_renderer_tests covers the first Qt image render adapter seam.
fusiondesk_qt_image_display_window_tests covers the first QWidget display surface adapter bound to QtImageDisplayRenderer.
fusiondesk_pc_client_display_mount_smoke and fusiondesk_pc_agent_display_mount_smoke cover explicit PC shell display adapter dependency mounting.
fusiondesk_pc_client_visible_display_mount_smoke covers PC client `--show-display-window` mounting the first QWidget visible display surface, and fusiondesk_qt_image_display_window_tests covers the status line used by the PC shell health presenter binding.
FusionDesk test targets undefine NDEBUG so Release CTest executes assertions.
```

Pre-entry evidence from G4:

```text
display.screen has role-scoped agent/client manifests or distinct module ids.
ModuleCatalog can filter the remote desktop suite by role.
ModuleHost role/platform gates pass before attach.
ModuleHost checks required module dependencies before start.
ModuleHost checks required channel readiness before start.
ModuleHost applies policy before start.
ModuleIngressRegistry routes are active only for running modules.
ModuleIngressRegistry unregisters routes on stop/detach.
fusiondesk_module_catalog_tests passes.
fusiondesk_module_ingress_tests passes.
fusiondesk_module_factory_tests passes.
```

Exit evidence:

```text
Agent session starts display.screen.agent through ModuleHost.
Client session starts display.screen.client through ModuleHost.
Agent sends first VIDEO packet through NetworkRouter.
Client receives VIDEO packet through manifest ingress.
Client renderer receives first frame.
Client sends PAYLOAD_ACK Request for keyframe recovery.
Agent sends Response and emits a keyframe VIDEO packet.
congestion drops stale delta frames rather than growing latency forever.
client receive path drops stale frames before decode and requests keyframe recovery on delta frame gaps.
basic display counters expose captured, encoded, sent, dropped, received, decoded, rendered, decode error, render error, frame-gap, and keyframe recovery counts.
fusiondesk_display_mvp_tests passes.
```

Minimum test route:

```text
agent/client session pair
fake transport or in-process router bridge
fake capture
fake encoder
fake decoder
fake renderer
first-frame render assertion
keyframe request/response assertion
video pressure/drop assertion
```

Forbidden:

```text
display module must not open sockets
display module must not call app window private APIs
display agent and display client responsibilities must not be mixed in one concrete class
```

## Gate P3.5 - Real Transport And Qt Boundary

Goal:

```text
Connect FusionDesk core packet transport to Qt without leaking Qt into core or modules.
```

Scope:

```text
optional Qt adapter CMake target
optional Qt runtime CMake target
Qt TCP transport socket adapter
Qt packet channel adapter
Qt channel binder adapter
Qt transport profile connector
Qt runtime transport manager
Qt TCP transport profile JSON loader
Qt TCP peer profile coordinator
Qt timer bridge
Qt event loop bridge
PacketCodec bytes over Qt TCP
PC client and agent shell smoke targets
```

Current evidence:

```text
fusiondesk_qt_adapters builds only when Qt Core/Network is found.
Qt discovery can use qmake to seed CMAKE_PREFIX_PATH.
QtTcpTransportSocket lives under adapters/qt.
QtTcpTransportSocket implements ITransportSocket for byte writes.
QtTcpTransportSocket can adopt an accepted QTcpSocket for server-side tests.
QtTcpTransportSocket detaches adopted sockets from their Qt parent before ownership transfer.
PacketCodec can inspect frame boundaries for TCP stream parsing.
QtPacketChannel lives under adapters/qt.
QtPacketChannel implements IChannel on top of QtTcpTransportSocket and PacketCodec.
QtChannelBinder lives under adapters/qt.
QtChannelBinder registers Qt transports into SocketGroup, registers or reuses ChannelSpec, binds QtPacketChannel, and marks ChannelRegistry ready.
QtSessionTransportConnector lives under runtime/qt.
QtSessionTransportConnector creates client-side Qt TCP transports from profiles and adopts accepted server-side QTcpSocket instances.
QtRuntimeTransportManager lives under runtime/qt and owns QtSessionTransportConnector instances plus QTcpServer listeners per session id.
runtime/qt can generate matching Qt TCP peer profile pairs, save them as JSON, and resolve tcpChannels/tcpListenChannels against known ChannelSpec entries.
runtime/connection has a pure C++ peer connection plan resolver that validates requested ChannelKey plans against known ChannelSpec entries without Qt.
runtime/connection has `PeerProfileService`, a pure C++ control-channel `PacketEnvelope` Request/Response service for `FDPP` peer profile exchange payloads.
runtime/connection has `PeerProfileRuntimeService`, a pure owner for caller-side FDPP dispatch, response subscriptions, RequestTracker completion, timeout expiry, and snapshots without Qt, JSON, sockets, or Session state.
runtime/qt has `QtPeerProfileRuntimeService`, a bridge that applies FDPP tcpListenChannels/tcpChannels through QtRuntimeTransportManager without moving Qt into runtime/connection or packet construction into apps.
runtime/connection has a pure C++ reconnect orchestration plan that maps service-selected degraded ChannelKey values to client replacement channels, agent replacement listeners, reason/keyframe intent, and teardown-after-success intent without Qt.
runtime/connection has ReconnectRuntimeService as the common owner for ReconnectCoordinator, RequestTracker, ReconnectTeardownService, and teardown executor.
runtime/connection has ReconnectDiagnosticsReport and exposes it through ReconnectRuntimeServiceSnapshot::diagnostics for plan, replacement, Session rebind, teardown, and timeout status.
runtime/tunnel has TunnelReconnectExecutor and ITunnelReplacementBackend as the first relay/direct/P2P replacement executor contract below ReconnectCoordinator.
runtime/tunnel has TunnelCandidateProfile negotiation and ITunnelTransportFactory as the pure candidate selection and concrete adapter boundary; factory requests can carry local and peer candidates.
modules/feature has lifecycle-only FeatureModuleFactory skeletons for the non-display feature module manifests, and RuntimeHost registers them in ProductProfile composition.
modules/input has role-specific input.mouse/input.keyboard client/agent contract modules with FDIN payloads.
modules/clipboard has the active data-redirection framework, FDCL payloads, runtime owner APIs, Windows endpoint seams, and PC dry-run/native validation entry points.
platform/windows has a dry-run input adapter seam.
adapters/qt has a Qt input capture seam.
PC shell startup can mount input feature profile modules and inject the first Qt/Windows adapter dependencies without moving Qt or OS APIs into modules.
runtime/feature has FeatureRuntimeService, a pure C++ owner that polls input capture and drives input module APIs without Qt, OS APIs, or direct packet construction.
PC shell startup can run that owner with --pump-profile-modules.
runtime/feature has FeatureRuntimePolicy as a pure foundation for feature operation allow/deny/audit.
FeatureRuntimeService can own input capture open/close when it is the active pump owner, avoiding shared capture lifecycle ownership in future production startup.
QtReconnectExecutor lives under runtime/qt and adapts the pure IReconnectReplacementExecutor contract to QtReconnectOrchestrator for the local Qt TCP replacement path.
QtReconnectRuntimeService lives under runtime/qt and adds QtReconnectExecutor plus QtTimerBridge without moving Qt into runtime/connection.
QtTcpPeerProfileCoordinator lives under runtime/qt and turns a local multi-endpoint TCP connection plan into validated client-connect and agent-listen profile JSON files.
fusiondesk_pc_profile_plan exposes the local peer profile plan path as a startup-script CLI that writes paired client/agent JSON from named MVP channels.
PC shell startup can bind no-sessionId Qt TCP transport profiles to the RuntimeHost session it just created.
PC product startup now also has `PcProductSessionController`, so production UI/service code has a reusable owner for session creation, Qt transport manager lifetime, FDPP peer-profile runtime startup/request/apply, typed display.codec.v1 inventory/request/response/completion/codec-create/dependency-mount helper flow, reconnect runtime startup, display runtime startup/pump/stop, optional QWidget display window lifecycle, profile mount/start, and structured diagnostics instead of duplicating CLI wiring or parsing shell stdout.
QtRuntimeTransportManager reports listener startup through listeningChannels and reserves readyChannels for actually bound channels.
QtRuntimeTransportManager can load and apply Qt TCP transport connect/listen profile JSON files through startup-facing calls.
QtTimerBridge lives under runtime/qt and wraps QTimer behind a standard C++ callback API.
QtEventLoopBridge lives under runtime/qt and wraps QCoreApplication post/process/runUntil behind a standard C++ callback API.
fusiondesk_pc_client and fusiondesk_pc_agent are thin QCoreApplication shells that create RuntimeHost, start role-specific sessions through SessionMainline, and optionally apply --transport-profile, --listen-profile, FDPP peer-profile startup negotiation, or FDMI module inventory exchange to the current shell session.
PC shell startup can use --peer-profile-service on the agent and --peer-profile-channel on the client to negotiate non-bootstrap channels over an already-ready control channel through QtPeerProfileRuntimeService.
PC shell startup can use --module-inventory-service on the responder side and --module-inventory-request on the requester side to exchange FDMI after profile module mount and before SessionMainline module start. `--print-module-inventory-diagnostics` prints pending, completion, response status, and responder counters.
PC shell session diagnostics include remote module inventory rows after a successful FDMI exchange.
PC shell --print-reconnect-diagnostics prints ReconnectRuntimeServiceSnapshot::diagnostics at reconnect completion/failure and exit lifecycle points.
PC shell --start-display waits for required module channels using LinkChannelBindingReport and --wait-channels-ms before SessionMainline module start.
SessionRuntimeDiagnosticsSnapshot exposes the same link/channel and module startup state through a pure runtime/session reader for future product UI/service consumption.
PC shell --print-session-diagnostics emits that snapshot as stable stdout records for shell scripts and future UI/service integration.
PC shell --run-ms provides bounded event-loop execution for startup smoke tests.
PC shell --require-display-frame checks display sent/rendered counters after bounded startup runs.
fusiondesk_qt_tcp_transport_tests sends a PacketEnvelope Request over Qt TCP and decodes it with PacketCodec.
fusiondesk_qt_tcp_transport_tests sends a PacketEnvelope Response back over Qt TCP and decodes it with PacketCodec.
fusiondesk_qt_tcp_transport_tests binds QtPacketChannel through QtChannelBinder, validates SocketGroup and ChannelRegistry ready state, sends with NetworkRouter, and receives through peer NetworkRouter subscriptions.
fusiondesk_qt_tcp_transport_tests connects a Qt TCP channel through QtSessionTransportConnector profile data and routes packets through NetworkRouter.
fusiondesk_qt_tcp_transport_tests applies no-sessionId tcpChannels/tcpListenChannels through the single-session QtRuntimeTransportManager entry points, accepts an inbound client connection, adopts it into the agent session NetworkManager, marks the channel ready, and routes generic PayloadAck traffic.
fusiondesk_qt_tcp_transport_tests applies coordinator-generated multi-endpoint profile files through QtRuntimeTransportManager, starts distinct control/video listeners and connectors, and marks both ChannelRegistry entries ready.
fusiondesk_qt_tcp_transport_tests wires RuntimeHost-mounted display agent/client sessions through JSON-loaded QtRuntimeTransportManager profile data using the one-call apply path, renders first frame, and completes keyframe request/response over Qt TCP with VIDEO on main_screen and PAYLOAD_ACK on small_data.
fusiondesk_qt_tcp_transport_tests applies a reconnect orchestration plan through QtReconnectRuntimeService, ReconnectRuntimeService, ReconnectCoordinator, and QtReconnectExecutor, verifies both sessions record the planned reason/keyframe intent, closes old transports after rebind, and routes PayloadAck over the replacement channel.
fusiondesk_qt_timer_bridge_tests drives RequestTracker timeout expiry through QtTimerBridge.
fusiondesk_qt_event_loop_bridge_tests runs posted callbacks and immediate predicates through QtEventLoopBridge.
fusiondesk_qt_peer_profile_coordinator_tests covers local multi-endpoint connection-plan validation, requested channel resolution against known ChannelSpec entries, client/agent profile pairing, canonical JSON save/load roundtrip, no-sessionId shell profile shape, and missing/duplicate/unknown channel plus duplicate endpoint failure reporting without starting sockets.
fusiondesk_peer_connection_plan_tests covers the pure C++ peer connection plan resolver for known channel resolution and invalid plan rejection.
fusiondesk_peer_profile_service_tests covers `FDPP` request/response envelope roundtrip, PacketCodec/ProtocolValidator compatibility, service-side control-route handling, terminal Response generation, and unrelated control payload ignore.
fusiondesk_peer_profile_runtime_service_tests covers `PeerProfileRuntimeService` dispatching FDPP profile requests, completing a loopback control-channel response through RequestTracker, expiring unanswered requests, and snapshotting caller-side state.
fusiondesk_qt_peer_profile_runtime_service_tests covers `QtPeerProfileRuntimeService` using an existing control channel to exchange FDPP, auto-start agent listening for a requested main_screen channel, apply the client main_screen connect profile, and leave that requested channel ready on both sessions.
fusiondesk_reconnect_orchestration_plan_tests covers service reconnect plan success, client/agent side replacement intent, reason/keyframe/teardown preservation, and empty/duplicate/unknown/unplanned degraded-channel rejection.
fusiondesk_reconnect_diagnostics_report_tests covers the combined reconnect diagnostics builder and ReconnectRuntimeService pending/expired teardown snapshot path.
fusiondesk_tunnel_reconnect_executor_tests covers TunnelReconnectExecutor mapping ReconnectOrchestrationSidePlan to tunnel backend requests and running through ReconnectCoordinator without changing Session; it also covers candidate profile selection and client/agent transport factory request construction with local/peer candidates.
fusiondesk_module_factory_tests and fusiondesk_runtime_host_tests cover non-display feature module factory composition and RuntimeHost profile mount of skeleton modules.
fusiondesk_feature_contract_tests covers input mouse/keyboard Event routing through ModuleHost and NetworkRouter.
fusiondesk_windows_feature_adapter_tests covers the Windows input dry-run adapter seam.
fusiondesk_qt_input_capture_tests covers Qt input event conversion.
fusiondesk_pc_feature_adapter_startup_smoke covers PC shell feature adapter profile mounting and the required-channel gate for generic profile module start.
fusiondesk_qt_image_display_renderer_tests renders a decoded software frame through QtImageDisplayRenderer when Qt Gui is available.
fusiondesk_pc_client_smoke and fusiondesk_pc_agent_smoke start the thin PC shells with --smoke.
fusiondesk_pc_agent_listen_profile_smoke starts the PC agent shell with a no-sessionId --listen-profile bound to the current RuntimeHost session.
fusiondesk_pc_profile_plan_smoke starts fusiondesk_pc_profile_plan, verifies generated multi-channel profile JSON, and rejects unknown channel names.
fusiondesk_pc_agent_start_display_requires_ready_channel_smoke proves PC agent --start-display refuses to start display before required channels are ready.
fusiondesk_pc_two_peer_start_display_smoke starts real PC agent/client executables with fusiondesk_pc_profile_plan generated no-sessionId control/small_data/main_screen listen/connect profiles, required-channel-gated --start-display, bounded --run-ms execution, client rendered-frame verification, delayed reconnect through QtReconnectRuntimeService, post-reconnect rendered-frame progress, display runtime diagnostics, and --print-reconnect-diagnostics output from the runtime snapshot.
fusiondesk_pc_peer_profile_start_display_smoke starts real PC agent/client executables with JSON only for control-channel bootstrap, then uses QtPeerProfileRuntimeService FDPP exchange to negotiate small_data/main_screen and FDMI module inventory exchange before required-channel-gated display startup, including module inventory completion diagnostics and remote module session diagnostics.
fusiondesk_pc_client_display_mount_smoke and fusiondesk_pc_agent_display_mount_smoke mount role-specific display adapter dependencies through PC shells.
fusiondesk_source_purity_scan keeps current core/module purity enforced by CTest.
```

Exit evidence:

```text
PC shell startup can invoke QtRuntimeTransportManager with loaded Qt TCP connect/listen profile data.
PC-side local profile generation can prepare those files through a CLI without app code or launch scripts knowing the profile JSON schema.
runtime/connection can prepare a service-backed reconnect plan without creating concrete sockets or calling Session.
runtime/qt can execute the local TCP replacement form of that plan without moving rebind ownership out of Session.
Core RuntimeHost/Session startup remains transport-agnostic and does not auto-load Qt profiles.
channel init can bind and mark ChannelRegistry ready through the runtime Qt transport path.
PC client and agent shells can create RuntimeHost and attach/listen Qt transport adapters in real connection startup tests.
```

Forbidden:

```text
no Qt includes in include/fusiondesk/core or src/core
no Qt socket ownership in feature modules
no direct app-window dependency from modules
```

## Gate P4 - Input MVP

Goal:

```text
Add interactive control without polluting display or network ownership.
```

Scope:

```text
input.keyboard
input.mouse
input.touch
input.gamepad
IInputCapture
IInputInjector
shortcut policy
optional UDP optimization boundary
```

Exit evidence:

```text
Client input capture sends keyboard, mouse, and touch packets.
Agent input injector receives and injects packets through platform adapters.
special shortcut policy is evaluated before injection.
input is ordered independently from display video.
```

Pre-MVP contract evidence:

```text
input.mouse and input.keyboard resolve to role-specific client/agent modules.
FDIN payloads carry sequence, timestamp, modifiers, mouse coordinates/buttons, and keyboard key data.
InputClientModule emits Event NoResponseRequired packets on small_data.
InputAgentModule consumes through ModuleIngressRegistry and calls IInputInjector.
First Windows and Qt adapter seams exist, PC shell startup can inject them into profile-mounted modules, FeatureRuntimeService can pump captured mouse/keyboard events through module APIs, and FeatureRuntimePolicy can deny/audit operations before send; production global/raw capture, shortcut policy, and concrete platform owner adapters remain pending.
```

## Gate P5 - Enterprise Data Redirection

Goal:

```text
Add clipboard, filesystem, and printer as governed enterprise modules.
```

Scope:

```text
clipboard.redirect
filesystem.redirect
printer.redirect
data-transfer stream model
request id and chunk id protocol
policy direction model
audit events
```

Exit evidence:

```text
directional allow/deny works.
audit event is emitted before external data leaves a boundary.
large transfers are chunked and throttled.
bulk channel pressure cannot block display or input.
```

Current status:

```text
Clipboard CLIP-01 through CLIP-09 framework work is now in the active FusionDesk
slice: pure TransferSourceBundle types, role-specific clipboard.redirect
modules, FDCL v1 FormatList/ReadFormat/FileRange/ObjectLock/drag operation
codec paths, fake endpoint tests, runtime owner APIs, Windows endpoint seams,
large_data file range scheduling/backpressure, FDCL Ack-driven large_data window
release, and static plus provider-based dynamic drag coordinate pre-reserved
paths exist.
RuntimeHost/SessionManager reconnect cleanup now has a focused smoke test for
clipboard pending large_data reservations, object locks, and ingress replay.
An in-process two-peer reconnect smoke sends FDCL traffic through bridged
client/agent sessions and verifies dropped large_data Ack cleanup on reconnect.
`fusiondesk_pc_two_peer_clipboard_reconnect_smoke` starts real PC agent/client
processes over generated Qt TCP profiles, reconnects only the `large_data`
channel while clipboard runtime is active, suppresses display fresh-state
requests with `--reconnect-no-display-keyframe`, verifies reconnect diagnostics
for the replacement channel, and still requires the client endpoint to expose
the expected dry-run clipboard text after reconnect.
`fusiondesk_pc_two_peer_clipboard_file_smoke` starts real PC agent/client
processes, seeds the agent dry-run Windows endpoint with directory and loose
local file paths, requires the client to read multiple advertised files through
FDCL LockObject/FileRange/UnlockObject with a bounded read chunk that forces
multiple FileRange windows, materializes the remote offer into a local output
directory, verifies saved file contents, and asserts both sides report object
lock/unlock plus file-range request/response byte counters.
`fusiondesk_pc_two_peer_clipboard_drag_smoke` starts real PC agent/client
processes, sends drag start/move/drop coordinates for the current clipboard
offer, and verifies the peer dry-run Windows endpoint records the drag lifecycle
without embedding file or clipboard bytes in coordinate messages.
`fusiondesk_pc_two_peer_clipboard_text_smoke` now starts real PC agent/client
processes over generated Qt TCP profiles, seeds the agent Windows endpoint in
dry-run mode, requires the client to observe the expected text, and covers the
app shell -> runtime owner -> clipboard module -> QtPacketChannel path. This
validates process/network/module closure for plain text, while real native
Windows clipboard access in an interactive desktop session remains a separate
manual/opt-in gate. The same smoke accepts
`FUSIONDESK_VALIDATE_PC_NATIVE_CLIPBOARD_TEXT=1` to switch both processes to
`--windows-clipboard-native`, seed through `--clipboard-seed-text`, disable
owner suppression for same-process verification, and print endpoint diagnostics.
Native Windows `OpenClipboard` access is bounded-retried and can be tuned with
`--clipboard-open-retry-count` / `--clipboard-open-retry-delay-ms` for
interactive desktop validation. The two-process native smoke also accepts
`FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_COUNT` and
`FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_DELAY_MS` to pass those overrides to
both child processes.
The client side uses `--clipboard-require-wait-ms` so the shell keeps pumping
transports/runtime while waiting for native clipboard materialization.
`fusiondesk_pc_two_peer_clipboard_image_smoke` starts real PC agent/client
processes over generated Qt TCP profiles, publishes a local canonical PNG file
through `--clipboard-seed-image-png`, and requires the client endpoint to expose
exact matching `image/png` bytes through `--require-clipboard-image-png`. This
keeps the image copy path under non-interactive CTest while native desktop image
clipboard validation remains a separate endpoint/platform gate.
The Qt clipboard endpoint now also snapshots local `file://` QMimeData URLs as
FDCL FileList offers and attaches a pure local file range provider, so Linux,
macOS, and other Qt hosts have a first non-Windows source-side file provider
for ordinary file-list clipboard offers. `fusiondesk_qt_clipboard_endpoint_tests`
sets recursive local file URLs, validates the advertised FileList, and reads
file bytes through the provider. Remote file publication into non-Windows native
clipboards/pasteboards remains platform-specific follow-up work.
The first native macOS AppKit endpoint now exists behind
`platform/macos/clipboard`: it snapshots `NSPasteboard` text/html/rtf/png,
TIFF-to-PNG, and file URL offers, uses `NSPasteboardItemDataProvider` for lazy
remote text/rich/image publication, polls `changeCount` as the endpoint change
monitor, and publishes remote FDCL FileList offers as `NSFilePromiseProvider`
objects whose delegates stream the promised files through FDCL
LockObject/FileRange/UnlockObject when a local target app fulfills the promise.
The APPLE-only smoke now covers delayed text publication and remote FileList
promise publication counters; the target still needs a real macOS
compile/runtime gate.
`QtClipboardEndpoint` text, HTML, RTF, and PNG image publication now verify that
the published QMimeData or image is observable on QClipboard before reporting success. The endpoint
also snapshots local `text/html` or `text/rtf` QMimeData with a plain-text
fallback as one source bundle and local QClipboard images as canonical
`image/png`, so Qt-backed non-Windows hosts have canonical rich-text and image
paths. The PC text smoke has an opt-in
`FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_TEXT=1` gate that forces the client to use
`--clipboard-endpoint qt`; it remains an environment gate because Windows OLE
clipboard access must be available for QClipboard publication.
The Windows endpoint now also maps and publishes registered
`Rich Text Format` clipboard data as canonical `text/rtf`, including dry-run
snapshot/publication and delayed-rendered native offers. It also maps and
publishes registered `PNG` clipboard data as canonical `image/png` for byte
passthrough, and transcodes Windows CF_DIB/CF_DIBV5 image data to and from
canonical `image/png` for ordinary Windows image clipboard interoperability.
It also carries `image/x-dib` as a Windows-fidelity native passthrough format
inside the same `TransferSourceBundle` when a DIB clipboard snapshot is
available.
`tools/clipboard_windows_validation.ps1` wraps the same PC agent/client
startup path for manual desktop validation. It can run a local native text
roundtrip, split agent/client validation across two Windows desktops by shared
agent LAN endpoint, run generated `Html`/`Rtf` scenarios for formatted text,
run a generated recursive file scenario for multi-file FDCL
LockObject/FileRange/UnlockObject reads with configurable read chunk windows,
materialize the remote file offer into a local directory with `-SaveFilesDir`,
run a drag-preflight scenario for remote file drag offers, or use `-DryRun` to
validate only runtime/network/module closure. Non-dry-run Text, Html, Rtf, and
Image scenarios preflight `OpenClipboard` before child process launch so
non-interactive sessions fail with an explicit native error instead of a late
agent/client requirement timeout. The native drag-preflight path now also
asserts the remote LockObject/FileRange/UnlockObject request and response
counters behind the bounded FileContents stream read.
`fusiondesk_clipboard_windows_validation_text_dryrun_smoke` keeps the
non-interactive Text wrapper path under CTest,
`fusiondesk_clipboard_windows_validation_image_dryrun_smoke` keeps the
non-interactive Image wrapper path under CTest,
`fusiondesk_clipboard_windows_validation_rtf_dryrun_smoke` keeps the
non-interactive RTF wrapper path under CTest,
`fusiondesk_clipboard_windows_validation_file_smoke` keeps the generated File
scenario under CTest when PowerShell is available, and
`fusiondesk_clipboard_windows_validation_drag_preflight_smoke` keeps the native
drag preflight wrapper under CTest without entering the blocking DoDragDrop loop.
`fusiondesk_windows_clipboard_remote_file_stream_tests` now also covers a test
`IDropTarget` reading `FileGroupDescriptorW` and the first `FileContents`
`IStream` lazily through DragEnter/DragOver/Drop, so the automated Windows
platform evidence includes both the OLE data object and the drop-target read
side before the manual blocking DoDragDrop gate.
The same manual validation wrapper now also has `-Scenario DragLoop` for an
interactive desktop gate: it enables the real Windows `DoDragDrop` loop, waits
for the user to move the generated native drag over a local target and release
the mouse before `-DragLoopTimeoutMs`, and asserts native drop plus remote
LockObject/FileRange/UnlockObject lazy stream counters. This scenario is not
registered as CTest because it intentionally blocks on user input.
In the current non-interactive session the native clipboard preflight fails at
`OpenClipboard` with access denied, so it is an environment validation gate
rather than default CTest evidence.
Canonical/native format mapping, same-platform native passthrough, and
cross-platform canonical transcoding are documented as the required format
contract. A pure ClipboardRuntimeService owner skeleton now pumps endpoint
snapshots through clipboard module APIs with policy/audit hooks.
The runtime policy hook is now configurable in code and in the PC shell:
`ConfigurableClipboardRuntimePolicy` records authorize/audit counters, last
operation metadata, and a bounded recent audit-event snapshot; the shell shares
one policy instance between `ClipboardRuntimeService` and
`ClipboardRuntimeRemoteReader`, and `--print-clipboard-diagnostics` emits
`clipboard.runtime_policy` summary rows plus per-operation `clipboard.audit`
rows for product UI/service handoff without logging clipboard content.
The retained audit-event window is configurable through
`--clipboard-runtime-max-audit-events` and the policy-file runtime field
`maxRecentAuditEvents`.
`PcProductSessionController` now owns a product-level clipboard runtime seam:
it can start/stop `ClipboardRuntimeService`, pump clipboard changes, expire
pending reads, default the runtime to the active controller session, and expose
clipboard runtime plus runtime-policy snapshots from `PcProductSessionSnapshot`.
`ProductProfile.clipboardPolicy` is now the persistent product policy object
for clipboard module allow/deny/size limits and runtime allow/deny/audit rules;
PC shell clipboard policy switches populate it, RuntimeHost uses it when
mounting clipboard modules, and the controller uses it when no explicit runtime
policy is injected. PC shell `--clipboard-policy-file` loads the same product
policy object from JSON before CLI overrides and rejects invalid policy files at
startup.
`ClipboardProductPolicyPresentation` now gives product UI/service callers a
stable policy view, and PC shell diagnostics emits the same view as
`clipboard.policy`.
`ClipboardProductHealthPresentation` now folds clipboard runtime and policy
snapshots into stable product status/action/runtime/policy state codes, and PC
shell `--print-clipboard-diagnostics` emits `clipboard.health` rows for the
same presentation.
`fusiondesk_qt_tcp_transport_tests` includes a reentrant dispatch regression
for QtPacketChannel so synchronous clipboard remote reads cannot redispatch the
current inbound frame during nested event polling.
`QtClipboardEndpoint` still has a non-production validation fallback that can
publish remote FDCL file offers by materializing FileList entries into an
endpoint-owned temporary directory and publishing local `file://` URLs through
QClipboard. It verifies that those URLs are observable before reporting
success, so unavailable desktop clipboard access is surfaced as a publication
failure instead of a false success. This fallback is not the Windows or macOS
production remote-file strategy: Windows uses OLE FileContents lazy streams,
macOS uses AppKit file promises, and Linux owns the local URI/file-path
presentation through the future FUSE/portal adapter.
`fusiondesk_qt_clipboard_endpoint_tests` covers the Qt fallback path with a
fake remote reader/locker.
PC shell can select clipboard endpoint adapters through
`--clipboard-endpoint auto|windows|macos|qt`. `auto` keeps the linked native OS
endpoint as the default; explicit `qt` creates a `QGuiApplication`,
instantiates `QtClipboardEndpoint`, and prints `clipboard.endpoint kind=qt`
diagnostics with remote file materialization counters. A macOS build can select
`macos` and emit `clipboard.endpoint kind=macos` diagnostics for AppKit
pasteboard and file-promise publication.
PC shell also has `--require-clipboard-endpoint-file-text`, which reads file
contents through the selected endpoint's native/local file provider. The PC
file smoke covers that path against the agent's Windows dry-run local file
source. `FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_FILE=1` turns the same smoke into
an opt-in Qt endpoint remote-file publication gate, but it requires an
available desktop clipboard for QClipboard fallback publication.
The new clipboard design must keep FDCL-style module-owned payload compatibility,
PacketEnvelope Request/Response correlation, content policy, chunking, audit,
and watcher ownership inside the module/runtime adapter boundary.
PC shell can now export the normalized effective clipboard product policy with
`--clipboard-policy-export-file`, using the same JSON schema as
`--clipboard-policy-file` after file loading and CLI overrides are applied.
`CLIPBOARD_REDIRECTION_FOUNDATION.md` is the consensus baseline for that rebuild.
Remaining work: passing interactive native Windows clipboard validation,
recording an interactive native DoDragDrop/drop-target validation run outside
CTest, macOS native compile/runtime plus interactive file-promise validation,
Linux FUSE/portal remote-file endpoint, enterprise policy service
integration, and a polished product policy/audit UI over the bounded runtime
diagnostics.
```

## Gate P6 - Android Controller

Goal:

```text
Expose the Qt-based remote desktop controller as an Android embeddable library.
```

Scope:

```text
Android Java/Kotlin facade
JNI opaque controller handle
Surface attach/detach
runtime lifecycle bridge
AAR packaging
arm64-v8a and x86_64 test ABI
```

Exit evidence:

```text
native controller can be created and closed.
connect and disconnect calls are asynchronous.
Surface destroyed pauses rendering without tearing down the session.
Surface recreated reattaches renderer and requests keyframe.
public API exposes Android types only, not Qt or JNI implementation types.
```

## Gate P7 - Tunnel And Direct Connection

Goal:

```text
Add relay, NAT traversal, and direct tunnel below the existing network contract.
```

Scope:

```text
SocketFactory
RelaySocket
P2PTunnelSocket
hole punching negotiation
fallback path selection
transport diagnostics
```

Exit evidence:

```text
TunnelReconnectExecutor can implement IReconnectReplacementExecutor.
Tunnel replacement requests preserve session id, degraded channels, reason, keyframe intent, teardown intent, candidate endpoint, and listener/connect side.
TunnelCandidateProfile can select compatible client/agent candidate pairs before concrete socket creation.
ITunnelTransportFactory exists as the concrete LAN/relay/direct socket adapter boundary and receives the selected local/peer candidates when needed.
modules do not change when tunnel transport is selected.
ChannelRegistry sees the same channel model for LAN, relay, and tunnel.
control channel survives tunnel fallback.
policy can forbid direct tunnel when enterprise profile requires relay.
```

## Gate P8 - Hardening And Release

Goal:

```text
Make the platform releasable for enterprise rollout.
```

Scope:

```text
crash recovery
observability
security review
protocol compatibility
installer packaging
license and policy migration
performance budgets
```

Exit evidence:

```text
Windows and Linux packages are produced by CI.
macOS is either green or explicitly optional.
Android AAR is reproducible.
protocol version compatibility tests exist.
session reconnect and module restart tests exist.
security review findings are tracked.
```
## Peer Profile Exchange Note

runtime/connection now also has a pure C++ peer profile exchange pair builder that turns resolved plans into client/agent profile pair data without Qt or sockets.
fusiondesk_peer_profile_exchange_tests covers that builder on success and invalid-plan rejection paths.
`PeerProfileService` moves that exchange from local pair-building into a control-channel `PacketEnvelope` Request/Response service. JSON remains local runtime/qt startup and smoke tooling; production exchange traffic is `FDPP` payload over the control route.
`PeerProfileRuntimeService` is the caller-side owner for that production exchange boundary. It sends FDPP requests, subscribes for response kinds, tracks pending requests with RequestTracker, records completed profile exchange results, expires timeouts, and snapshots state for future PC/Qt orchestration. fusiondesk_peer_profile_runtime_service_tests covers this frame.
`QtPeerProfileRuntimeService` is the Qt application bridge for that frame. It can auto-apply the agent listen side before sending the FDPP response and apply completed client connect profiles after the response, leaving the PC shell to call one runtime owner instead of hand-building transport JSON or packets. fusiondesk_qt_peer_profile_runtime_service_tests covers the bootstrap-control-to-main-screen path. PC shell now invokes that owner for startup when passed --peer-profile-service / --peer-profile-channel; fusiondesk_pc_peer_profile_start_display_smoke covers the real executable path.
## Peer Coordination Note

runtime/connection now also has a pure C++ peer coordination planner that combines peer profile exchange with reconnect hints for known channel plans.
fusiondesk_peer_coordination_tests covers the success path, replacement intent, teardown-after-success intent, and invalid reconnect-channel rejection.
## Service Reconnect Plan Note

runtime/connection now also has a pure C++ reconnect orchestration planner for service-backed reconnect. It rejects empty, duplicate, unknown, or unplanned degraded channels and emits client/agent replacement intent plus post-rebind teardown intent without creating sockets or calling Session. fusiondesk_reconnect_orchestration_plan_tests covers the planner.
ReconnectCoordinator provides the large pure C++ service frame above the planner: plan resolution, replacement executor, client reconnect handoff, and teardown executor sequencing. ReconnectRuntimeService owns the common coordinator/tracker/teardown composition. QtReconnectExecutor is the first concrete adapter for `IReconnectReplacementExecutor`, and QtReconnectRuntimeService adds the Qt timer/runtime owner. fusiondesk_reconnect_coordinator_tests covers the high-level frame with one smoke test.
## Reconnect Teardown Command Note

runtime/connection now also has a pure C++ old-transport teardown command/response/envelope contract for the post-rebind phase. It turns teardown-after-success target channels into correlated commands with `messageId`, `correlationId`, and `timeoutMs`, maps those commands to control-channel `PacketEnvelope` requests with the target channel carried in an `FDRT` payload, and only terminal `Response` or `Error` results can complete the summary. `Ack` is treated as non-terminal. fusiondesk_reconnect_teardown_ack_tests covers this contract and the envelope mapping.
ReconnectTeardownDispatcher extends that contract to the pure runtime dispatch boundary: it sends teardown requests through `NetworkRouter`, tracks them through `RequestTracker`, records `Ack` as interim state, summarizes terminal responses, and synthesizes a terminal error on local send failure. fusiondesk_reconnect_teardown_dispatch_tests covers this boundary.
ReconnectTeardownHandler adds the peer-side pure runtime handler boundary: it subscribes to the control route, recognizes FDRT requests, ignores unrelated control payloads, rejects malformed FDRT payloads, calls `IReconnectTeardownCloseTarget`, and sends terminal Response/Error. fusiondesk_reconnect_teardown_handler_tests covers this boundary.
ReconnectTeardownService combines the dispatch boundary, response subscriptions, peer handler, and RequestTracker expiry into one pure runtime execution boundary. fusiondesk_reconnect_teardown_service_tests covers start/stop, side-plan dispatch, Ack interim handling, terminal completion, timeout expiry, peer-side handler routing, inactive dispatch rejection, and ReconnectRuntimeService stop-time cancellation of pending teardown requests. fusiondesk_qt_timer_bridge_tests proves QtTimerBridge can drive this service's timeout expiry without Qt entering runtime/connection.
QtRuntimeTransportManager implements the current Qt TCP close-target adapter for ReconnectTeardownHandler and handles already-closed old transports idempotently after successful local reconnect commit. fusiondesk_qt_tcp_transport_tests covers this adapter path.
## Qt Reconnect Orchestrator Note

runtime/qt now has QtReconnectRuntimeService, QtReconnectExecutor, and QtReconnectOrchestrator for the local TCP start/execution form of the reconnect orchestration plan. QtReconnectRuntimeService starts the Qt timer/teardown owner and delegates common coordination to ReconnectRuntimeService. QtReconnectExecutor implements the pure replacement-executor boundary; QtReconnectOrchestrator starts agent replacement listeners with planned reason/keyframe metadata, reconnects client replacement channels through QtRuntimeTransportManager, leaves Session as the only rebind owner, and relies on the existing Qt transport commit path for old transport teardown. fusiondesk_qt_tcp_transport_tests covers this owner-driven path, and fusiondesk_pc_two_peer_start_display_smoke covers PC shell delayed `--reconnect-profile` routing through the same owner in client-replacement mode plus line-oriented diagnostics output from `ReconnectRuntimeServiceSnapshot::diagnostics`. QtSessionTransportConnector exposes transport snapshots so reconnect accepts cannot invalidate transport iteration during Qt event polling.
## Reconnect Rebind Note

Session reconnect now reaches the pure core network rebind boundary: a reconnect request can carry replacement `IChannel` instances, Session calls `NetworkManager::rebindChannel`, and tests verify the affected video channel returns to ready while an unaffected control channel remains ready. Session then asks ModuleHost to replay running module ingress routes for affected consumed channels. runtime/qt can prepare local Qt TCP replacement channels, drive Session reconnect in one call, close/remove the old active Qt transport after rebind, and expose the close-target adapter for peer-side FDRT teardown. PC shell delayed reconnect and diagnostics output now use the runtime reconnect owner; production remote service execution and PC UI reconnect wiring remain later gates.
## Reconnect Report Note

Session reconnect now exposes a structured latest-attempt report through `Session::lastReconnectReport` and `SessionSnapshot.lastReconnect`. The report records per-channel degrade/rebind outcomes, ingress replay outcomes, and module pause/resume reports, including failure cases, so stage-gate checks do not have to infer reconnect state only from diagnostics events. `ReconnectDiagnosticsReport` now aggregates that session report with coordinator plan/replacement/teardown and timeout state, and ReconnectRuntimeService exposes the aggregate through its snapshot for UI/runtime consumers.

## Tunnel Replacement Executor Note

runtime/tunnel now has the first pure C++ tunnel replacement executor contract. `TunnelReconnectExecutor` adapts the existing `IReconnectReplacementExecutor` interface to `ITunnelReplacementBackend`, so LAN TCP, relay, and direct P2P implementations can be selected below `ReconnectCoordinator` without changing Session or feature modules. `TunnelCandidateProfile` now selects compatible client/agent candidates by ChannelKey, mode, encryption requirement, and fallback policy, and `ITunnelTransportFactory` is the concrete socket adapter interface with local/peer candidate request context. This is still a contract-only slice: real relay sockets, NAT traversal, candidate gathering, and enterprise policy selection are later Gate P7 work.
