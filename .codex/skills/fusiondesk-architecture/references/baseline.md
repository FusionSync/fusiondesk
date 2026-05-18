# FusionDesk Compact Baseline

Project root:

```text
E:\workspace\GIT_CODE\ASTUTE\Production-HSR2\HSR-Windows-src
```

Canonical docs:

```text
docs/architecture/README.md
docs/architecture/GOAL_AUTOPILOT_PLAN.md
docs/architecture/FUSIONDESK_MINIMAL_VERSION_RUNBOOK.md
docs/architecture/FUSIONDESK_PROJECT_INIT_METHOD.md
docs/architecture/FUSIONDESK_IMPLEMENTATION_BASELINE.md
docs/architecture/FUSIONDESK_STAGE_GATES.md
docs/architecture/MODULE_AND_INTERFACE_BLUEPRINT.md
docs/architecture/RUNTIME_HOST_AND_SESSION_MANAGER_DESIGN.md
docs/architecture/NETWORK_CHANNEL_REGISTRY_AND_SCHEDULER.md
```

Core rule:

```text
FusionDesk core is pure C++.
Qt, JNI, OS APIs, transport details, and codec details stay in runtime, adapters, platform, bindings, apps, or modules as appropriate. Legacy Source code is reference-only and must not become the target runtime implementation.
```

Layering:

```text
apps -> runtime -> core
runtime -> modules
runtime -> adapters
modules -> core interfaces
adapters -> framework, transport, codec, or platform implementation
platform -> OS services
bindings -> external package surfaces
```

Current implementation roots:

```text
use include/fusiondesk/core/... and src/core/... for current core work.
legacy singular roots include/fusiondesk/module, network, policy, protocol, session
and src/module, network, policy are empty compatibility placeholders and must not
become active ownership roots.
```

First milestone:

```text
fusiondesk_core_display_mvp
```

Required proof:

```text
session creation
policy authorization
request/response envelope and correlation
protocol envelope validation
packet wire codec
capability negotiation
capability exchange envelope
negotiated channel allowlist boundary
default MVP channel specs
ChannelRegistry
PriorityScheduler
NetworkManager skeleton
channel rebind boundary
NetworkManager transport boundary
ITransportSocket
SocketGroup
SessionManager
RuntimeHost
session reconnect lifecycle
channel readiness
module attach/start
ModuleHost role/platform/dependency/channel-readiness gates
VIDEO route from agent to client
PAYLOAD_ACK route from client to agent
keyframe request
first frame render
diagnostics
```

Current G4 truth:

```text
ModuleRuntime carries NetworkRouter, optional NetworkManager egress, and ChannelRegistry.
Session creates ModuleHost with the session-owned ChannelRegistry.
ModuleHost rejects unsupported role/platform before attach.
ModuleHost rejects missing required module dependencies before start.
ModuleHost rejects missing required channel readiness before start.
ModuleHost still applies IPolicyEngine before module.start.
ModuleManifest carries ModuleVersion and peer module compatibility declarations.
ModuleCompatibilityCheck validates peer module id/version ranges without parsing module payload schema.
ModuleStartOptions.peerVersions can carry remote module version facts into module.start, and ModuleHost exposes startAllowedModules(options) for that future exchange result.
ModuleHost recalculates peer compatibility from the target module manifest and rejects incompatible peer module versions before module.start when peerVersions are provided.
ModuleHost performs all-or-nothing preflight when ModuleStartOptions.allowPartialStart is false, so FDMI version mismatch blocks the profile before any module start side effects.
display.screen, input.mouse, and input.keyboard role-specific manifests declare peer module compatibility ranges.
ModuleInventoryService exchanges FDMI module declaration inventories over the control channel, records last remote inventory, treats empty inventories as received declaration data, and converts remote manifests into ModulePeerVersion values for ModuleHost start options.
ModuleInventoryRuntimeService owns caller-side FDMI requests, response subscriptions, RequestTracker completion, timeout expiry, and snapshots.
SessionMainline can pass ModuleStartOptions into ModuleHost, so FDMI peerVersions can gate module start without SessionMainline parsing FDMI payloads.
PC shell can run FDMI after profile module mount and before profile module start with --module-inventory-service / --module-inventory-request.
Session stores remote module inventory in RemoteModuleInventorySnapshot, SessionRuntimeDiagnosticsSnapshot exposes it, and PC shell session diagnostics can print remote module rows.
ModuleComposer enforces ProductProfile module version constraints before module creation, and RuntimeHost passes profile constraints through to composition/mount reports.
Mainline module management is limited to module identity, version, role, platform, dependency range, channel binding, and policy-visible feature checks. Module payload schema and operation compatibility stay inside module-owned codecs, validators, and runtime logic.
ModuleHost activates ModuleIngressRegistry after successful start.
ModuleHost unregisters ingress before stop.
ModuleHost exposes structured ModuleSnapshot values, and SessionSnapshot carries moduleSnapshots.
ModuleHost notifies optional reconnect-aware running modules in stop order for pause and start order for resume, filtered by affected channel bindings.
ModuleHost replays running module ingress routes during reconnect, filtered by affected consumed channel bindings.
ModuleCatalog exposes display.screen.agent/client and role-filtered remote desktop suite selection.
ModuleComposer resolves ProductProfile module ids through registered factories, orders selected dependencies before dependents, and reports missing/dependency/cycle failures.
ModuleHost preserves add order for start/stop/export/snapshots so composition order is preserved.
DisplayModuleFactory resolves display.screen into display.screen.agent or display.screen.client by session role.
RuntimeHost mounts display.screen through ModuleComposer when display dependencies are supplied.
FeatureModuleFactory and FeatureModule exist under modules/feature as lifecycle-only skeletons for audio.desktop, audio.microphone, filesystem.redirect, printer.redirect, input.keyboard, input.mouse, input.touch, input.gamepad, and camera.redirect.
InputModuleFactory resolves input.mouse and input.keyboard into role-specific client/agent modules with the FDIN payload contract.
Clipboard is removed from the current FusionDesk slice and must be rebuilt later as an independently versioned data-redirection module.
RuntimeHost registers display, input, then generic feature factories in the ProductProfile composition path, so dedicated feature contracts override lifecycle skeletons when available.
SessionCreateOptions can register ProductProfile minimum channel specs into NetworkManager; readiness still comes from transport binding.
SessionMainline exists under runtime/session as the pure C++ minimum startup frame for RuntimeHost -> SessionManager -> adapter-created IChannel bind/ready -> ProductProfile multi-module mount -> ModuleHost start reports. It also has a continuation path for mounting and starting modules on an already-created session after Qt or future tunnel adapters bind concrete channels.
LinkChannelBindingReport exists under runtime/session and combines ChannelRegistry snapshots, module-required ChannelBinding values, and listening-channel hints into a startup report with registered/listening/bound/ready/degraded/blocked facts before module start.
SessionRuntimeDiagnosticsSnapshot exists under runtime/session and aggregates SessionSnapshot, LinkChannelBindingReport, mounted/running module counts, blocked-channel count, display capture status, display codec status, and session diagnostics for future UI/service readers without pulling Qt or app code into the runtime/session boundary.
The current minimal ProductProfile start is all-or-nothing; if a required channel for any mounted profile module is blocked, SessionMainline reports the blockers and does not partially start profile modules.
FUSIONDESK_MINIMAL_VERSION_RUNBOOK.md defines the current minimal build commands, canonical smoke set, manual run shape, definition of done, and explicit out-of-scope list.
Session reconnect can accept already-created replacement IChannel instances and call NetworkManager::rebindChannel after affected channels are degraded and reconnect-aware modules are paused.
SessionSnapshot carries lastReconnect, a structured latest-attempt report with per-channel degrade/rebind outcomes, ingress replay outcomes, and module pause/resume outcomes.
DisplayAgentModule and DisplayClientModule exist for the fake first-frame slice.
Display modules publish first-frame and keyframe recovery diagnostics events.
Display modules expose basic structured MVP counters/snapshots for captured, encoded, sent, dropped, captureErrors, encode failure, invalid control request, capture geometry/format full-refresh promotion, received, decoded, rendered, client dropped, frame gap, decode error, render error, keyframe recovery, keyframe request failure/timeout, pending request, reconnect rendered-frame baseline, last rendered frame id, and decoder recovery counts.
runtime/display has a pure DisplayCaptureBackendCapability selector for the documented production backend matrix; it selects among Windows DXGI/WGC/GDI, Linux X11/PipeWire/DRM, macOS ScreenCaptureKit/Quartz, Android MediaProjection, Harmony/OpenHarmony system capture placeholders, and RK Linux/Android variants. Capabilities carry available/unavailableReason plus optional DisplayTargetArchitecture and DisplayTargetSocProfile tags, so higher-priority production probes can be rejected without breaking fallback and board-specific paths can be filtered. fusiondesk_display_capture_backend_selection_tests covers the cross-platform selector contract for Windows, Linux X11/Wayland/Embedded, macOS, Android agent/client, HarmonyOS/OpenHarmony, and RK Linux/Android routes. DisplayCapturePlatformPlanRequest, DisplayCaptureRuntimeRole, DisplayCaptureCapabilitySource, and planDisplayCapturePlatform now provide the pure startup-planning contract that separates real probed factory capabilities from diagnostic-only unavailable default matrices and marks client roles render-only; fusiondesk_display_capture_platform_plan_tests covers that contract. IDisplayCaptureBackendFactory, StaticDisplayCaptureBackendFactory, DisplayCaptureBackendFactoryRegistry, createSelectedDisplayCapture, queryDisplayCaptureSourceCatalog, createSourceCatalog, unavailableDefaultDisplayCaptureBackendCapabilities, and createUnavailableDefaultDisplayCaptureBackendFactory exist, and PC shell agent --mount-display now creates display capture through WindowsDisplayCaptureBackendFactory, runs its probed capabilities through planDisplayCapturePlatform, then creates the selected backend through createSelectedDisplayCapture. DisplaySourceSelectionRequest, DisplaySourceSelectionResult, selectDisplaySource, and displaySourceMatchesSelection provide pure runtime source-picker preflight against DisplayTopologySnapshot. PC shell --print-display-capture-plan prints stable capture-plan rows for capability source, candidates, selected backend, and rejected backend reasons; --print-display-sources prints selected backend source catalog rows through the backend factory contract for monitor/window source-selection diagnostics with ok/provider/rejected/message flags, requested source id/native handle, selected-source match state, selected=1 source rows, failure rejection/message rows, source type, and native handle fields. The Windows aggregate factory registers the default-probed WindowsDxgiDesktopDuplicationCapture path, a rollout-gated WindowsGraphicsCapture monitor/window path with FUSIONDESK_ENABLE_WGC_CAPTURE diagnostics, and WindowsGdiDisplayCaptureFactory as fallback. WindowsGdiDisplaySourceCatalog and WindowsDxgiDesktopDuplicationSourceCatalog expose backend-specific monitor topology snapshots; WindowsGraphicsCaptureSourceCatalog exposes monitor plus visible-window topology; Windows GDI, DXGI, and WGC reject invalid source ids with SourceNotFound instead of silently falling back to the primary monitor. DisplayCaptureBackendFailoverRequest, selectDisplayCaptureBackendFailover, and createFailoverDisplayCapture provide the pure runtime plan/create contract for excluding a failed backend and selecting the next compatible backend; DisplayRuntimeService can execute switch-backend through that factory path, and PC agent --start-display now passes the Windows aggregate factory plus selection request into the runtime service. FUSIONDESK_ENABLE_DXGI_CAPTURE=0 disables DXGI for diagnostics or staged rollout. fusiondesk_windows_dxgi_display_capture_opt_in_tests skips by default and validates a real non-empty DXGI BGRA frame only when FUSIONDESK_VALIDATE_DXGI_CAPTURE=1 is set. fusiondesk_windows_wgc_display_capture_opt_in_tests skips by default, validates a real non-empty WGC BGRA monitor frame when FUSIONDESK_VALIDATE_WGC_CAPTURE=1 is set, and can additionally validate a visible window source through nativeSourceHandle when FUSIONDESK_VALIDATE_WGC_WINDOW_CAPTURE=1 is set. CMakePresets.json and cmake/toolchains provide first host/cross build entry points for Windows host, Linux x86_64/aarch64/loongarch64/mips64el, RK3568/RK3588, Android arm64-v8a/x86_64, and OpenHarmony/HarmonyOS arm64.
runtime/display also has a pure DisplayCodecCapability selector and IDisplayCodecBackendFactory contract for production codec rollout. DisplayCodecId covers raw_bgra, H.264, H.265, and AV1; DisplayCodecBackendKind covers raw_frame, software, FFmpeg software, Windows Media Foundation, VAAPI, V4L2 M2M, VideoToolbox, Android MediaCodec, Harmony system codec, and Rockchip MPP slots. ProductDisplayCodecPolicy now lives on ProductProfile and carries codec preference, hardware/software preference, Windows MediaFoundation H.264 production enablement, P-frame enablement, and selectionMode; PC shell --display-codec-policy windows-h264-production activates this policy without injecting product rollout env vars into child processes. The default matrix keeps raw_frame available as the current MVP fallback while future H.264/H.265/AV1 hardware/software adapters report unavailable until real adapter factories provide candidates. RawFrameDisplayCodecBackendFactory creates the current RawFrameEncoder/RawFrameDecoder through createSelectedDisplayEncoder/createSelectedDisplayDecoder, and PC agent/client startup now uses that codec backend factory path instead of direct app-level raw codec construction. DisplayCodecNegotiationRequest, DisplayCodecNegotiationResult, DisplayCodecNegotiationAttempt, and negotiateDisplayCodec provide the pure two-peer codec intersection contract for matching an agent encoder selection with a client decoder selection. PeerProfileExtension gives FDPP a generic opaque request/response extension carrier, and runtime/display owns display.codec.v1 through DisplayCodecPeerProfilePayload plus encode/decode helpers for encoder/decoder selection requests and codec candidate inventories. PC shell --display-codec-negotiate-fdpp is the live startup bridge: client sends decoder inventory in FDPP, agent negotiates against local encoder inventory, response returns selected encoder/decoder inventory, the agent waits before display dependency creation, and both roles pin the negotiated adapter. PcProductSessionController now has the typed product helper for the same display.codec.v1 flow: build encoder/decoder inventory requests from ProductDisplayCodecPolicy plus codec-factory candidates, append client decoder inventory, handle agent negotiation against a typed encoder request, return pinned encoder/decoder inventory, pin the current role's post-negotiation request, read/validate client completion from QtPeerProfileRuntimeServiceSnapshot, create selected encoder/decoder objects with DisplayCodecRuntimeInfo, assemble role-specific DisplayMvpDependencies, and mount those dependencies through the product controller. PC shell --display-codec-negotiate-local remains the Windows-Windows diagnostic bridge for the same helper: it runs local encoder/decoder capability negotiation before codec object creation, prints display.codec.negotiation rows with --print-display-codec-plan, pins the current role to the negotiated adapter, and fails closed on negotiation failure. Display modules now also own the FDSF compressed-frame payload envelope with codec id, Annex B/AVCC bitstream format, coded/visible dimensions, decoded pixel format, frame id, keyframe flag, timestamp, sequence-header/config bytes, and compressed bitstream bytes, so future H.264/H.265/AV1 packets remain opaque to the mainline. WindowsMediaFoundationDisplayCodecBackendFactory, probeWindowsMediaFoundationH264Codec, and preflightWindowsMediaFoundationH264Adapter are the first Windows H.264 codec capability/probe/preflight skeleton; FUSIONDESK_SELECT_MF_H264 is still a validation-only selector override, while ProductDisplayCodecPolicy is the production rollout path that promotes MediaFoundation H.264 to the default Windows product candidate while preserving raw fallback when policy is off or the host probe fails. DisplayCodecRuntimeInfo now flows selected encoder/decoder adapter, codec, backend, selectionMode, fallbackReason, H.264 delta/P-frame state, payload/error counters, and delayed decoder counters into DisplayAgentSnapshot, DisplayClientSnapshot, SessionRuntimeDiagnosticsSnapshot, and PC shell session.diagnostics.display_codec rows for future product UI/service readers. convertBgraToNv12, convertNv12ToBgra, and Nv12Frame provide pure runtime CPU conversion helpers for validated Bgra32 CapturedFrame input to NV12 Y/UV planes and decoder NV12 output back to BGRA. preflightWindowsMediaFoundationH264Encode is the manual real encode/decode gate behind FUSIONDESK_VALIDATE_MF_H264_ENCODE; it enumerates H.264 MFT candidates, selects a CPU-buffer NV12/RGB32 input subtype, captures real bitstream bytes, validates FDSF wrapping, direct-decodes the factory output, and verifies BGRA decoded pixels without making the default startup path selectable. WindowsMediaFoundationH264Encoder and WindowsMediaFoundationH264Decoder are the first direct-create MediaFoundation codec objects behind the factory; the encoder converts Bgra32 frames to NV12 or packed RGB32, drains all output after each input frame, uses monotonic sample timestamps, recreates the encoder for requested keyframes when the host MFT does not support AVEncVideoForceKeyFrame, and keeps state for opt-in/product P-frame streams; the decoder accepts FDSF-wrapped H.264, selects decoder-published NV12/RGB32/ARGB32 output types, enables low-latency mode when supported, preserves delayed-output metadata, converts NV12 output back to BGRA, and recreates decoder state for keyframe sequence headers. PC shell --print-display-codec-plan prints selected codec, fallback/hardware/zero-copy flags, candidates, and rejection messages. FUSIONDESK_VALIDATE_PC_H264_DISPLAY turns the two-peer PC smoke into an opt-in Windows-Windows H.264 first-frame plus reconnect fresh-frame gate, and FUSIONDESK_VALIDATE_PC_H264_PRODUCTION validates the ProductProfile policy rollout path without exact CLI pinning. fusiondesk_display_codec_selection_tests covers current raw fallback, injected hardware H.264 preference, hardware/software policy filters, zero-copy decode preference, codec parsing, and Rockchip architecture/SoC filtering; fusiondesk_display_codec_negotiation_tests covers H.264 selection when both peers support it, raw fallback when the decoder lacks H.264, exact-backend no-silent-fallback behavior, direction validation, and no-common-preference rejection; fusiondesk_display_codec_peer_profile_tests covers display.codec.v1 payload roundtrip, malformed rejection, and decoded payload negotiation; fusiondesk_peer_profile_service_tests covers opaque FDPP extension request/response roundtrip; fusiondesk_pc_product_session_controller_tests covers product-controller display.codec.v1 inventory building, H.264 negotiation, raw fallback, role request pinning, raw encoder/decoder creation, runtimeInfo publication, display dependency build/mount, and missing-extension failure with pure typed inventories; fusiondesk_pc_peer_profile_start_display_smoke covers live FDPP codec negotiation for the current raw fallback path over real PC agent/client processes; fusiondesk_display_codec_backend_factory_tests covers raw encoder/decoder creation, unavailable default factory diagnostics, registry forwarding, exact unavailable adapter rejection, and selected-H.264 factory creation failure without silent raw fallback; fusiondesk_display_encoded_video_payload_tests covers FDSF metadata/config/bitstream roundtrip, malformed rejection, and raw decoder rejection of compressed envelopes; fusiondesk_windows_media_foundation_display_codec_tests covers disabled rollout diagnostics, registry raw fallback, opt-in probe stability, odd-size preflight rejection before MediaFoundation startup, BGRA-to-NV12 preflight diagnostics, opt-in adapter preflight stability, direct encoder/decoder creation, validation-only selector enablement, policy production selector enablement with P-slice output, repeated keyframe encode/decode recovery, decoder delayed-output metadata mapping, low-latency P-frame output handling, default-stable encode preflight, manual encode/decode gate, and manual P-frame gate; fusiondesk_pc_agent_start_display_requires_ready_channel_smoke and fusiondesk_pc_two_peer_start_display_smoke cover PC session diagnostics display_codec rows; fusiondesk_display_color_conversion_tests covers deterministic BGRA-to-NV12 and NV12-to-BGRA output plus invalid input rejection.
DisplayProductDiagnosticsSnapshot, DisplayProductHealthLevel, buildDisplayProductDiagnostics, DisplayProductHealthPresentation, and buildDisplayProductHealthPresentation now fold SessionRuntimeDiagnosticsSnapshot session/link/module/capture/codec state into a product-facing display health summary plus stable status/action/capture/codec state presentation codes. PC shell --print-session-diagnostics emits the same data as session.diagnostics.display_health rows with ok/warning/degraded/blocked health, usable flag, capture status/recovery action, codec rollout/fallback/P-frame state, delayed decoder counters, status, action, captureState, codecState, fallback warning, delay warning, and capture recovery warning fields for transition tooling.
parseDisplayCaptureSourceType, parseDisplayPlatformFamily, parseDisplayTargetArchitecture, and parseDisplayTargetSocProfile normalize CLI/CI strings for monitor/desktop, window, virtual-display, media-projection, windows, linux-x11, wayland, linux-embedded, macos/darwin, android-client, android-agent, harmonyos, openharmony/ohos, rk3568/rk3588 Linux/Android targets, x86, x86_64/amd64, arm32/armv7, arm64/aarch64, loongarch64, mips64el, generic, rk3568, and rk3588 before backend filtering. PC shell forwards --display-source-type, --display-target-platform, --display-target-arch, and --display-target-soc into the backend selection request, forwards sourceType/nativeSourceHandle into DisplayCaptureOpenOptions, and its blocked-start smoke asserts current Windows window capture does not silently fall back to monitor-only DXGI/GDI while WGC rollout is not enabled.
DisplayCaptureBackendSelectionRequest::requestedAdapterId and PC shell --display-capture-backend provide explicit auto/gdi/dxgi/wgc/exact-adapter rollout control while preserving selector priority as the default.
Display modules implement optional reconnect-aware hooks; display client resume can request a fresh keyframe through PAYLOAD_ACK Request/Response, and display agent resume sends a fresh keyframe plus a follow-up delta when requested so one-frame-delayed codecs have enough post-reconnect input to render.
Display frame types carry width, height, stride, pixel format, frame id, keyframe flag, and timestamp metadata.
DisplayAgentModule records the last successfully sent frame shape and promotes source, geometry, stride, or pixel-format changes on a later delta capture into a keyframe/full-refresh frame; fusiondesk_display_mvp_tests covers the keyframe flag, counter, and diagnostic event.
Display capture adapters expose stable backend identity through IDisplayCapture::backendId, structured state through IDisplayCapture::lastStatus, and a stable error count through IDisplayCapture::captureErrors; Windows GDI, DXGI, and WGC populate backend ids plus success/failure states for source, access/system-call, timeout, device-loss, invalid-frame, unsupported, and not-open cases. Windows DXGI native error classification is tested: access-lost maps to SourceHotplug/recoverable, device removed/reset maps to DeviceLost/recoverable, access denied maps to AccessDenied/non-recoverable, and session disconnected maps to SessionModeUnsupported/recoverable. displayCaptureStatusCodeName provides stable status names, and runtime/display decideDisplayCaptureRecovery maps capture statuses into retry, reopen, recreate, switch-backend, wait-for-permission, or stop actions. DisplayCaptureRecoveryPolicy/State/Plan add same-backend recovery limits, failed-recovery cooldown, and repeated recoverable-failure promotion to switch-backend. DisplayRuntimeService executes recoverable reopen/recreate decisions by calling DisplayAgentModule::reopenCapture, executes switch-backend by calling createFailoverDisplayCapture plus DisplayAgentModule::replaceCapture when a backend factory is configured, and exposes cooldown-block/promotion/consecutive-failure counters; both success paths can send a fresh keyframe. It also exposes basic capture health metrics for pump count, frame attempts, frame timestamps, last observed frame age, effective FPS x1000, consecutive frame misses, service-observed captured pixel bytes, encoded payload bytes, sent payload bytes, last frame byte sizes, and effective bitrate Kbps. In client mode, DisplayRuntimeService can send a FirstFrameTimeout keyframe Request over small_data when no frame has rendered and no keyframe request is pending, and can retry a Reconnect keyframe Request when a reconnect resume baseline does not advance after channel rebind. Windows DXGI Desktop Duplication reuses the latest successful frame for keyframe/full-refresh requests when AcquireNextFrame times out on a static desktop. DisplayCaptureOpenOptions has sourceType/nativeSourceHandle for monitor/window capture and includeCursor enabled by default, and WindowsCursorOverlay maps the current Win32 cursor/hotspot into selected source/fit/stretch BGRA frames for both GDI and DXGI; PC shell --display-no-cursor disables it for diagnostics and prints includeCursor in capture plan/session diagnostics while future cursor sideband remains separate. DisplayAgentSnapshot, DisplayClientSnapshot, DisplayRuntimeServiceSnapshot, SessionRuntimeDiagnosticsSnapshot, and PC shell diagnostics propagate the selected backend, latest capture status, captureErrors, includeCursor, captured/sent frame counts, byte counters, and recovery action for future UI consumers. runtime/display resolveDisplayCaptureOutputSize is the shared source/stretch/fit geometry rule used by Windows GDI, DXGI, and WGC so non-GDI paths honor the same raw-frame target guard as GDI.
RawFrameEncoder and RawFrameDecoder provide the first pure C++ software-frame payload schema.
FDSC provides the first display-owned control payload schema for keyframe request and scheduled response.
Display modules stamp SessionContext sessionId and traceId on outbound packets so real PacketCodec validation accepts VIDEO Event traffic.
Session-mounted display modules send through NetworkManager enqueue/flush before NetworkRouter send; router-only unit tests remain supported.
Display client keyframe requests are tracked through RequestTracker and can expire into timeout counters.
Display agent rejects malformed display control requests with terminal Error responses.
Display agent does not send empty encoded VIDEO payloads and rolls capture state back when startup first-frame send fails.
Display agent drops congested delta frames and preserves keyframe recovery while congested.
WindowsGdiDisplayCapture exists under platform/windows/display as the first PC capture adapter seam, with GDI, DXGI, and WGC source catalog diagnostics for monitor/window selection.
QtImageDisplayRenderer exists under adapters/qt/display as the first Qt software render adapter seam. QtImageDisplayWindow exists as the first QWidget-backed visible PC display surface, includes a small status line, and stays in the Qt adapter/app boundary, not in display modules.
WindowsInputInjector exists under platform/windows/input as the first dry-run input injection seam.
QtInputCapture exists under adapters/qt/input as the first Qt input event conversion seam.
PC client and agent shells can explicitly mount role-specific display adapter dependencies with --mount-display.
PC agent --start-display now starts the DisplayRuntimeService frame pump, and runtime/display withDefaultRawFrameCaptureTarget applies the current raw-frame MVP bounded 1280x720 fit target unless --display-target-width, --display-target-height, or --display-scale-mode source overrides it. PC client --start-display starts DisplayRuntimeService in client monitoring mode; --display-first-frame-timeout-ms controls first-frame and reconnect fresh-frame recovery timeout, --print-display-runtime-diagnostics emits display runtime snapshot rows for smoke and future UI handoff including pump/frame attempt counts, frame timestamps, last observed frame age, effective FPS x1000, consecutive misses, service-observed captured/encoded/sent byte counters, last frame byte sizes, and effective bitrate Kbps, and --show-display-window opens the first QWidget display surface bound to the Qt image renderer with a status line updated from DisplayProductHealthPresentation.
PC client and agent shells can mount input feature profile modules with --mount-input or --profile-module and inject first feature adapter dependencies through RuntimeHost.
runtime/feature has FeatureRuntimeService, a pure C++ owner that polls IInputCapture and calls InputClientModule send APIs.
PC client and agent shells can start that owner with --pump-profile-modules; QTimer remains in the PC shell boundary, not in runtime/feature or modules.
runtime/feature also has FeatureRuntimePolicy as a pure foundation for operation-level allow/deny/audit and service-owned input capture lifecycle with idempotent start and destructor cleanup.
defaultLargeDataChannelSpec exists for redirection startup profiles, and pc_profile_plan accepts large_data by name.
QtTcpTransportSocket exists under adapters/qt and can roundtrip PacketCodec PacketEnvelope bytes over loopback Qt TCP.
QtTcpTransportSocket detaches adopted QTcpSocket instances from their Qt parent before taking transport ownership.
QtPacketChannel exists under adapters/qt and can route PacketEnvelope through NetworkManager/ChannelRegistry/NetworkRouter over loopback Qt TCP.
QtChannelBinder exists under adapters/qt and can register SocketGroup, bind ChannelRegistry, and mark ready for QtPacketChannel.
QtSessionTransportConnector exists under runtime/qt and can create/adopt Qt TCP transports from profile entries, drive QtChannelBinder, and prepare reconnect replacement channels without marking them ready.
QtSessionTransportConnector exposes transport lists as snapshots, not live internal vector references, because polling a Qt transport can process Qt events that accept reconnect sockets and mutate the connector's active transport storage.
QtRuntimeTransportManager exists under runtime/qt and owns QtSessionTransportConnector instances plus QTcpServer listeners per RuntimeHost sessionId. It can prepare Qt TCP reconnect replacement channels from connect profiles or accepted sockets, drive Session::reconnect in one runtime-facing call, close/remove the previous active Qt transport after successful one-call reconnect, and implement IReconnectTeardownCloseTarget for peer-side FDRT teardown; Session::reconnect performs the actual rebind.
runtime/connection has a pure C++ peer connection plan resolver that maps requested ChannelKey entries to known ChannelSpec values and rejects missing, duplicate, unknown, or endpointless plans before transport-specific validation.
runtime/connection has PeerProfileService, a pure C++ control-channel PacketEnvelope Request/Response service that carries FDPP peer profile exchange payloads without JSON, Qt, or sockets, supports generic opaque PeerProfileExtension request/response payloads for module-owned profile data, and ignores unrelated control payloads before decoding.
runtime/connection has PeerProfileRuntimeService, a pure C++ caller-side owner that sends FDPP profile requests, subscribes for response kinds, tracks completion with RequestTracker, expires timeouts, and snapshots state without Qt, JSON, sockets, or Session ownership.
runtime/qt has QtPeerProfileRuntimeService, which applies FDPP exchange results through QtRuntimeTransportManager for agent listen profiles and client connect profiles without moving Qt into runtime/connection or packet construction into apps.
runtime/connection has a pure C++ reconnect orchestration plan resolver that maps service-selected degraded ChannelKey values to client replacement TCP channels, agent replacement TCP listeners, reason/keyframe intent, and teardown-after-success intent without Qt, sockets, or Session calls.
ReconnectCoordinator exists under runtime/connection as the pure C++ service-level frame above the reconnect orchestration plan. It sequences plan resolution, replacement executor calls, client reconnect handoff, and teardown executor dispatch without owning Qt, sockets, or Session internals.
ReconnectRuntimeService exists under runtime/connection as the common owner for ReconnectCoordinator, RequestTracker, ReconnectTeardownService, and ReconnectTeardownServiceExecutor; stop cancels pending teardown requests with terminal Cancelled responses.
ReconnectRuntimeServiceSnapshot carries ReconnectDiagnosticsReport, a pure C++ aggregate of coordinator plan/replacement stages, optional Session rebind report, teardown pending state, expired request count, and timeout state for runtime/UI readers.
runtime/tunnel has TunnelReconnectExecutor and ITunnelReplacementBackend as the first pure relay/direct/P2P replacement executor contract below ReconnectCoordinator. It maps side plans into tunnel replacement requests without creating sockets or changing Session ownership.
runtime/tunnel has TunnelCandidateProfile negotiation for compatible client/agent candidate selection and ITunnelTransportFactory as the concrete LAN/relay/direct adapter boundary. TunnelTransportFactoryRequest can carry both the local candidate and selected peer candidate.
QtReconnectOrchestrator exists under runtime/qt and consumes the local TCP form of that reconnect orchestration plan. It starts reconnect-aware agent listeners with planned reason/keyframe metadata and drives client replacement reconnects through QtRuntimeTransportManager while leaving logical rebind in Session.
QtReconnectExecutor exists under runtime/qt and adapts the pure IReconnectReplacementExecutor boundary to QtReconnectOrchestrator, so ReconnectCoordinator can drive Qt local TCP without depending on Qt types.
QtReconnectRuntimeService exists under runtime/qt and composes QtReconnectExecutor, ReconnectRuntimeService, and QtTimerBridge. PC shell reconnect CLI wiring calls this owner instead of direct QtRuntimeTransportManager reconnect helpers.
runtime/qt can load Qt TCP transport profile JSON files, validate tcpChannels/tcpListenChannels against known ChannelSpec entries, and apply connect/listen profiles through QtRuntimeTransportManager.
runtime/qt can generate matching client-connect/agent-listen profile pairs and serialize/save them to the canonical JSON profile shape.
QtTcpPeerProfileCoordinator exists under runtime/qt and can generate, validate, and save local multi-endpoint TCP client-connect/agent-listen profile files from known ChannelSpec values and requested channel plans through the common peer connection resolver; it does not start sockets, negotiate P2P, or mark channels ready.
fusiondesk_pc_profile_plan exists under apps/pc and can generate paired client/agent profile JSON from named MVP channel plans for startup scripts.
PC shell startup can bind no-sessionId Qt TCP transport profiles to the RuntimeHost session it just created.
PC shell startup can invoke QtPeerProfileRuntimeService with --peer-profile-service on the agent and --peer-profile-channel on the client, so JSON can bootstrap only the control channel and FDPP can negotiate later feature channels.
PC shell --print-reconnect-diagnostics exposes ReconnectRuntimeServiceSnapshot::diagnostics at reconnect completion/failure and exit lifecycle points.
PC shell --print-session-diagnostics exposes SessionRuntimeDiagnosticsSnapshot as stable stdout records at startup, transport/listen profile application, profile mount/start, blocked-start, reconnect-complete, and exit lifecycle points.
PC shell creates role sessions and mounts/starts ProductProfile modules through SessionMainline rather than direct SessionManager/ModuleHost calls.
PC shell --start-display waits for required module channels with --wait-channels-ms using LinkChannelBindingReport before SessionMainline module start.
PC shell --run-ms provides bounded event-loop execution for startup smoke tests.
PC shell --require-display-frame checks display sent/rendered counters after bounded startup runs; first-frame and reconnect fresh-frame requests are owned by DisplayRuntimeService instead of the shell injecting duplicate keyframe requests.
fusiondesk_qt_tcp_transport_tests proves JSON-loaded QtRuntimeTransportManager profile data can bind to the current runtime session, listen/adopt inbound agent channels, mark multiple coordinator-generated endpoint channels ready, prepare reconnect replacement channels, drive one-call Session reconnect, close/remove old active Qt transports after rebind, and carry RuntimeHost-mounted display first-frame/keyframe recovery over Qt TCP through the one-call apply path.
fusiondesk_qt_tcp_transport_tests also proves QtReconnectRuntimeService can start the Qt timer/teardown owner frame and call ReconnectRuntimeService -> ReconnectCoordinator -> QtReconnectExecutor to start the local TCP form of a reconnect orchestration plan through QtReconnectOrchestrator, preserve planned reason/keyframe intent on both sessions, close old transports after rebind, route PayloadAck over the replacement channel, and exercise QtRuntimeTransportManager as the ReconnectTeardownHandler close-target adapter.
fusiondesk_peer_connection_plan_tests covers pure C++ peer connection plan resolution without Qt.
fusiondesk_peer_profile_service_tests covers PeerProfileService FDPP request/response envelope roundtrip, service-side terminal Response generation, and unrelated control payload ignore.
fusiondesk_peer_profile_runtime_service_tests covers PeerProfileRuntimeService FDPP request dispatch, loopback response completion through RequestTracker, timeout expiry, and snapshots.
fusiondesk_qt_peer_profile_runtime_service_tests covers QtPeerProfileRuntimeService exchanging FDPP over a bootstrap control channel, auto-starting agent main_screen listening, applying the client main_screen connect profile, and leaving both registries ready.
fusiondesk_qt_peer_profile_coordinator_tests covers local connection-plan validation, ChannelSpec-backed requested channel resolution, multi-endpoint client/agent profile pairing, canonical JSON save/load roundtrip, no-sessionId shell profile shape, and failure reporting for missing/duplicate/unknown channels and duplicate endpoints without starting sockets.
fusiondesk_pc_profile_plan_smoke covers the PC profile plan CLI generating multi-channel profile JSON and rejecting unknown channel names.
QtTimerBridge exists under runtime/qt and can drive RequestTracker timeout expiry through QTimer without Qt entering core.
QtTimerBridge exists under runtime/qt and can drive ReconnectTeardownService timeout expiry through QTimer without Qt entering runtime/connection.
fusiondesk_qt_timer_bridge_tests covers QtTimerBridge start/stop, deferred handler install, RequestTracker timeout expiry, and ReconnectTeardownService timeout expiry.
QtEventLoopBridge exists under runtime/qt and can post callbacks, process events, and run until a predicate through QCoreApplication without Qt entering core.
fusiondesk_qt_event_loop_bridge_tests covers posted callback execution, immediate predicate handling, and empty task rejection.
fusiondesk_display_frame_codec_tests covers raw frame schema roundtrip, FDSC display control schema roundtrip, and invalid payload rejection.
Display PAYLOAD_ACK Request/Response traffic now uses small_data while VIDEO stays on main_screen; display manifests require both channels, session-mounted display sends use NetworkManager::flushPacket for the exact enqueued packet, and display_mvp, qt_tcp_transport, PC two-peer, PC FDPP, PC profile-plan, network_manager, and FDMI inventory tests cover the split.
fusiondesk_feature_contract_tests covers input.mouse/input.keyboard Event routing through ModuleHost/NetworkRouter/IInputInjector.
fusiondesk_module_protocol_tests covers real role-specific manifest peer declarations, test.protocol client/agent module version declarations, peer compatibility range checks, ModuleHost startup and rejection with peerVersions, NetworkRouter ingress, ProtocolValidator request/response checks, and module-owned control Exchange success/unsupported/protocol-error payload handling.
fusiondesk_module_inventory_exchange_tests covers FDMI manifest subset roundtrip, control-channel ModuleInventoryService request/response, empty inventory receipt, caller-side ModuleInventoryRuntimeService terminal Response/Error completion, empty successful response completion, timeout expiry, malformed request-envelope rejection, non-Ok response rejection, remote inventory to ModuleHost peerVersions, and incompatible remote module version rejection.
fusiondesk_module_factory_tests and fusiondesk_runtime_host_tests cover ProductProfile module version constraints through ModuleComposer and RuntimeHost mount reports, including alias/concrete duplicate request attempts that must not bypass alias constraints.
fusiondesk_display_capture_backend_factory_tests covers StaticDisplayCaptureBackendFactory, DisplayCaptureBackendFactoryRegistry, unavailable capability slots, aggregate selection, rejected-message diagnostics, and full-platform unavailable default backend factory coverage.
fusiondesk_display_capture_platform_plan_tests covers role-aware client render-only planning, probed Windows factory capability selection, explicit requested-backend planning, missing RK Linux adapter diagnostics through unavailable default matrices, future macOS default-matrix selection, and platform-default accepted memory types.
fusiondesk_display_capture_backend_failover_tests covers failed backend exclusion, DXGI to WGC/GDI fallback planning, explicit requested-backend blocking, emergency override, and selected failover backend creation.
fusiondesk_windows_gdi_display_capture_tests covers Windows GDI display capture adapter construction/open/close, the default-probed DXGI direct-create path, rollout-gated WGC monitor/window capture factory creation, FUSIONDESK_ENABLE_WGC_CAPTURE disabled/not-enabled/enabled-probed diagnostics, aggregate Windows factory fallback to GDI when higher-priority backends are disabled, forced GDI selection, forced DXGI/WGC rejection with rollout reasons, WGC window backend selection when rollout is enabled, and explicit GDI/DXGI rejection of window sources. fusiondesk_windows_dxgi_display_capture_opt_in_tests is the manual real-frame DXGI validation gate and captured a 288x180 frame from a 320x180 fit request in the current dev session when FUSIONDESK_VALIDATE_DXGI_CAPTURE=1 was set. fusiondesk_windows_wgc_display_capture_opt_in_tests is the manual real-frame WGC validation gate and captured a 288x180 monitor frame from a 320x180 fit request in the current dev session when FUSIONDESK_VALIDATE_WGC_CAPTURE=1 was set; adding FUSIONDESK_VALIDATE_WGC_WINDOW_CAPTURE=1 captured a 180x180 visible-window frame through nativeSourceHandle in the same session.
fusiondesk_qt_image_display_renderer_tests covers decoded software frame rendering into QImage callbacks.
fusiondesk_windows_feature_adapter_tests covers the Windows input dry-run adapter seam.
fusiondesk_qt_input_capture_tests covers Qt event conversion into pure input events.
fusiondesk_pc_feature_adapter_startup_smoke covers PC shell feature adapter profile mounting and required-channel start gating.
fusiondesk_session_mainline_tests covers the blocked LinkChannelBindingReport path before module start.
fusiondesk_session_mainline_tests covers SessionRuntimeDiagnosticsSnapshot for both ready and blocked link/channel states.
fusiondesk_pc_agent_start_display_requires_ready_channel_smoke and fusiondesk_pc_two_peer_start_display_smoke cover PC shell session diagnostics output for blocked and ready startup paths.
fusiondesk_pc_agent_listen_profile_smoke covers PC agent --listen-profile with a no-sessionId profile bound to the current shell session.
fusiondesk_pc_agent_start_display_requires_ready_channel_smoke covers PC agent --start-display rejecting display start before required channels are ready.
fusiondesk_pc_two_peer_start_display_smoke covers real PC agent/client executables using fusiondesk_pc_profile_plan generated no-sessionId control/small_data/main_screen listen/connect profiles, gated --start-display, bounded --run-ms execution, client rendered-frame verification, delayed --reconnect-profile routed through QtReconnectRuntimeService, post-reconnect rendered-frame progress, display runtime diagnostics, and --print-reconnect-diagnostics output from the runtime snapshot.
fusiondesk_pc_peer_profile_start_display_smoke covers real PC agent/client executables using JSON only for control-channel bootstrap, then FDPP through QtPeerProfileRuntimeService to negotiate small_data/main_screen and FDMI through ModuleInventoryRuntimeService before display startup, including module inventory completion diagnostics and remote module session diagnostics.
fusiondesk_pc_client_display_mount_smoke and fusiondesk_pc_agent_display_mount_smoke cover PC shell display adapter dependency mounting.
PcProductSessionController now exists in apps/pc/common as the first product-level startup seam above the CLI shell. It owns RuntimeHost session creation through SessionMainline, QtRuntimeTransportManager lifetime, QtPeerProfileRuntimeService startup/request/apply, typed display.codec.v1 inventory/request/response/completion/codec-create/dependency-mount helpers, QtReconnectRuntimeService startup, DisplayRuntimeService startup/pump/stop, display pump timer lifecycle, optional QWidget display window lifecycle, profile mount/start continuations, listening-channel-aware SessionRuntimeDiagnosticsSnapshot, peer-profile snapshots, display runtime snapshots, and direct DisplayProductHealthPresentation reads for future PC UI/service callers. fusiondesk_pc_product_session_controller_tests covers agent startup, peer-profile owner activation and request entry with assigned message id, product-controller display.codec.v1 inventory building, H.264 negotiation, raw fallback, role request pinning, raw encoder/decoder creation, runtimeInfo publication, display dependency build/mount, missing-extension failure, reconnect owner activation, display module mount with fake capture/raw encoder dependencies, optional QWidget window creation/status update/close through QtImageDisplayRenderer, display runtime start/stop through the controller, channel-blocked health after mount, and required-channel module-start blocking.
fusiondesk_pc_client and fusiondesk_pc_agent are thin QCoreApplication shells that initialize RuntimeHost, start role-specific sessions, optionally apply --transport-profile, --listen-profile, or FDPP peer-profile startup negotiation to the current shell session, gate --start-display on required channels, support bounded --run-ms execution, support --require-display-frame, and support --smoke exit.
fusiondesk_pc_client_smoke and fusiondesk_pc_agent_smoke cover thin PC shell startup.
fusiondesk_source_purity_scan is a CTest guard for forbidden Qt, Source, and ThirdParty tokens under current FusionDesk core and modules.
Production input behavior beyond the current adapter startup seam and pure runtime feature policy foundation, clipboard rebuild as a later data-redirection module, production behavior inside remaining non-display feature modules, production PC UI invocation of QtPeerProfileRuntimeService and display.codec.v1 helper flow through the new product controller, reconnect lifecycle peer-profile invocation, production peer coordination service execution, production service execution over the reconnect plan, production PC UI reconnect wiring and diagnostics rendering, hardware encode/decode/render backends, non-Windows display platform adapters, Android JNI/AAR, tunnel candidate gathering, relay/direct P2P socket implementations, NAT traversal, enterprise tunnel selection policy, and actual product UI screens over the current DisplayProductDiagnosticsSnapshot/DisplayProductHealthPresentation surfaces are still pending.
```

G5 current route:

```text
fake capture -> fake encoder -> VIDEO Event -> fake decoder -> fake renderer -> first frame
FDSC PAYLOAD_ACK Request -> Response/Error -> keyframe VIDEO Event
congested video pressure -> delta drop, keyframe still sent
client receive path -> stale/duplicate frame drop before decode, delta frame-gap keyframe recovery request, decode-failure recovery request
session-mounted paths go through NetworkManager enqueue/flush before NetworkRouter, while focused unit paths can still use router-only runtime
fusiondesk_display_mvp_tests covers this route, startup first-frame rollback, malformed control rejection, stale-frame drop, frame-gap recovery request, decode-failure recovery request, timeout accounting, and the display counters.
```

Production capture target:

```text
Windows GDI, default-probed DXGI Desktop Duplication, and rollout-gated WGC monitor capture are implemented today.
The canonical production direction is backend selection behind platform/display adapters. The pure selector, architecture/SoC tags, reusable factory registry/static capability helpers, backend-factory source catalog creation and query diagnostics, full-platform unavailable default factory helpers, pure backend failover planner, cross-target CMake preset/toolchain entry points, Windows aggregate factory, default-probed DXGI path with disable override, manual DXGI real-frame validation gate, rollout-gated WGC monitor/window path with manual validation gates, Windows source catalog diagnostics, and GDI fallback path exist:
Windows DXGI Desktop Duplication or Windows Graphics Capture with GDI fallback;
Linux X11/XDamage/XShm, Wayland PipeWire portal, or DRM/KMS/GBM for embedded/headless;
macOS ScreenCaptureKit;
Android client render first and future agent capture through MediaProjection;
HarmonyOS/OpenHarmony only through isolated device SDK adapters;
RK3568/RK3588 through Linux/Android capture paths with vendor codec acceleration kept in codec adapters.
Extended targets include Windows arm64, Linux aarch64, loongarch64, mips64el, Android ABIs, OpenHarmony/HarmonyOS arm64, and RK3568/RK3588 aarch64 builds.
```

Doc landing checklist:

```text
Goal status: docs/architecture/GOAL_AUTOPILOT_PLAN.md
Real state: docs/architecture/FUSIONDESK_IMPLEMENTATION_BASELINE.md
Gate evidence: docs/architecture/FUSIONDESK_STAGE_GATES.md
Module contracts: docs/architecture/MODULE_AND_INTERFACE_BLUEPRINT.md
Session/runtime: docs/architecture/SESSION_MODEL.md and RUNTIME_HOST_AND_SESSION_MANAGER_DESIGN.md
Display route: docs/architecture/DISPLAY_MODULE_DESIGN.md
Network route: docs/architecture/NETWORK_MODEL.md and NETWORK_CHANNEL_REGISTRY_AND_SCHEDULER.md
```
Peer profile exchange note:

runtime/connection now also has a pure C++ peer profile exchange pair builder that turns a resolved plan into client/agent profile pair data without Qt or sockets.
fusiondesk_peer_profile_exchange_tests covers that builder on success and invalid-plan rejection paths.
PeerProfileService exposes that exchange over the control channel using FDPP PacketEnvelope Request/Response traffic. JSON profile files remain local runtime/qt startup and smoke tooling, not the production exchange payload.
PeerProfileRuntimeService owns caller-side FDPP exchange execution through NetworkRouter response subscriptions and RequestTracker; QtPeerProfileRuntimeService is the Qt bridge PC/Qt invokes for startup peer-profile negotiation instead of hand-building profile packets or relying only on local JSON files. PcProductSessionController now wraps that owner and the display.codec.v1 helper flow for product callers. Production PC UI and reconnect lifecycle paths still need to call the same owner automatically.
Peer coordination note:

runtime/connection now also has a pure C++ peer coordination planner that combines peer profile exchange with reconnect hints for known channel plans.
fusiondesk_peer_coordination_tests covers the core planner.
Service reconnect plan note:

runtime/connection now also has a pure C++ reconnect orchestration planner for service-backed reconnect. It validates degraded ChannelKey values against known specs and replacement profile channels, emits client/agent replacement intent plus teardown-after-success intent, and does not create sockets or call Session.
fusiondesk_reconnect_orchestration_plan_tests covers success, default reason, and empty/duplicate/unknown/unplanned degraded-channel rejection.
fusiondesk_reconnect_coordinator_tests covers the pure high-level ReconnectCoordinator frame with one broad smoke test; fusiondesk_qt_tcp_transport_tests covers the QtReconnectRuntimeService owner path and QtReconnectExecutor adapter path.
fusiondesk_reconnect_diagnostics_report_tests covers the combined reconnect diagnostics builder and ReconnectRuntimeService pending/expired teardown snapshot path.
fusiondesk_tunnel_reconnect_executor_tests covers TunnelReconnectExecutor plugging a fake relay/direct/P2P backend into ReconnectCoordinator, plus tunnel candidate profile selection and transport factory request construction with local/peer candidates.
Reconnect teardown command note:

runtime/connection now also has a pure C++ reconnect teardown command/response/envelope/dispatch/handler/service contract. It turns post-rebind teardown target channels into correlated commands with messageId, correlationId, and timeoutMs, maps them to control-channel PacketEnvelope requests with the target channel in an FDRT payload, dispatches them through NetworkRouter, subscribes for interim/terminal responses, tracks them through RequestTracker, handles peer-side FDRT requests through IReconnectTeardownCloseTarget, expires timed-out requests through RequestTracker, and summarizes terminal Response/Error results while treating Ack as interim. fusiondesk_reconnect_teardown_ack_tests covers command generation, side-plan input, request/response envelope mapping, non-control route rejection, complete/incomplete/mismatched/failed responses, duplicate target channels, and non-terminal Ack rejection. fusiondesk_reconnect_teardown_dispatch_tests covers message-id assignment, send tracking, Ack-then-terminal completion, and local send-failure synthesis. fusiondesk_reconnect_teardown_handler_tests covers peer-side handler success/failure, malformed FDRT rejection, unrelated payload ignore, stop/unsubscribe, and route normalization. fusiondesk_reconnect_teardown_service_tests covers service start, response subscription, Ack interim handling, terminal completion, timeout expiry, peer-side handler routing, inactive dispatch rejection, and ReconnectRuntimeService stop-time cancellation of pending teardown requests; fusiondesk_qt_tcp_transport_tests covers the QtRuntimeTransportManager close-target adapter path.
Qt reconnect orchestrator note:

runtime/qt now also has QtReconnectRuntimeService, QtReconnectExecutor, and QtReconnectOrchestrator for the local TCP start/execution form of the reconnect orchestration plan. QtReconnectRuntimeService owns Qt timer expiry and delegates common coordination to ReconnectRuntimeService. QtReconnectExecutor implements the pure replacement-executor boundary; QtReconnectOrchestrator starts agent replacement listeners with the planned reason/keyframe intent, reconnects client replacement channels, and leaves rebind plus module replay in Session. fusiondesk_qt_tcp_transport_tests covers this owner-driven path.
Reconnect-aware module note:

Session reconnect now delegates module pause/resume notification to ModuleHost. ModuleHost filters running modules by affected ChannelKey bindings, pauses in stop order, resumes in start order, and only calls modules that implement the optional IReconnectAwareModule interface.
Reconnect rebind note:

Session reconnect now reaches the pure core channel-rebind boundary by carrying replacement IChannel instances plus ChannelReadyInfo in ReconnectRequest and invoking NetworkManager::rebindChannel. fusiondesk_session_manager_tests covers replacement video channel readiness, old channel replacement, unaffected control channel readiness, and post-rebind send routing through the new channel.
Reconnect ingress replay note:

Session reconnect now asks ModuleHost to replay active ingress routes for running modules after channel rebind, filtered by affected consumed channel bindings. fusiondesk_module_host_tests covers replay replacement without duplicate delivery, and fusiondesk_session_manager_tests records replay outcomes in lastReconnect.replayedIngress.
Qt reconnect replacement note:

runtime/qt now prepares concrete Qt TCP reconnect replacement channels from connect profiles or accepted QTcpSocket instances without marking ChannelRegistry ready, and QtRuntimeTransportManager can drive Session::reconnect in one call while Session remains the only rebind point. After successful one-call reconnect, runtime/qt closes and removes the previous active Qt transport for that channel. fusiondesk_qt_tcp_transport_tests covers replacement preparation, one-call reconnect, Session rebind, old transport teardown, and PayloadAck traffic over the rebound Qt channel.
Reconnect report note:

Session reconnect now exposes Session::lastReconnectReport and SessionSnapshot.lastReconnect. fusiondesk_session_manager_tests covers successful reconnect reporting and failed replacement-channel reporting, including ingress replay report presence on the successful path.
ReconnectRuntimeService now exposes ReconnectDiagnosticsReport through its snapshot, and QtReconnectExecutor passes the Session reconnect report back through the pure replacement-executor result when QtRuntimeTransportManager performs a rebind.

PC reconnect profile note:

fusiondesk_pc_client and fusiondesk_pc_agent now accept --reconnect-profile, --reconnect-after-ms, and --reconnect-reason for delayed session reconnect after startup. The current PC two-peer smoke uses full startup control/small_data/main_screen profiles, then a delayed degraded main_screen-only reconnect profile to prove scoped reconnect and post-reconnect display progress while control and small_data stay alive.

Router snapshot note:

NetworkRouter::submitIncoming now dispatches from a snapshot of matching handlers so reconnect replay or subscription changes during packet handling do not invalidate iteration. fusiondesk_network_manager_tests covers the subscription mutation path.
