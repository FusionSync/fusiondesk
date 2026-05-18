# Module And Interface Blueprint

This document defines the large modules, small modules, ownership boundaries, and abstract interfaces for the `FusionDesk` rebuild.

## Ownership Rule

```text
Modules own feature behavior.
Network owns packet transport and routing.
Session owns lifecycle and orchestration.
Policy owns allow, deny, audit, and constraints.
Platform owns OS-specific services.
Adapters own integration with external frameworks and technologies. Old code is reference-only.
Apps own process startup only.
```

## Large Modules

### Core

Small modules:

```text
core/protocol
core/session
core/network
core/module
core/policy
core/diagnostics
```

Responsibilities:

```text
define value types
define abstract contracts
define lifecycle state
define routing and manifest semantics
define policy decision shape
```

Non-responsibilities:

```text
no Qt
no sockets
no OS handles
no codecs
no app windows
no legacy Source dependency
```

### Runtime

Small modules:

```text
runtime/host
runtime/session
runtime/product
runtime/diagnostics
runtime/feature
runtime/qt
```

Responsibilities:

```text
compose sessions
load product profiles
create module instances
bind network, policy, and diagnostics
bridge to event loops and thread executors
```

Non-responsibilities:

```text
no direct screen capture
no direct input injection
no direct filesystem redirection logic
no business packet parsing outside session control
```

### Network

Small modules:

```text
transport sockets
channel registry
priority scheduler
network router
reconnect manager
socket factory
```

Responsibilities:

```text
open and bind sockets
register logical channels
route packets
apply priority and queue policy
report pressure
rebind channels after reconnect
```

Non-responsibilities:

```text
no codec parsing
no module policy
no UI notification
no feature-specific state machines
```

### Display

Small modules:

```text
display.screen
display.cursor
display.watermark
display.monitor
display.render
display.codec
```

Responsibilities:

```text
agent capture and encode
client decode and render
module-owned codec capability declaration and adapter selection
cursor synchronization
watermark rendering or overlay
screen geometry and monitor changes
keyframe and ACK behavior
```

Non-responsibilities:

```text
no input event ownership
no socket ownership
no login or policy ownership
no direct app window dependency
```

### Input

Small modules:

```text
input.keyboard
input.mouse
input.touch
input.gamepad
input.shortcut
```

Responsibilities:

```text
client-side capture
agent-side injection
coordinate mapping
shortcut policy
lock key synchronization
optional low-latency transport selection
```

Non-responsibilities:

```text
no display frame ownership
no network reconnect ownership
no policy storage
```

### Audio

Small modules:

```text
audio.desktop
audio.microphone
audio.codec
audio.device
```

Responsibilities:

```text
desktop audio capture and playback
microphone capture and playback
format negotiation
jitter handling
mute and volume state
```

Non-responsibilities:

```text
no camera pipeline ownership
no control channel ownership
no enterprise policy decisions
```

### Data Redirection

Small modules:

```text
clipboard.redirect
filesystem.redirect
printer.redirect
data.stream
data.audit
```

Responsibilities:

```text
clipboard formats and content
file and directory redirection
printer job lifecycle
large payload chunking
request and response correlation
audit event generation
```

Non-responsibilities:

```text
no realtime media routing
no generic socket management
no hidden policy checks outside policy service
```

### Camera

Small modules:

```text
camera.capture
camera.codec
camera.render
camera.device
```

Responsibilities:

```text
camera enumeration
device busy state
capture and encode
decode and render
permission handoff
```

Non-responsibilities:

```text
no desktop screen capture
no microphone ownership
```

### Security, Policy, And Audit

Small modules:

```text
auth.login
auth.capability
policy.license
policy.enterprise
policy.device
audit.events
```

Responsibilities:

```text
login result integration
feature negotiation
module authorization
transport constraints
directional data policy
audit requirements
runtime revoke
```

Non-responsibilities:

```text
no packet IO
no platform device work
no feature implementation
```

### Platform

Small modules:

```text
platform.windows
platform.linux
platform.macos
platform.android
platform.common
```

Responsibilities:

```text
native screen capture APIs
native render surface APIs
input injection APIs
audio device APIs
clipboard APIs
printer and filesystem OS integration
permission and lifecycle helpers
```

Non-responsibilities:

```text
no session policy
no product profile
no business module orchestration
```

### Adapters

Small modules:

```text
adapters.qt
adapters.transport
adapters.display
adapters.codec
```

Responsibilities:

```text
wrap Qt sockets, timers, threads, and surfaces
wrap concrete transport libraries
wrap codec libraries or platform media APIs
translate compatibility wire frames to PacketEnvelope where protocol compatibility requires it
isolate framework and OS assumptions
```

Non-responsibilities:

```text
no new business logic
no core abstractions
no policy decisions
no runtime wrapping of old Source modules as the target implementation
```

### Bindings

Small modules:

```text
bindings.android.facade
bindings.android.jni
bindings.android.lifecycle
```

Responsibilities:

```text
public Android controller API
JNI opaque handle management
Surface lifecycle bridge
Java/Kotlin listener callbacks
AAR packaging
```

Non-responsibilities:

```text
no direct module implementation
no public Qt types
no public JNI implementation details
```

## Core Abstract Interfaces

The following signatures are design-level contracts. Concrete signatures may adjust types as code lands, but dependencies and responsibilities should not change.

Interface status:

```text
These are target contracts for runtime, adapters, and modules.
Current G1 core exposes concrete ProtocolValidator, PacketCodec,
CapabilityPayloadCodec, CapabilityNegotiator, CapabilityExchange, and
ChannelAllowlistValidator classes. Current G2 core also exposes concrete
ChannelRegistry, PriorityScheduler, default channel specs, and NetworkManager
skeleton classes, plus ITransportSocket and SocketGroup.
NetworkManager can already route packet bytes to SocketGroup by ChannelSpec.socketClass in core tests.
Current G4 core has ModuleHost gates for session role, local platform,
required module presence, required channel readiness, and policy authorization.
ModuleRuntime carries the session-owned ChannelRegistry so modules still do not own sockets.
Abstract IProtocolValidator, IPacketCodec, and capability service interfaces
are introduced only when runtime composition needs injectable implementations.
```

### Session

```cpp
class ISessionController {
public:
    virtual ~ISessionController() = default;
    virtual SessionId id() const = 0;
    virtual SessionRole role() const = 0;
    virtual SessionState state() const = 0;
    virtual const SessionContext& context() const = 0;
    virtual Result start(const SessionStartOptions& options) = 0;
    virtual Result connect(const ConnectRequest& request) = 0;
    virtual Result reconnect(const ReconnectRequest& request) = 0;
    virtual void requestStop(const StopReason& reason) = 0;
    virtual Result setFeatureEnabled(FeatureId feature, bool enabled) = 0;
    virtual HealthSnapshot health() const = 0;
};

class ISessionManager {
public:
    virtual ~ISessionManager() = default;
    virtual SessionId createSession(const SessionCreateOptions& options) = 0;
    virtual ISessionController* findSession(SessionId id) = 0;
    virtual Result closeSession(SessionId id, const StopReason& reason) = 0;
    virtual std::vector<SessionSnapshot> sessions() const = 0;
};
```

### Network

```cpp
class ITransportSocket {
public:
    virtual ~ITransportSocket() = default;
    virtual SocketClass socketClass() const = 0;
    virtual Result open(const SocketOpenOptions& options) = 0;
    virtual void close(const CloseReason& reason) = 0;
    virtual SendResult write(const ByteBuffer& bytes) = 0;
    virtual SocketState state() const = 0;
};

class IChannelRegistry {
public:
    virtual ~IChannelRegistry() = default;
    virtual Result registerChannel(const ChannelSpec& spec, std::shared_ptr<IChannel> channel) = 0;
    virtual void markReady(ChannelKey key, ChannelReadyInfo ready) = 0;
    virtual void markPressure(ChannelKey key, ChannelPressure pressure) = 0;
    virtual ChannelSnapshot snapshot(ChannelKey key) const = 0;
    virtual std::vector<ChannelSnapshot> channels() const = 0;
};

class IPriorityScheduler {
public:
    virtual ~IPriorityScheduler() = default;
    virtual EnqueueResult enqueue(const PacketEnvelope& packet, const SendOptions& options) = 0;
    virtual std::optional<PacketEnvelope> nextPacket(SocketClass socketClass) = 0;
    virtual ChannelPressure pressure(ChannelKey key) const = 0;
    virtual void drain(ChannelKey key, DrainReason reason) = 0;
};

class INetworkRouter {
public:
    virtual ~INetworkRouter() = default;
    virtual bool registerChannel(std::shared_ptr<IChannel> channel) = 0;
    virtual void unregisterChannel(ChannelId channelId, ChannelType channelType) = 0;
    virtual SendResult send(const PacketEnvelope& packet) = 0;
    virtual SubscriptionToken subscribe(const RouteMatch& route, PacketHandler handler) = 0;
    virtual void unsubscribe(SubscriptionToken token) = 0;
    virtual void submitIncoming(const PacketEnvelope& packet) = 0;
};

class IRequestTracker {
public:
    virtual ~IRequestTracker() = default;
    virtual MessageId nextMessageId() = 0;
    virtual TrackResult track(const PacketEnvelope& request, ResponseHandler handler) = 0;
    virtual bool complete(const PacketEnvelope& response) = 0;
    virtual std::size_t cancelByChannel(ChannelId channelId, ChannelType channelType, ResponseStatus status) = 0;
    virtual std::size_t expire(std::uint64_t nowUsec) = 0;
};

class IProtocolValidator {
public:
    virtual ~IProtocolValidator() = default;
    virtual ProtocolValidationResult validate(const PacketEnvelope& packet) const = 0;
};

class IPacketCodec {
public:
    virtual ~IPacketCodec() = default;
    virtual ByteBuffer encode(const PacketEnvelope& packet) const = 0;
    virtual PacketDecodeResult decode(const ByteBuffer& bytes) const = 0;
};
```

### Module

```cpp
class IModule {
public:
    virtual ~IModule() = default;
    virtual const ModuleManifest& manifest() const = 0;
    virtual ModuleState state() const = 0;
    virtual bool attach(const ModuleRuntime& runtime) = 0;
    virtual bool start(const ModuleStartOptions& options) = 0;
    virtual void stop(const ModuleStopOptions& options) = 0;
    virtual void detach() = 0;
    virtual DiagnosticsSnapshot diagnostics() const = 0;
};

struct ModuleReconnectOptions {
    std::string reason;
    std::vector<ChannelKey> affectedChannels;
    std::uint32_t reconnectCount = 0;
    bool requestFreshState = false;
};

struct ModuleIngressReplayReport {
    std::string moduleId;
    bool replayed = false;
    std::size_t tokenCount = 0;
    std::string diagnostics;
    std::string message;
};

class IReconnectAwareModule {
public:
    virtual ~IReconnectAwareModule() = default;
    virtual void pauseForReconnect(const ModuleReconnectOptions& options) = 0;
    virtual void resumeAfterReconnect(const ModuleReconnectOptions& options) = 0;
};

struct ReconnectReport {
    bool attempted = false;
    bool ok = false;
    std::uint32_t reconnectCount = 0;
    std::string reason;
    bool requestedFreshState = false;
    std::vector<ReconnectChannelReport> degradedChannels;
    std::vector<ReconnectChannelReport> reboundChannels;
    std::vector<ModuleReconnectReport> pausedModules;
    std::vector<ModuleIngressReplayReport> replayedIngress;
    std::vector<ModuleReconnectReport> resumedModules;
};

class IModuleFactory {
public:
    virtual ~IModuleFactory() = default;
    virtual bool supports(const std::string& requestedModuleId,
                          const ModuleCreateOptions& options) const = 0;
    virtual ModuleManifest manifest(const ModuleCreateOptions& options) const = 0;
    virtual std::shared_ptr<IModule> create(const ModuleCreateOptions& options) const = 0;
};

struct ModuleCompositionRequest {
    std::vector<std::string> requiredModules;
    ModuleCreateOptions createOptions;
};

struct ModuleCompositionResult {
    std::vector<std::shared_ptr<IModule>> modules;
    std::vector<ModuleManifest> manifests;
    std::vector<std::string> missingModules;
    std::vector<std::string> dependencyFailures;
};

class IModuleHost {
public:
    virtual ~IModuleHost() = default;
    virtual Result addFactory(std::shared_ptr<IModuleFactory> factory) = 0;
    virtual CompositionResult compose(const ProductProfile& profile) = 0;
    virtual std::vector<ModuleStartReport> startAllowedModules() = 0;
    virtual std::vector<ModuleReconnectReport> pauseRunningModulesForReconnect(const ModuleReconnectOptions& options) = 0;
    virtual std::vector<ModuleIngressReplayReport> replayRunningModuleIngressForReconnect(const ModuleReconnectOptions& options) = 0;
    virtual std::vector<ModuleReconnectReport> resumeRunningModulesAfterReconnect(const ModuleReconnectOptions& options) = 0;
    virtual Result setModuleEnabled(const std::string& moduleId, bool enabled) = 0;
    virtual void stopAll(const ModuleStopOptions& options) = 0;
};
```

### Policy And Audit

```cpp
class IPolicyEngine {
public:
    virtual ~IPolicyEngine() = default;
    virtual FeatureSet authorizeFeatures(const PolicyContext& context, FeatureSet requested) const = 0;
    virtual PolicyDecision authorizeModule(const PolicyContext& context, const ModuleManifest& manifest) const = 0;
    virtual PolicyDecision authorizePacket(const PolicyContext& context, const PacketEnvelope& packet) const = 0;
    virtual AuditDecision audit(const AuditEvent& event) const = 0;
};

class IPolicyProvider {
public:
    virtual ~IPolicyProvider() = default;
    virtual PolicyBundle loadPolicy(const PolicyLoadRequest& request) = 0;
    virtual PolicyVersion currentVersion() const = 0;
};
```

### Diagnostics

```cpp
class IDiagnosticsSink {
public:
    virtual ~IDiagnosticsSink() = default;
    virtual void publish(const DiagnosticEvent& event) = 0;
    virtual void publishMetric(const MetricSample& metric) = 0;
    virtual DiagnosticsSnapshot snapshot(SessionId sessionId) const = 0;
};
```

Current display seam note:

```text
The current FusionDesk display code exposes pure C++ DisplayPixelFormat, DisplayCaptureOpenOptions, DisplayRenderSurface, RawFrameEncoder/RawFrameDecoder, and MVP lifecycle defaults on IDisplayCapture and IDisplayRenderer.
Concrete Windows capture and Qt image render adapters live outside core and are injected through DisplayMvpDependencies.
DisplayAgentModule currently tracks the last successfully sent frame shape and promotes source, geometry, stride, or pixel-format changes to a keyframe/full-refresh frame through the module-owned display payload path.
IDisplayCapture now also exposes stable backend identity, structured lastStatus, and captureErrors counters for runtime/UI diagnostics without leaking platform adapter internals.
DisplayClientModule records the rendered-frame baseline at reconnect resume, and DisplayRuntimeService can retry a reconnect keyframe/full-refresh request if the client does not render a fresh frame after channel rebind.
The broader target contracts below still describe the production interface direction.
```

## Feature Abstract Interfaces

### Display

```cpp
class IDisplayCapture {
public:
    virtual ~IDisplayCapture() = default;
    virtual Result open(const DisplayCaptureOptions& options) = 0;
    virtual std::vector<DisplayInfo> displays() const = 0;
    virtual Result selectDisplay(DisplayId id) = 0;
    virtual CaptureResult captureNextFrame() = 0;
    virtual void requestKeyFrame() = 0;
    virtual void close() = 0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual Result configure(const VideoEncodeConfig& config) = 0;
    virtual EncodedFrame encode(const CapturedFrame& frame, EncodeHint hint) = 0;
    virtual void reset(const ResetReason& reason) = 0;
};

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;
    virtual Result configure(const VideoDecodeConfig& config) = 0;
    virtual DecodedFrame decode(const EncodedFrame& frame) = 0;
    virtual void reset(const ResetReason& reason) = 0;
};

class IDisplayRenderer {
public:
    virtual ~IDisplayRenderer() = default;
    virtual Result attachSurface(const RenderSurface& surface) = 0;
    virtual void detachSurface() = 0;
    virtual Result render(const DecodedFrame& frame) = 0;
    virtual void setCursor(const CursorImage& cursor) = 0;
    virtual void resize(const DisplayGeometry& geometry) = 0;
};
```

### Input

```cpp
class IInputCapture {
public:
    virtual ~IInputCapture() = default;
    virtual Result start(IInputSink& sink, const InputCaptureOptions& options) = 0;
    virtual void stop() = 0;
};

class IInputInjector {
public:
    virtual ~IInputInjector() = default;
    virtual Result injectMouse(const MouseEvent& event) = 0;
    virtual Result injectKeyboard(const KeyboardEvent& event) = 0;
    virtual Result injectTouch(const TouchEvent& event) = 0;
    virtual Result injectGamepad(const GamepadEvent& event) = 0;
};
```

Current `FUSIONDESK-MODULE-001B` implementation shape:

```text
input.mouse resolves to input.mouse.client or input.mouse.agent by SessionRole.
input.keyboard resolves to input.keyboard.client or input.keyboard.agent by SessionRole.
InputClientModule produces Mouse/Keyboard Event packets on small_data.
InputAgentModule consumes Mouse/Keyboard Event packets and calls IInputInjector.
Input payloads use the pure C++ FDIN schema.
Input Event packets are NoResponseRequired and Coalescable; policy and channel readiness still gate module start through ModuleHost.
Production Qt capture and OS injection adapters are pending and must live outside modules.
```

Current `FUSIONDESK-MODULE-001C/001D/001E/001F` adapter, startup, runtime pump, and policy seams:

```text
WindowsInputInjector implements IInputInjector as a dry-run platform seam.
QtInputCapture implements IInputCapture and converts Qt mouse/key events into MouseInputEvent and KeyboardInputEvent queues.
RuntimeHost can pass IInputInjector dependencies into InputModuleFactory during ProductProfile mounting.
InputClientModule can own an optional IInputCapture dependency and opens/closes it through the module lifecycle.
PC shell startup can mount input profile modules through --mount-input or --profile-module and inject QtInputCapture/WindowsInputInjector dependencies without modules seeing Qt or OS APIs.
FeatureRuntimeService lives under runtime/feature, polls IInputCapture, and sends captured mouse/keyboard events through InputClientModule APIs.
PC shell can start that owner through --pump-profile-modules while QTimer remains in the shell/runtime-Qt boundary.
FeatureRuntimePolicy can deny or audit input operations before module send APIs are called.
FeatureRuntimeService can own IInputCapture open/close when it is the active pump owner, so future production startup avoids two client modules opening the same capture source.
Production global hooks, RawInput, shortcut policy, and UAC/security desktop handling are pending concrete adapter work.
```

### Audio

```cpp
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual Result open(const AudioSourceOptions& options) = 0;
    virtual AudioFormat format() const = 0;
    virtual AudioChunk read() = 0;
    virtual void close() = 0;
};

class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual Result open(const AudioSinkOptions& options) = 0;
    virtual Result render(const AudioChunk& chunk) = 0;
    virtual void setVolume(int level) = 0;
    virtual void close() = 0;
};
```

### Clipboard, Filesystem, Printer

```cpp
class IClipboardEndpoint {
public:
    virtual ~IClipboardEndpoint() = default;
    virtual FormatList formats() const = 0;
    virtual Result setFormats(const FormatList& formats) = 0;
    virtual DataResult requestFormat(const ClipboardFormatRequest& request) = 0;
    virtual StreamResult openFileContents(const ClipboardFileRequest& request) = 0;
};

class IFileRedirection {
public:
    virtual ~IFileRedirection() = default;
    virtual Result addPath(const LocalPath& path, FsPermission permission) = 0;
    virtual Result removePath(FsSessionId id) = 0;
    virtual FsResponse handleRequest(const FsRequest& request) = 0;
};

class IPrinterBridge {
public:
    virtual ~IPrinterBridge() = default;
    virtual Result installOrAttach(const PrinterBridgeOptions& options) = 0;
    virtual PrintJobResult openJob(const PrintJobRequest& request) = 0;
    virtual Result writeChunk(const PrintChunk& chunk) = 0;
    virtual Result closeJob(PrintJobId id, PrintJobCloseReason reason) = 0;
};
```

Current clipboard status:

```text
Clipboard code is removed from the active FusionDesk slice to keep display.screen focused.
Rebuild clipboard later as a data-redirection module, not as a mainline/session shortcut.
The future clipboard module must own FDCL-style payload compatibility, Request/Response
correlation, watcher state, stream chunking, and content policy decisions.
Qt and platform clipboard endpoints must stay in adapters/platform and enter through
module-owned interfaces only.
```

### Camera

```cpp
class ICameraSource {
public:
    virtual ~ICameraSource() = default;
    virtual std::vector<CameraDevice> devices() const = 0;
    virtual Result open(CameraDeviceId id, const CameraOptions& options) = 0;
    virtual CameraFrame capture() = 0;
    virtual void close() = 0;
};

class ICameraRenderer {
public:
    virtual ~ICameraRenderer() = default;
    virtual Result render(const CameraFrame& frame) = 0;
    virtual void reset() = 0;
};
```

### Platform

```cpp
class IPlatformInfo {
public:
    virtual ~IPlatformInfo() = default;
    virtual PlatformKind platform() const = 0;
    virtual CpuArch cpuArch() const = 0;
    virtual std::string osVersion() const = 0;
    virtual CapabilitySet capabilities() const = 0;
};

class IPermissionService {
public:
    virtual ~IPermissionService() = default;
    virtual PermissionState state(PermissionKind permission) const = 0;
    virtual Result request(PermissionKind permission) = 0;
};
```

### Android Binding

```cpp
class IAndroidControllerRuntime {
public:
    virtual ~IAndroidControllerRuntime() = default;
    virtual ControllerHandle create(const AndroidControllerOptions& options) = 0;
    virtual Result connect(ControllerHandle handle, const AndroidConnectRequest& request) = 0;
    virtual Result attachSurface(ControllerHandle handle, AndroidSurfaceHandle surface) = 0;
    virtual void detachSurface(ControllerHandle handle) = 0;
    virtual void close(ControllerHandle handle) = 0;
};
```

## Module Manifest Direction Rule

Each feature must define role-specific bindings.

Example:

```text
display.screen.agent
  consumes: PAYLOAD_ACK, CONTROL
  produces: VIDEO, CURSOR_CHANGE, WATERMARK

display.screen.client
  consumes: VIDEO, CURSOR_CHANGE, WATERMARK
  produces: PAYLOAD_ACK, CONTROL
```

Do not use one concrete manifest to hide both sides unless the manifest explicitly lists role-scoped bindings.

## Module Version And Module-Owned Protocol Compatibility

Mainline module management is deliberately narrow:

```text
RuntimeHost, SessionMainline, ModuleCatalog, ModuleComposer, and ModuleHost may inspect:
moduleId
module version
role flags
platform support
module dependency ids and version ranges
channel bindings
policy-visible feature flags
```

They must not parse or decide module payload schema compatibility. FDIN, FDCL, raw display frame payloads, FDPP, FDRT, and future module-specific payloads remain owned by their modules or runtime services.

Manifest direction:

```text
ModuleManifest.version declares the local module implementation version.
ModuleManifest.compatiblePeers declares which peer module ids and version ranges this module can start with.
ModulePeerCompatibility.compatibilityMode is an opaque module-owned mode such as native, v1-family, downgraded-v1, or relay-only.
ModuleStartOptions.peerVersions can carry remote module version facts into module.start after the session or runtime has exchanged manifests.
ModuleHost recalculates peer compatibility from the target module manifest before calling module.start and does not trust caller-supplied compatibility flags.
```

Boundary rule:

```text
The mainline can reject a missing module, unsupported role/platform, missing dependency, incompatible module version, missing required channel, or denied policy.
After a module starts, payload schema compatibility, operation compatibility, downgrade translation, and protocol rejection are handled inside that module.
```

The first test-only module for this boundary is `test.protocol.client` plus `test.protocol.agent`. It uses `PacketType::Exchange` on the control channel to prove a versioned module can start through ModuleHost while its own Request/Response payload handling remains internal.

## ModuleHost Start Gate

Module startup is a staged gate. A module must pass all checks before its `start()` method is called.

```text
1. addModule rejects an empty or duplicate module id.
2. addModule rejects unsupported session role before attach.
3. addModule rejects unsupported local platform before attach.
4. addModule calls attach only after role and platform are accepted.
5. startAllowedModules rejects missing required module dependencies.
6. startAllowedModules rejects missing required channel readiness.
7. startAllowedModules calls IPolicyEngine::authorizeModule.
8. startAllowedModules can pass negotiated peer module versions through ModuleStartOptions.
9. startAllowedModules calls module.start only when policy allows.
```

Current limitation:

```text
ModuleComposer sorts selected dependencies before dependents, and ModuleHost preserves add order when starting modules.
ModuleIngressRegistry registration is tied to start and stop; detach-specific cleanup will be revisited when dynamic unload lands.
Module diagnostics still start from module-provided strings, but ModuleHost and SessionSnapshot now expose structured state snapshots.
```

Current implementation vs target contract:

```text
Implemented: ModuleRuntime carries SessionContext, NetworkRouter, and ChannelRegistry.
Implemented: ModuleManifest carries ModuleVersion and peer module version compatibility declarations.
Implemented: ModuleStartOptions can carry peer module version facts into module.start.
Implemented: ModuleHost has a startAllowedModules(options) path for future manifest exchange results and rejects incompatible peer module versions before module.start.
Implemented: ModuleHost performs all-or-nothing preflight when ModuleStartOptions.allowPartialStart is false, so FDMI version mismatch blocks the profile before any module start side effects.
Implemented: ModuleCompatibilityCheck validates peer module id and version range without parsing module payload schema.
Implemented: display.screen, input.mouse, and input.keyboard role-specific manifests declare peer module compatibility ranges.
Implemented: ModuleInventoryService exchanges FDMI module declaration inventories over the control channel and exposes remote module versions for ModuleHost start options.
Implemented: ModuleInventoryRuntimeService owns caller-side FDMI requests, response subscriptions, RequestTracker completion, timeout expiry, and snapshots.
Implemented: SessionMainline can pass ModuleStartOptions into ModuleHost so FDMI peerVersions can gate module start without SessionMainline parsing FDMI payloads.
Implemented: PC shell can run FDMI after profile module mount and before module start through --module-inventory-service and --module-inventory-request.
Implemented: Session stores remote module inventory in RemoteModuleInventorySnapshot, SessionRuntimeDiagnosticsSnapshot exposes it, and PC shell session diagnostics can print remote module rows.
Implemented: ModuleComposer and RuntimeHost enforce ProductProfile module version constraints before module creation/mount.
Implemented: ModuleHost gates role, platform, dependency presence, channel readiness, and policy.
Implemented: ModuleIngressRegistry can register manifest consumes routes and unregister tokens.
Implemented: ModuleHost activates ingress after start and unregisters ingress before stop.
Implemented: ModuleCatalog exposes display.screen.agent/client and role-filtered suite selection.
Implemented: ModuleFactory and ModuleComposer resolve profile module ids through registered factories.
Implemented: ModuleComposer topologically orders selected module dependencies and reports cycles.
Implemented: ModuleHost preserves add order for start/stop/export/snapshots.
Implemented: ModuleHost can notify optional reconnect-aware running modules in stop-order for pause and start-order for resume, filtered by affected channel bindings.
Implemented: ModuleHost can replay running module ingress routes during reconnect, filtered by affected consumed channel bindings.
Implemented: SessionSnapshot carries a structured lastReconnect report with channel, ingress replay, and module reconnect outcomes.
Implemented: DisplayModuleFactory resolves display.screen into role-specific display agent/client modules.
Implemented: RuntimeHost can mount display ProductProfile modules through ModuleComposer.
Implemented: FeatureModuleFactory and FeatureModule provide lifecycle-only skeletons for audio, microphone, filesystem, printer, keyboard, mouse, touch, gamepad, and camera module manifests.
Implemented: InputModuleFactory resolves input.mouse/input.keyboard to role-specific client/agent modules with FDIN payload contracts.
Implemented: Clipboard code is removed from the active FusionDesk slice and deferred to a later data-redirection rebuild.
Implemented: RuntimeHost registers display, input, then generic feature factories in the ProductProfile composition path, so feature-specific factories override lifecycle skeletons when available.
Implemented: RuntimeHost can pass input adapter dependencies into InputModuleFactory.
Implemented: WindowsInputInjector and QtInputCapture provide first adapter seams for input.
Implemented: PC shell product profile startup can select input feature modules, inject first input adapter dependencies, and assert role-scoped mounts.
Implemented: FeatureRuntimeService provides the first pure C++ runtime owner for input capture polling through module APIs.
Implemented: FeatureRuntimePolicy, policy/audit counters, and optional service-owned input capture lifecycle with idempotent start and destructor cleanup provide the production feature policy foundation without Qt or OS dependencies.
Implemented: SessionCreateOptions can register ProductProfile minimum channel specs into NetworkManager before module mount.
Implemented: ModuleHost exposes ModuleSnapshot values and SessionSnapshot carries moduleSnapshots.
Implemented: test.protocol client/agent smoke covers version declaration, peer compatibility, ModuleHost startup, and module-owned Exchange Request/Response payload handling.
Pending: concrete global/raw input capture adapters, future clipboard/data-redirection rebuild, enterprise content policy mapping, and UI/service startup wiring beyond the pure runtime feature policy foundation.
Pending: production UI/service handling of inventory mismatch diagnostics.
Pending: running-state dependency checks for dynamic enable/disable flows.
```

ModuleCatalog and composition minimum contract:

```text
ModuleCatalog discovers manifests by product capability and session role.
Product profile may request the capability alias display.screen.
Catalog resolves display.screen to display.screen.agent for AgentSession.
Catalog resolves display.screen to display.screen.client for ClientSession.
Implemented role-specific catalogs and factories return manifests with module version and peer compatibility declarations.
ModuleFactory creates the role-specific module implementation.
ModuleComposition records selected modules/manifests, missing factories or create failures, and dependency failures.
ModuleComposition must keep module version checks at the module id/version range level. It must not encode FDIN/FDCL/raw-frame payload schema rules as composition dependencies.
RuntimeHost records mount results as mounted, denied by ModuleHost, missing, or dependency-failed.
```

ModuleIngress lifecycle rule:

```text
attach does not activate packet ingress.
Routes are active only for modules that successfully start.
Registration failure after start stops the module and reports failure.
stop unregisters ingress before module state is released.
detach must leave no active subscription tokens.
Reconnect replays active subscriptions for running modules only.
```

## Startup Module Set

The first product profile should contain only:

```text
session.base
network.base
policy.base
diagnostics.base
display.screen
```

Add input only after display first-frame, keyframe recovery, and diagnostics are working.
