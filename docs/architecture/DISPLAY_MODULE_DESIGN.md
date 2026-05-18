# Display Module Design

The first `FusionDesk` module is `display.screen`: server-side screen capture and client-side rendering.

This document defines the MVP boundary. Input, clipboard, audio, filesystem, printer, camera, and peripheral modules are out of scope for the first display slice.

## Goal

```text
Agent captures the primary screen.
Agent encodes or packages frame payloads.
Network sends frames over the video channel.
Client receives video payloads.
Client decodes and renders frames.
Client sends display control and recovery messages back to Agent.
Session diagnostics report display health.
```

## Legacy Reference Checklist

Server-side reference files:

```text
Source/Modules/Display/Server/cdisplay.*
Source/Modules/Display/Server/cscreenencoder.*
Source/Modules/Display/Server/Platform/Windows/cscreencapture.*
Source/Modules/Display/Server/Platform/LinuxX11/cscreencapture.*
Source/Modules/Display/Server/Screen/cscreenmanager.*
```

Client-side reference files:

```text
Source/Modules/Display/Client/cdisplay.*
Source/Modules/Display/Client/cscreendecoder.*
Source/Modules/Display/Client/crenderwidget.*
Source/Modules/Display/Client/cscreenrender.*
Source/Modules/Display/Client/copenglrender.*
Source/Modules/Display/Client/Screen/resolutioncalculator.*
```

Protocol reference types:

```text
VIDEO_PAYLOAD_HEADER
VIDEO_PAYLOAD_ACK
SCREEN_PAYLOAD_TYPE
ENCODER_TYPE
RENDER_TYPE
VIDEO_PIXEL_FORMAT
TRANSFER_DATA_TYPE_VIDEO
TRANSFER_DATA_TYPE_PAYLOAD_ACK
TRANSFER_DATA_TYPE_CURSOR_CHANGE
TRANSFER_DATA_TYPE_WATERMARK
```

Use these files to understand current behavior, packet fields, platform edge cases, and regression scenarios. Do not wrap these old classes as the target `FusionDesk` implementation.

The first implementation should create new `FusionDesk` modules and platform/framework adapters behind the interfaces in `MODULE_AND_INTERFACE_BLUEPRINT.md`.

For the mature reference analysis and the target production pipeline, use
`DISPLAY_SCREEN_PIPELINE_DESIGN.md`. It contains the mature open-source
reference comparison, adopted design decisions, production channel matrix,
runtime ownership, PC Qt surface boundary, Android controller boundary,
capability contracts, and staged delivery plan. This document remains the MVP
contract and current-state reference.

## Current FusionDesk Status

Implemented in the current MVP slice:

```text
DisplayAgentModule
DisplayClientModule
IDisplayCapture
IVideoEncoder
IVideoDecoder
IDisplayRenderer
DisplayPixelFormat
DisplayCaptureOpenOptions
DisplayCaptureSourceType plus nativeSourceHandle for monitor/window source selection
DisplayScaleMode source, fit, and stretch target sizing
DisplaySourceInfo, DisplaySourceGeometry, and DisplayTopologySnapshot
IDisplaySourceCatalog
DisplayRenderSurface
RawFrameEncoder
RawFrameDecoder
DisplayCodecId, DisplayCodecDirection, DisplayCodecBackendKind, DisplayCodecCapability, DisplayCodecSelectionRequest, and selectDisplayCodec as the pure runtime codec selection contract for raw/H.264/H.265/AV1 software, hardware, and zero-copy adapter rollout
IDisplayCodecBackendFactory, StaticDisplayCodecBackendFactory, RawFrameDisplayCodecBackendFactory, DisplayCodecBackendFactoryRegistry, createSelectedDisplayEncoder, and createSelectedDisplayDecoder as the pure runtime codec factory injection contract
DisplayCodecNegotiationRequest, DisplayCodecNegotiationResult, and negotiateDisplayCodec as the pure runtime two-peer codec intersection contract; it selects a codec only when agent encoder and client decoder selections both support the same codec, can fall back to raw when policy allows, and refuses silent fallback when an exact backend is pinned
DisplayCodecPeerProfilePayload and the `display.codec.v1` FDPP extension payload codec as the display-owned way to serialize encoder/decoder selection requests and codec candidate inventories without making runtime/connection understand display semantics
WindowsMediaFoundationDisplayCodecBackendFactory, probeWindowsMediaFoundationH264Codec, and preflightWindowsMediaFoundationH264Adapter as the first Windows H.264 codec capability/probe/preflight skeleton; it is rollout-gated, reports unavailable unless validation or production rollout policy enables selection, exercises BGRA-to-NV12 and decoder output preflight checks, and lets default PC codec diagnostics fall back to raw when policy is off or the host probe fails
preflightWindowsMediaFoundationH264Encode as the opt-in Windows H.264 single-frame encode/decode validation gate; it enumerates encoder MFT candidates, selects a CPU-buffer input subtype, feeds a synthetic BGRA/NV12 frame, produces a real H.264 bitstream when the host encoder supports it, wraps the bitstream in FDSF, and validates direct factory decode without making the adapter selectable
WindowsMediaFoundationH264Encoder private IVideoEncoder implementation created by WindowsMediaFoundationDisplayCodecBackendFactory::createEncoder when explicit ProductDisplayCodecPolicy or validation env enables the adapter; it drains all output after each input frame, uses monotonic sample timestamps, recreates the encoder for requested keyframes when the host MFT does not support AVEncVideoForceKeyFrame, keeps all-intra behavior when P-frame mode is disabled, and keeps encoder state for true P-frame validation or product rollout when policy enables it
WindowsMediaFoundationH264Decoder private IVideoDecoder implementation created by WindowsMediaFoundationDisplayCodecBackendFactory::createDecoder when FUSIONDESK_ENABLE_MF_CODEC is set; it accepts FDSF-wrapped H.264, selects decoder-published NV12/RGB32/ARGB32 output types, enables decoder low-latency mode when supported, marks keyframe input samples as clean points, preserves delayed-output frame metadata, converts NV12 output back to BGRA, recreates decoder state for keyframe sequence headers, and avoids per-frame drain on the P-frame continuous stream
FUSIONDESK_SELECT_MF_H264 validation-only selector gate; when paired with FUSIONDESK_ENABLE_MF_CODEC=1 it lets PC diagnostics select windows.media_foundation.h264 explicitly without changing the default raw fallback
ProductDisplayCodecPolicy product rollout gate, exposed in the PC shell as --display-codec-policy windows-h264-production; it lets Windows-Windows profiles select windows.media_foundation.h264 from the normal codec preference while preserving raw fallback if the policy is off or the host probe fails. FUSIONDESK_SELECT_MF_H264 remains a validation-only override.
FUSIONDESK_VALIDATE_PC_H264_DISPLAY validation gate for the real PC agent/client Windows-Windows H.264 first-frame and reconnect fresh-frame smoke path
FUSIONDESK_MF_H264_PFRAME opt-in gate for true Windows MediaFoundation P-frame delta compression, and FUSIONDESK_VALIDATE_MF_H264_PFRAME focused gate for keyframe plus P-slice codec validation
runtime/display convertBgraToNv12 and convertNv12ToBgra pure CPU conversion helpers with Nv12Frame plane metadata for the Windows MediaFoundation H.264 validation path and future codec adapters
raw frame payload schema with width, height, stride, pixel format, frame id, keyframe flag, and timestamp
FDSF encoded-video payload schema with codec id, Annex B/AVCC bitstream format, coded and visible dimensions, decoded pixel format, frame id, keyframe flag, timestamp, sequence-header/config bytes, and compressed bitstream bytes for future H.264/H.265/AV1 adapters
FDSC display control payload schema for keyframe request/scheduled response
DisplayModuleFactory
fake first-frame integration test
VIDEO Event from agent to client
PAYLOAD_ACK Request/Response for keyframe recovery over small_data
display client keyframe requests tracked through RequestTracker with timeout accounting
display agent rejects malformed or unsupported PAYLOAD_ACK control requests with terminal Error
display agent blocks empty encoded frames instead of sending unusable VIDEO packets
display agent rolls capture lifecycle back when startup first-frame send fails
second keyframe render after request
diagnostics events for first frame and keyframe recovery
basic DisplayAgentSnapshot and DisplayClientSnapshot counters for the MVP path
DisplayAgentSnapshot and DisplayClientSnapshot byte counters for captured pixels, encoded payloads, sent payloads, received payloads, decoded pixels, rendered pixels, and last-frame byte sizes
display snapshots include encode failures, invalid control requests, keyframe request failures/timeouts, pending keyframe requests, and decoder recovery attempts
display client drops stale or duplicate frames before decode, detects delta frame-id gaps, requests keyframe recovery, and reports dropped/frame-gap/decode-error counters
DisplayAgentSnapshot source id, target size, scale mode, capture-open failures, and send failures
DisplayAgentModule promotes captured source geometry, stride, or pixel-format changes to a keyframe/full-refresh frame and reports captureGeometryOrFormatChanges
delta frame drop when video channel is congested
keyframe recovery preserved while congested
display outbound packets carry SessionContext sessionId and traceId
session-mounted display modules route outbound packets through NetworkManager enqueue/flush so ChannelRegistry validation, PriorityScheduler queue policy, and pressure updates apply before NetworkRouter send
RuntimeHost ProductProfile mounts display.screen into role-specific modules through ModuleComposer
RuntimeHost forwards display capture options into DisplayModuleFactory
DisplayAgentModule owns capture open/close lifecycle and reports capture open failures
DisplayRuntimeService pure runtime owner for bounded agent frame pumping, target FPS, and client first-frame timeout recovery
DisplayCaptureBackendCapability, DisplayCaptureBackendSelectionRequest, and DisplayCaptureBackendSelectionResult pure runtime contracts
DisplayCaptureBackendSelectionRequest requestedAdapterId for explicit backend selection and field diagnostics
DisplayCapturePlatformPlanRequest, DisplayCaptureRuntimeRole, DisplayCaptureCapabilitySource, and planDisplayCapturePlatform pure runtime startup contract for role-aware capture planning, probed-factory capability selection, diagnostic-only default matrices, and client render-only handling
IDisplayCaptureBackendFactory and createSelectedDisplayCapture factory helper
IDisplayCaptureBackendFactory source catalog creation for UI/source-picker diagnostics without app-level backend switches
DisplaySourceSelectionRequest, DisplaySourceSelectionResult, selectDisplaySource, and displaySourceMatchesSelection pure runtime helpers for source-picker preflight against a DisplayTopologySnapshot
DisplayCaptureBackendFailoverRequest, selectDisplayCaptureBackendFailover, and createFailoverDisplayCapture pure runtime helper for excluding a failed backend and selecting the next compatible backend
DisplayCaptureStatus, DisplayCaptureStatusCode, and stable status names for platform adapter diagnostics
IDisplayCapture::captureErrors stable adapter error counter
DisplayCaptureRecoveryAction and decideDisplayCaptureRecovery pure runtime policy for capture timeout, hotplug, device loss, permission, and fallback decisions
DisplayCaptureRecoveryPolicy, DisplayCaptureRecoveryState, and DisplayCaptureRecoveryPlan for same-backend recovery limits, failed-recovery cooldown, and repeated recoverable-failure promotion to switch-backend
DisplayAgentModule::reopenCapture gives runtime a narrow recovery operation that closes and reopens the selected capture adapter and can emit a fresh keyframe
DisplayRuntimeService executes recoverable reopen/recreate capture decisions, can execute switch-backend through the configured backend factory, honors recovery cooldown/promotion policy, tracks recovery attempts/success/failures/cooldown blocks/promotions, and sends a fresh keyframe after successful recovery
DisplayRuntimeService can also monitor the client side for first-frame timeout when it is configured as a client runtime owner; if no frame has rendered within the configured timeout and no keyframe request is already pending, it sends a `FirstFrameTimeout` keyframe Request over `small_data`
DisplayRuntimeService client mode also tracks the rendered-frame baseline at reconnect resume and retries a `Reconnect` keyframe Request if no fresh frame renders after channel rebind
DisplayRuntimeServiceSnapshot exposes basic capture health metrics for production diagnostics: pump count, frame attempts, last pump/attempt timestamps, first/last delivered-frame timestamps, last observed frame age, effective FPS x1000, and consecutive frame misses
DisplayRuntimeServiceSnapshot also exposes service-observed captured pixel bytes, encoded payload bytes, sent payload bytes, last frame byte sizes, and effective bitrate Kbps so raw-frame MVP and future H.264/H.265 adapters share one throughput diagnostics surface
DisplayAgentSnapshot, DisplayRuntimeServiceSnapshot, SessionRuntimeDiagnosticsSnapshot, and PC shell session diagnostics carry the selected capture backend id, latest capture status, captureErrors count, and recovery action for runtime/UI consumption
SessionRuntimeDiagnosticsSnapshot and PC shell session diagnostics also project display captured/sent frame counts plus captured, encoded, and sent byte counters for product UI/service readers
DisplayCaptureOpenOptions includes an includeCursor flag; Windows GDI/DXGI capture adapters use WindowsCursorOverlay to compose the current Win32 cursor into BGRA frames by default while future cursor sideband remains a separate optimization
PC shell `--display-no-cursor` disables includeCursor for diagnostics or cursor sideband experiments, and capture plan/session diagnostics print the effective includeCursor value
StaticDisplayCaptureBackendFactory for capability-only unavailable/probe slots
DisplayCaptureBackendFactoryRegistry for cross-platform aggregate factory composition
unavailableDefaultDisplayCaptureBackendCapabilities and createUnavailableDefaultDisplayCaptureBackendFactory convert the full default backend matrix into diagnostic-only platform placeholders
default display capture backend matrix and selector for Windows, Linux X11, Linux Wayland, Linux embedded, macOS, Android agent/client, HarmonyOS/OpenHarmony, and RK Linux/Android targets
ProductProfile minimum channel specs are registered into the session NetworkManager before display module mount
JSON-loaded QtRuntimeTransportManager profile data can carry RuntimeHost-mounted display first-frame and keyframe recovery over Qt TCP
PC shell no-sessionId transport profiles can bind to the current runtime session before display start orchestration
QtRuntimeTransportManager tcpListenChannels can accept/adopt an inbound agent video channel before display start
PC shell --start-display waits for required display channels and rejects startup if they are still not ready
PC agent --start-display starts the agent DisplayRuntimeService after required channels and module start; --pump-display and --display-fps remain explicit runtime controls
PC client --start-display starts DisplayRuntimeService in client monitoring mode so first-frame timeout recovery is owned by runtime rather than by the app shell
PC client --show-display-window opens the first QWidget display surface, binds it to QtImageDisplayRenderer, and lets PC shell update a status line from DisplayProductHealthPresentation without exposing QWidget through display module contracts
PC agent --start-display passes the Windows aggregate capture factory and selection request into DisplayRuntimeService so auto-mode device-loss recovery can fail over from the selected backend to the next compatible backend
runtime/display withDefaultRawFrameCaptureTarget applies the bounded 1280x720 fit target for raw-frame MVP callers when no display target is supplied, so full-source BGRA frames do not become the default transport load before codec negotiation exists
runtime/display resolveDisplayCaptureOutputSize provides the shared source/stretch/fit output-size rule used by Windows GDI and DXGI capture adapters
PC shell display capture options include --display-source-type, --display-source-id, --display-target-width, --display-target-height, and --display-scale-mode
PC agent display backend selection accepts --display-target-platform, --display-target-arch, and --display-target-soc, parsed through runtime/display, for platform, architecture, and board-specific capture rollout diagnostics
PC agent display capture creation runs through planDisplayCapturePlatform using Windows aggregate factory probed capabilities before createSelectedDisplayCapture, so the shell path shares the full-platform startup plan contract
PC shell --print-display-capture-plan emits stable line-oriented capture plan diagnostics with capability source, candidates, selected adapter, rejection reasons, architecture, and SoC hints for scripts and future UI/service readers
PC shell --print-display-sources emits stable line-oriented source catalog diagnostics through the selected capture backend factory, including ok/source/requested/provider/rejected/message fields, requested source id/native handle, selected-source match state, selected=1 source rows, and rejection/message rows for failed source discovery
PC shell --print-display-codec-plan emits stable codec planning diagnostics for current raw fallback selection, future hardware/software codec rejection reasons, requested codec backend, codec preference, and architecture/SoC hints
SessionRuntimeDiagnosticsSnapshot and PC shell session diagnostics now emit session.diagnostics.display_codec rows for selected encoder/decoder adapter, codec, backend, selectionMode, fallbackReason, H.264 P-frame/delta-frame state, payload/error counters, and delayed decoder counters
DisplayProductDiagnosticsSnapshot, DisplayProductHealthLevel, buildDisplayProductDiagnostics, DisplayProductHealthPresentation, and buildDisplayProductHealthPresentation fold session/link/module/capture/codec diagnostics into product-facing ok/warning/degraded/blocked health with a usable flag plus stable status/action/capture/codec state codes; PC shell session diagnostics also emit session.diagnostics.display_health rows as transition tooling for future PC UI/service readers
PC shell --display-codec-negotiate-local runs the pure two-peer codec negotiation helper against local agent-encoder and client-decoder capability views, prints display.codec.negotiation diagnostics when --print-display-codec-plan is set, and pins the current role to the negotiated adapter; this is a Windows-Windows diagnostic bridge, not remote FDPP capability exchange
PC shell --display-codec-negotiate-fdpp runs the live FDPP display.codec.v1 codec inventory exchange over the bootstrap control channel, waits on the agent before display dependency creation, negotiates client decoder inventory against agent encoder inventory, and pins both roles to the selected adapter before display modules start
FUSIONDESK_ENABLE_MF_CODEC gates standalone MediaFoundation H.264 probe/preflight diagnostics; selector-based startup can use ProductDisplayCodecPolicy for production selection or FUSIONDESK_SELECT_MF_H264=1 for validation selection, preserving raw fallback when policy is off or the host probe fails
FUSIONDESK_VALIDATE_MF_H264_ENCODE gates the manual MediaFoundation single-frame encode/decode validation path so CI/default CTest does not require a host H.264 encoder/decoder
FUSIONDESK_VALIDATE_PC_H264_DISPLAY gates the real PC two-peer H.264 first-frame plus reconnect fresh-frame validation path so CI/default CTest does not require host MediaFoundation codec selection
fusiondesk_display_codec_negotiation_tests covers pure two-peer display codec negotiation for H.264 selection, raw fallback, exact-backend no-silent-fallback behavior, direction validation, and no-common-preference rejection
PC two-peer smoke starts agent/client executables with fusiondesk_pc_profile_plan generated no-sessionId control/small_data/main_screen profiles, gated display startup, and client rendered-frame verification
optional WindowsGdiDisplayCapture adapter target under platform/windows/display
WindowsGdiDisplaySourceCatalog monitor topology snapshot under platform/windows/display
WindowsDxgiDesktopDuplicationSourceCatalog monitor topology snapshot under platform/windows/display
WindowsGraphicsCaptureSourceCatalog monitor and visible-window topology snapshot under platform/windows/display
Windows GDI and DXGI reject invalid source ids with SourceNotFound instead of silently falling back to the primary monitor
WindowsGdiDisplayCapture source selection and source/fit/stretch scaling through GDI BitBlt/StretchBlt
WindowsGdiDisplayCaptureFactory exposes the GDI capability to the runtime backend selector
DisplayCaptureBackendCapability carries available/unavailableReason so production probes can publish high-priority backends without breaking fallback
DisplayCaptureBackendCapability also carries optional target architecture and SoC profile tags; generic platforms leave them empty, Rockchip paths use arm64 plus RK3568/RK3588 tags
WindowsDxgiDesktopDuplicationCapture compiles behind platform/windows/display as the first DXGI Desktop Duplication adapter path
WindowsDxgiDesktopDuplicationDisplayCaptureFactory can direct-create the DXGI adapter; default capability now runs a runtime probe and falls back to GDI when the probe fails, while FUSIONDESK_ENABLE_DXGI_CAPTURE=0 explicitly disables DXGI for diagnostics or rollout control
WindowsDxgiDesktopDuplicationCapture keeps the last successful frame and can resend it as a keyframe/full-refresh when DXGI reports no new frame during a keyframe request
fusiondesk_windows_dxgi_display_capture_opt_in_tests provides an explicit FUSIONDESK_VALIDATE_DXGI_CAPTURE=1 manual real-frame validation gate for DXGI Desktop Duplication without affecting default CTest runs; the gate also validates that DXGI honors the requested fit target
Windows GDI and DXGI adapters publish stable backendId plus lastStatus diagnostics for source-not-found, access/system-call failures, timeout, device loss, unsupported session, invalid frame, and success states
Windows DXGI maps native failures into stable status groups: access lost becomes SourceHotplug, device removed/reset becomes DeviceLost, and session/permission failures keep distinct recoverability semantics
DisplayRuntimeServiceSnapshot exposes the latest capture recovery action and recovery counters so UI/runtime readers do not parse adapter-specific errors
WindowsGraphicsCaptureDisplayCaptureFactory publishes a rollout-gated Windows Graphics Capture monitor/window backend; FUSIONDESK_ENABLE_WGC_CAPTURE=0 disables it, unset keeps it out of default rollout, and FUSIONDESK_ENABLE_WGC_CAPTURE=1 probes and creates the WGC adapter when the current Windows session supports WGC
DisplayCaptureOpenOptions carries sourceType and nativeSourceHandle so WGC can open monitor sources by sourceId and window sources either by sourceId from WindowsGraphicsCaptureSourceCatalog or by native HWND handle
WindowsDisplayCaptureBackendFactory uses DisplayCaptureBackendFactoryRegistry to aggregate Windows DXGI/WGC/GDI factories; current PC startup probes DXGI by default and falls back to GDI when DXGI is disabled or unavailable
PC agent --mount-display creates capture through WindowsDisplayCaptureBackendFactory + selector instead of directly new-ing the GDI adapter
PC agent --display-capture-backend can force auto, gdi, dxgi, wgc, or an exact adapter id for controlled rollout and diagnostics
optional QtImageDisplayRenderer adapter target under adapters/qt/display
optional QtImageDisplayWindow QWidget adapter target under adapters/qt/display for the first visible PC client surface
```

Production capture direction:

```text
Windows production capture should select DXGI Desktop Duplication or Windows Graphics Capture before falling back to GDI.
Linux production capture should select X11/XDamage/XShm, Wayland PipeWire portal, or DRM/KMS/GBM by session type and policy.
macOS production capture should prefer ScreenCaptureKit.
Android remains controller/client render-first; future Android agent capture uses MediaProjection behind an adapter.
HarmonyOS/OpenHarmony capture is adapter-only and device SDK dependent.
RK3568/RK3588 use the Linux or Android display path; vendor hardware acceleration belongs in codec adapters, not display modules.
```

Not implemented yet:

```text
production adapter injection into PC client and agent startup
production PC UI invocation of QtPeerProfileRuntimeService, peer coordination service execution, reconnect peer-profile invocation, and full UI-driven display lifecycle orchestration
production real capture policy, UI monitor selection, hotplug handling, and source-change recovery beyond the current runtime recovery owner
product-tuned backend-switch thresholds, product UI presentation of capture status diagnostics, and operator controls for recovery policy
non-Windows production capture adapter implementations behind the existing backend factories
hardware encoder/decoder adapter implementations
production PC UI/service binding to display codec diagnostics and non-raw decoder reset/quality adaptation after startup
production Qt OpenGL, D3D, VA, X11, or Android surface renderer
Linux, macOS, Android, HarmonyOS/OpenHarmony, LoongArch, mips64el, RK3568, and RK3588 display platform adapters
per-stage latency counters, product-level reconnect counters, decoder error counters, and actual product UI/service binding beyond the current DisplayProductDiagnosticsSnapshot/DisplayProductHealthPresentation surfaces
generic module factory/composition for non-display modules
automatic transport/channel binding from real adapters
```

Current reconnect-aware display behavior:

```text
DisplayAgentModule and DisplayClientModule implement optional IReconnectAwareModule.
ModuleHost pause/resume notifications are filtered by display main_screen or small_data channels.
Display client resume can request a keyframe through the existing PAYLOAD_ACK Request/Response path.
Display agent resume sends a fresh keyframe plus a follow-up delta when fresh state is requested, so one-frame-delayed codecs such as MediaFoundation H.264 P-frame streams have enough post-reconnect input to render.
```

## Module Manifest

```text
product capability alias: display.screen
sku: fusiondesk.module.display
feature: Screen
run modes: in-process, hosted
```

Concrete role manifests:

```text
module id: display.screen.agent
role: agent
required channels:
  main_screen, video, required
  small_data, standard/control, required for display control and recovery
optional channels:
  second_screen, video
consumes:
  main_screen: WATERMARK
  small_data: PAYLOAD_ACK
produces:
  main_screen: VIDEO, CURSOR_CHANGE, WATERMARK
  small_data: PAYLOAD_ACK

module id: display.screen.client
role: client
required channels:
  main_screen, video, required
  small_data, standard/control, required for display control and recovery
optional channels:
  second_screen, video
consumes:
  main_screen: VIDEO, CURSOR_CHANGE, WATERMARK
  small_data: PAYLOAD_ACK
produces:
  small_data: PAYLOAD_ACK
```

MVP channel split:

```text
VIDEO remains on main_screen. Request-like display control and recovery traffic
routes over small_data so video channel degradation does not block
keyframe/full-refresh recovery.
```

## Server Components

```text
DisplayAgentModule
  -> ScreenSource
  -> CapturePipeline
  -> EncoderPipeline
  -> DisplaySender
  -> DisplayAckHandler
```

Responsibilities:

```text
DisplayAgentModule: lifecycle, policy, diagnostics
ScreenSource: selected monitor and capture configuration
CapturePipeline: platform capture loop
EncoderPipeline: video or graphic payload generation
DisplaySender: network send and queue pressure handling
DisplayAckHandler: keyframe, timestamp, open/close device ACK handling
```

Server start sequence:

```text
authorize display.screen
wait for main_screen video channel and small_data control channel ready
create ScreenSource for primary screen
initialize CapturePipeline
initialize EncoderPipeline
start through ModuleHost
activate PAYLOAD_ACK ingress
start capture loop
send first keyframe
```

## Client Components

```text
DisplayClientModule
  -> DisplayReceiver
  -> DecoderPipeline
  -> RenderSurface
  -> RenderBackend
  -> DisplayAckSender
```

Responsibilities:

```text
DisplayClientModule: lifecycle, policy, diagnostics
DisplayReceiver: VIDEO packet ingress and ordering checks
DecoderPipeline: decode video or graphic payload
RenderSurface: UI-provided native surface handle
RenderBackend: DX, GL, VA, XV, X11, or auto
DisplayAckSender: timestamp ACK and keyframe request
```

Client start sequence:

```text
authorize display.screen
create render surface
select render backend
start through ModuleHost
activate VIDEO ingress
activate CURSOR_CHANGE ingress when enabled
send open render device request
wait for first keyframe
decode and render
```

## Packet Flow

Primary video:

```text
Agent CapturePipeline
  -> EncoderPipeline
  -> PacketEnvelope(channel=main_screen, type=VIDEO, priority=Realtime)
  -> NetworkManager::enqueue
  -> PriorityScheduler
  -> NetworkManager::flushNext
  -> NetworkRouter::send
  -> VideoSocket
  -> Client NetworkRouter::submitIncoming
  -> DisplayReceiver
  -> DecoderPipeline
  -> RenderBackend
```

ACK:

```text
Client DisplayAckSender
  -> PacketEnvelope(channel=small_data, type=PAYLOAD_ACK, kind=Request, priority=Interactive)
  -> NetworkManager::enqueue
  -> PriorityScheduler
  -> NetworkManager::flushNext
  -> NetworkRouter::send
  -> ControlSocket or configured small-data socket
  -> Agent NetworkRouter::submitIncoming
  -> DisplayAckHandler
  -> PacketEnvelope(type=PAYLOAD_ACK, kind=Response, responseTo=request.messageId)
```

Keyframe recovery:

```text
decoder reset
frame disorder
channel reconnect
first frame timeout
  -> client sends PAYLOAD_ACK Request for keyframe
  -> agent sends PAYLOAD_ACK Response
  -> agent sends keyframe VIDEO Event
```

## Frame Policy

Frame types:

```text
keyframe
delta frame
graphic patch
cursor update
watermark update
```

Queue rules:

```text
keyframes are retained until sent or superseded by a newer keyframe.
delta frames may be dropped when video queue is congested.
graphic patches may be dropped if they are older than a newer keyframe.
cursor update can bypass video frame backlog.
watermark update is normal priority and reliable.
```

Latency rule:

```text
Prefer dropping stale realtime frames over building latency.
```

## Channel Priority

```text
VIDEO: Realtime
PAYLOAD_ACK: Interactive
CURSOR_CHANGE: Realtime
WATERMARK: Normal
screen size/control messages: Critical or Normal depending on subtype
```

## Request/Response Rules

Display video frames are realtime events:

```text
VIDEO: Event, NoResponseRequired
CURSOR_CHANGE: Event, NoResponseRequired
WATERMARK: Event or Request depending on policy
```

Display control and recovery messages are correlated requests:

```text
request keyframe: Request -> Response or Error
open render device: Request -> Response or Error
close render device: Request -> Response or Error
decoder reset notification: Event or Request depending on whether peer action is required
```

`PAYLOAD_ACK` is a display control and recovery packet type, not a raw transport acknowledgement. Request-like `PAYLOAD_ACK` messages must carry `messageId`, `correlationId`, `timeoutMs`, and must receive a `Response` or `Error`. Pure telemetry timestamp feedback may be an `Event` only when the feature contract explicitly marks it `NoResponseRequired`.

The current keyframe recovery payload schema is `FDSC` v1. It carries a display
control operation, keyframe reason, reserved flags, and optional frame id. The
module rejects legacy string payloads and malformed payloads with a terminal
`Error`; generic protocol validation still stays in the envelope/channel layer.

## MVP Scope

Included:

```text
single primary screen
video channel setup
server capture implementation behind `IDisplayCapture`
client render implementation behind `IDisplayRenderer`
VIDEO packet route
PAYLOAD_ACK route over small_data
keyframe request
basic diagnostics
network pressure response
```

Deferred:

```text
second screen
watermark
cursor pixel cache
adaptive resolution
hardware codec adapter implementations
product UI binding for codec negotiation state and module-start codec diagnostics
multi-monitor hotplug
Android SurfaceTexture / HardwareBuffer / MediaCodec optimized render path
P2P tunnel optimization
```

## Android Client Render Boundary

Android starts with a Qt-backed render surface for speed of delivery.

Future render modes:

```text
Qt OpenGL render surface
Android SurfaceTexture bridge
Android HardwareBuffer path
MediaCodec decoder path
software fallback
```

Rules:

```text
Display decode/render internals remain in the display module.
Android app code only attaches a render target and receives state callbacks.
AAR public API must not expose Qt UI types.
Surface recreation must request a keyframe.
Render backend reset must not force session disconnect.
```

## Failure Handling

Server failures:

```text
capture device lost -> stop capture, notify session, retry platform capture when policy allows
encoder failure -> request encoder reset, then send keyframe
video channel congested -> reduce frame rate and drop stale deltas
video channel failed -> pause capture and enter reconnect flow
```

Client failures:

```text
decoder failure -> reset decoder and request keyframe
render backend failure -> recreate backend or fall back to software/auto
first frame timeout -> request keyframe
payload disorder -> drop stale frame; delta frame gap -> request keyframe
```

## Diagnostics

MVP counters implemented:

```text
captured frames
encoded frames
sent frames
dropped frames
received frames
decoded frames
rendered frames
dropped frames
frame gaps
decode errors
keyframe requests
keyframe responses
render errors
last keyframe request id
last rendered frame id
capture geometry or pixel-format changes promoted to keyframes
capture errors
```

Production counters still required:

```text
channel reconnects
decoder errors
average encode latency
average network latency from timestamp ACK
average render latency
```

Required events:

```text
display module started
display module stopped
video channel ready
first frame sent
first frame rendered
keyframe requested
capture device lost
decoder reset
render backend changed
video channel congested
```

## Acceptance Gate

The first display module is accepted when:

```text
Agent can start display.screen.agent through ModuleHost.
Client can start display.screen.client through ModuleHost.
main_screen channel is registered through NetworkRouter.
small_data channel is registered through NetworkRouter.
VIDEO packets route server to client.
PAYLOAD_ACK Request/Response packets route client to server and back over small_data.
Client renders first frame.
Client can request keyframe and server responds.
Video congestion does not grow unbounded latency.
Session diagnostics show channel, capture, decode, and render state.
```
