# Qt Boundary

Qt is the main cross-platform framework for apps, platform adapters, event loops, sockets, and rendering. It is not part of the `FusionDesk` pure core contract.

## Rule

```text
core is pure C++17.
Qt appears only in runtime, modules implementation, platform, adapters, bindings internals, and apps.
```

## Qt Allowed

Qt may be used in:

```text
FusionDesk/apps
runtime/qt
modules/<feature>/platform/*
modules/<feature>/client or agent implementation when the feature needs Qt
platform/*
adapters/qt
bindings/android internals
```

Typical Qt usage:

```text
QCoreApplication / QGuiApplication
QObject lifecycle and signals where adapter-owned
QTcpSocket / QUdpSocket adapters
QThread / QTimer adapters
QWindow / QSurface / OpenGL render surfaces
Qt Android platform helpers
QImage or QByteArray inside adapter and module implementation
```

## Qt Forbidden

Qt must not be included by:

```text
include/fusiondesk/core/*
src/core/*
```

Forbidden examples:

```text
QString
QByteArray
QObject
QTcpSocket
QThread
QVariant
QJsonObject
QWindow
QAndroidJniObject
```

Core replacement types:

```text
std::string
std::vector<std::uint8_t>
std::function
std::chrono
std::variant where needed
plain structs
opaque handles
```

## Adapter Strategy

Qt adapters convert between Qt types and core types.

```text
QByteArray <-> protocol::ByteBuffer
QString <-> std::string
QTcpSocket event <-> IChannel send/receive
QSurface/QWindow <-> platform render surface handle
Qt timer <-> runtime scheduler tick
```

Adapters are replaceable. The Android AAR public API must not require an app to use Qt UI classes.

Current implementation status:

```text
fusiondesk_qt_adapters is an optional CMake target.
fusiondesk_qt_runtime is an optional CMake target.
fusiondesk_qt_display_adapters is an optional CMake target when Qt Gui is found.
CMake can use qmake to locate the active Qt installation.
QtTcpTransportSocket lives under adapters/qt.
QtTcpTransportSocket implements the pure-core ITransportSocket byte-write contract.
QtTcpTransportSocket can deliver received bytes through an adapter callback.
QtPacketChannel lives under adapters/qt.
QtPacketChannel implements IChannel on top of QtTcpTransportSocket and PacketCodec.
QtChannelBinder lives under adapters/qt.
QtChannelBinder registers Qt transports into SocketGroup, binds QtPacketChannel, and marks ChannelRegistry ready.
QtSessionTransportConnector lives under runtime/qt.
QtSessionTransportConnector creates client-side Qt TCP transports from profiles and adopts accepted server-side QTcpSocket instances.
QtSessionTransportConnector exposes transport lists as snapshots, not live internal vector references, because Qt event processing during poll can accept reconnects and replace transports.
QtRuntimeTransportManager lives under runtime/qt and owns QtSessionTransportConnector instances plus QTcpServer listeners per session id.
QtRuntimeTransportManager can prepare reconnect replacement channels from Qt TCP connect profiles or accepted sockets without binding or marking ChannelRegistry ready, can call Session::reconnect in one runtime-facing API, and closes/removes the previous active Qt transport after that one-call reconnect succeeds; Session::reconnect still owns the actual rebind.
QtReconnectExecutor lives under runtime/qt and implements the pure IReconnectReplacementExecutor boundary by wrapping QtReconnectOrchestrator, so ReconnectCoordinator can drive the Qt local TCP path without Qt entering runtime/connection.
QtReconnectRuntimeService lives under runtime/qt and composes QtReconnectExecutor, the pure ReconnectRuntimeService owner, and QtTimerBridge. The common owner stays under runtime/connection; QTimer and Qt transport manager wiring stay under runtime/qt.
runtime/qt can load Qt TCP transport profile JSON files, validate tcpChannels and tcpListenChannels against known ChannelSpec entries, and apply them through QtRuntimeTransportManager.
runtime/connection resolves ChannelSpec-backed peer connection plans without Qt before runtime/qt applies TCP endpoint and JSON profile concerns.
QtTcpPeerProfileCoordinator lives under runtime/qt and can generate, validate, and save paired local multi-endpoint client-connect and agent-listen profile files from std::string/std::vector inputs while keeping QJson/QFile details out of core.
fusiondesk_pc_profile_plan lives under apps/pc and exposes that local profile planning path as a startup-script CLI while keeping profile JSON construction outside the PC shell.
PC shell startup can bind a no-sessionId profile to its current RuntimeHost session through the single-session QtRuntimeTransportManager entry points.
QtRuntimeTransportManager separates listeningChannels from readyChannels so a bound QTcpServer is not treated as a ready feature channel.
QtTcpTransportSocket detaches adopted QTcpSocket instances from their Qt parent before taking unique ownership.
QtTimerBridge lives under runtime/qt and wraps QTimer behind a standard C++ callback API.
QtEventLoopBridge lives under runtime/qt and wraps QCoreApplication post/process/runUntil behind a standard C++ callback API.
QtImageDisplayRenderer lives under adapters/qt/display and renders decoded software frames into QImage callbacks without Qt entering core or display module interfaces.
QtInputCapture lives under adapters/qt/input and converts Qt mouse/key events into pure input module events without Qt entering modules/input.
Clipboard Qt adapters are not part of the current FusionDesk slice. Rebuild clipboard later under adapters/qt/clipboard after display.screen and data-redirection ownership are stable.
PC shell startup may compose Qt feature adapters into RuntimeHost profile dependencies, but modules/input, core, and runtime/connection remain Qt-free.
FeatureRuntimeService lives under runtime/feature and remains Qt-free; it only polls IInputCapture and calls input module APIs. PC shell owns the QTimer used by --pump-profile-modules.
FeatureRuntimePolicy also lives under runtime/feature and remains Qt-free.
fusiondesk_pc_client and fusiondesk_pc_agent are thin QCoreApplication shells that create RuntimeHost and role-specific sessions.
fusiondesk_qt_tcp_transport_tests proves PacketEnvelope Request/Response roundtrip over Qt TCP via PacketCodec.
fusiondesk_qt_tcp_transport_tests also proves NetworkManager/SocketGroup/ChannelRegistry/NetworkRouter routing through QtChannelBinder and QtPacketChannel.
fusiondesk_qt_tcp_transport_tests proves QtSessionTransportConnector profile-driven connect/adopt paths.
fusiondesk_qt_tcp_transport_tests proves QtRuntimeTransportManager listener accept/adopt into a session-owned NetworkManager.
fusiondesk_qt_tcp_transport_tests proves coordinator-generated multi-endpoint profile files can drive QtRuntimeTransportManager listen/connect and mark multiple session channels ready.
fusiondesk_qt_tcp_transport_tests proves QtReconnectRuntimeService starts the Qt timer/teardown owner frame and lets ReconnectRuntimeService drive the local Qt TCP reconnect path through ReconnectCoordinator, QtReconnectExecutor, and QtReconnectOrchestrator, while QtRuntimeTransportManager prepares replacement channels, drives Session::reconnect, closes/removes the old active Qt transport, and resumes traffic.
fusiondesk_qt_tcp_transport_tests proves RuntimeHost-mounted display first-frame and keyframe recovery over JSON-loaded QtRuntimeTransportManager profile data.
fusiondesk_peer_connection_plan_tests proves the common peer connection plan resolver without Qt.
fusiondesk_qt_timer_bridge_tests proves QTimer-driven RequestTracker timeout expiry.
fusiondesk_qt_event_loop_bridge_tests proves posted callback execution through QtEventLoopBridge.
fusiondesk_qt_peer_profile_coordinator_tests proves local multi-endpoint profile generation, canonical JSON save/load roundtrip, no-sessionId shell profile shape, and invalid plan failure reporting without creating sockets.
fusiondesk_pc_profile_plan_smoke proves the startup-script CLI writes multi-channel client/agent profile JSON and rejects unknown channel names.
fusiondesk_qt_image_display_renderer_tests proves the Qt image render adapter can render a decoded software frame into a QImage sink.
fusiondesk_qt_input_capture_tests proves Qt input event conversion into MouseInputEvent and KeyboardInputEvent queues.
fusiondesk_pc_feature_adapter_startup_smoke proves PC shell startup can mount input profile modules and inject first feature adapters without moving Qt into modules.
fusiondesk_feature_runtime_service_tests proves the pure runtime feature owner drives input Event traffic through module APIs.
fusiondesk_feature_runtime_service_tests also proves policy denial/audit and service-owned input capture lifecycle without Qt entering runtime/feature or modules.
Production PC connection startup and production Qt/OpenGL/D3D render surface adapters are still pending.
```

## Android Qt Rule

The Android Qt Client may use Qt UI directly.

The Android Controller Library AAR may use Qt internally only if hidden behind Java/Kotlin facade classes.

Public AAR API accepts Android platform types:

```text
Context
Surface
SurfaceView or TextureView owner objects
Executor
Listener interfaces
plain Java/Kotlin data classes
```

Public AAR API must not require:

```text
QGuiApplication
QWindow
QObject
Qt signal-slot knowledge
Qt resource paths
```

## Build Guard

CTest now includes the local source purity check. CI should run it as part of
the normal `ctest` suite:

```text
fusiondesk_source_purity_scan
no Qt includes under include/fusiondesk/core
no Qt includes under src/core
no current Qt includes under include/fusiondesk/modules or src/modules
no Source or ThirdParty references under current core/modules
fusiondesk_core builds without finding Qt
```
## Peer Profile Exchange Note

runtime/connection also builds pure C++ peer profile pair data from a resolved plan before runtime/qt serializes JSON.
