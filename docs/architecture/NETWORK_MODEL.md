# Network Model

The `FusionDesk` network module uses a multi-socket, multi-channel design. The goal is to keep realtime display traffic, control messages, audio, input, and bulk redirection from blocking each other.

Feature modules never own sockets. They send and receive packets through `NetworkRouter`.

## Core Principle

```text
Socket transports own bytes.
Channels own ordering and priority within a socket.
NetworkRouter owns packet routing.
Modules own feature behavior.
Session owns lifecycle and policy.
```

## Topology

```text
SessionNetwork
  -> SocketGroup
    -> ControlSocket
    -> VideoSocket
    -> AudioSocket
    -> BulkSocket
    -> InputSocket
    -> OptionalUdpSocket
  -> ChannelRegistry
  -> PriorityScheduler
  -> NetworkRouter
```

Initial socket groups:

```text
control socket: login, channel init, heartbeat, policy, feature toggle, audit control
video socket: screen video and graphical patches
audio socket: desktop audio and microphone
input socket: keyboard, mouse, touch, gamepad
bulk socket: clipboard, filesystem, printer
udp socket: optional realtime acceleration and future tunnel probes
```

The old single-channel constants remain compatibility references. `FusionDesk` may map multiple logical channels onto separate sockets or reuse one socket with independent queues when deployment requires it.

## Channel Model

Every logical stream is a channel:

```text
channel id
channel type
socket class
priority
reliability mode
ordering mode
flow-control mode
packet type allowlist
module owner
```

Initial channels:

```text
userauth_main
small_data
main_screen
second_screen
audio
microphone
large_data
camera
filesystem
printer
gamepad
```

Current `small_data` default allowlist includes PayloadAck, Mouse, Keyboard,
Touchscreen, Gamepad, FilesystemControl, Printer, and Control. Clipboard was
removed from the active FusionDesk slice; future data-redirection modules can add
`defaultLargeDataChannelSpec` when they require a bulk channel, and must still
bind and mark that channel ready before ModuleHost start.

Channel types:

```text
standard
video
audio
bulk
control
input
```

## Priority Classes

Priority is explicit and enforced by `PriorityScheduler`.

```text
Critical = 0
Realtime = 1
Interactive = 2
Normal = 3
Bulk = 4
Background = 5
```

Mapping:

```text
Critical: channel init, heartbeat, disconnect, reconnect, policy revoke
Realtime: display video, cursor update, audio playback, microphone
Interactive: keyboard, mouse, touch, gamepad, display control/recovery PAYLOAD_ACK
Normal: clipboard metadata, camera control, feature toggle, audit notification
Bulk: filesystem data, printer spool, clipboard file payload
Background: diagnostics upload, logs, future update metadata
```

Scheduling rules:

```text
Critical always bypasses queue limits.
Realtime may drop stale frames instead of blocking.
Interactive must stay low latency and ordered per channel.
Normal is reliable and ordered.
Bulk is reliable but chunked and throttled.
Background only sends when no higher-priority pressure exists.
```

## Queue Policy

Each channel owns a bounded queue.

```text
Critical: small bounded queue, no drop except session teardown
Realtime video: latest-frame or latest-keyframe policy
Realtime audio: jitter buffer, drop late packets
Interactive: small queue, no stale mouse move accumulation
Normal: bounded reliable queue
Bulk: chunk queue with backpressure
Background: best-effort queue
```

Display-specific queue policy:

```text
Keep keyframes.
Drop stale delta frames when the video socket is congested.
Request a keyframe after frame loss, reconnect, decoder reset, or render backend reset.
Cursor packets can bypass video frame backlog.
```

## Reliability and Ordering

Modes:

```text
ReliableOrdered
ReliableUnordered
BestEffortLatest
BestEffortOrdered
```

Default mapping:

```text
control: ReliableOrdered
display video: BestEffortLatest over TCP queue semantics, with keyframe recovery
cursor: ReliableOrdered or BestEffortLatest depending on packet type
input: ReliableOrdered
audio: BestEffortOrdered with jitter handling
bulk: ReliableOrdered
```

Even when TCP is used, the router must apply application-level queue policy. TCP reliability alone is not enough for realtime UX because stale frames can still cause latency growth.

## Packet Envelope

All routed packets use:

```text
protocol version
session id
trace id
message id
correlation id
response to
channel id
channel type
packet type
message kind
priority
response status
sequence
timestamp
timeout
flags
payload
```

Compatibility parsers can translate existing `TRANSFER_DATA_HEADER` frames into this envelope when protocol compatibility is required. This is packet compatibility, not a runtime dependency on old transport or module classes.

Request/response, ACK, error, stream, and timeout rules are canonical in `PROTOCOL_MESSAGE_PATTERN.md`.

## Channel Init

Each socket performs channel initialization:

```text
open socket
send channel init
verify session id
verify channel id
verify channel type
verify peer capability
bind socket to ChannelRegistry
publish channel ready
```

Required channel readiness for first display MVP:

```text
control socket ready
main_screen video channel ready
small_data control channel ready
```

Optional readiness:

```text
second_screen video channel
input channel
audio channel
bulk channel
```

## Flow Control

The network layer exposes channel pressure:

```text
healthy
building queue
congested
draining
closed
failed
```

Display server behavior:

```text
healthy: capture at target fps
building queue: reduce delta frame rate
congested: drop stale deltas and keep latest frame
draining: pause capture or send low-fps keyframes
closed: stop sender
failed: notify session for reconnect
```

Bulk behavior:

```text
chunk payload
respect channel window
pause when higher-priority queues are congested
resume when scheduler allows
```

## ACK Model

ACK packets are feature-level signals, not raw socket acknowledgements.

ACK is not a final business response. Requests still need a `Response` or `Error` unless the feature contract explicitly marks the message as `NoResponseRequired`.

Display ACK types:

```text
timestamp ACK
request keyframe
open render device
close render device
decoder reset
```

Network ACKs:

```text
channel init ACK
heartbeat ACK
reconnect bind ACK
optional chunk ACK for bulk transfer
```

The network layer must not interpret video codec payloads. It only routes ACK packet types and preserves priority.

Session and module boundary:

```text
Network exposes readiness, pressure, degraded state, rebind, and subscription replay.
Network does not decide module policy.
Network does not decide whether a feature module should start or stop.
Network does not keep ingress active for stopped modules.
Session and ModuleHost decide lifecycle; ModuleIngressRegistry owns active subscription tokens.
```

## Reconnect

Reconnect is channel-based, not module-based.

```text
detect socket failure
mark affected channels degraded
keep unaffected sockets running
create replacement socket
re-init affected channels
rebind ChannelRegistry
replay active module ingress subscriptions for running modules
notify modules
```

For display:

```text
video channel reconnect -> request keyframe -> reset client decoder if needed -> resume rendering
```

## Future Tunnel Boundary

Tunnels plug in below socket creation:

```text
SocketFactory
  -> LanTcpSocket
  -> RelaySocket
  -> P2PTunnelSocket
```

Channel, priority, policy, and module contracts stay unchanged.

Current tunnel contract slice:

```text
ReconnectCoordinator
  -> IReconnectReplacementExecutor
  -> TunnelReconnectExecutor
  -> ITunnelReplacementBackend
  -> TunnelCandidateProfile negotiation
  -> ITunnelTransportFactory
  -> future LAN TCP, relay, or direct P2P channel factory
  -> Session::reconnect remains the only logical rebind point
```

`TunnelReconnectExecutor` maps the existing service reconnect side plan into
client connect candidates or agent listener candidates while preserving
session id, degraded channels, reason, keyframe intent, and post-rebind
teardown intent. It does not perform NAT traversal, create sockets, or mark
ChannelRegistry ready.

`TunnelCandidateProfile` is the first pure runtime/tunnel candidate selection
contract. It matches client connect candidates and agent listener candidates by
ChannelKey, mode, encryption requirement, and fallback policy. `ITunnelTransportFactory`
is the concrete adapter boundary below that selection result; factory requests
can carry both the local candidate and selected peer candidate, so LAN TCP,
relay, and direct P2P factories can implement transport-specific setup later
without changing Session, NetworkRouter, ChannelRegistry, or display modules.

Current Qt transport slice:

```text
QtTcpTransportSocket implements ITransportSocket under adapters/qt.
It can connect to a TCP endpoint, adopt an accepted QTcpSocket, detach adopted sockets from their QTcpServer parent before ownership transfer, write bytes, and deliver received bytes to an adapter callback.
QtPacketChannel implements IChannel on top of QtTcpTransportSocket and PacketCodec.
QtChannelBinder registers Qt transports into SocketGroup, binds QtPacketChannel into NetworkManager, and marks ChannelRegistry ready.
QtSessionTransportConnector creates client-side Qt TCP transports from profile entries and adopts accepted server-side QTcpSocket instances.
QtRuntimeTransportManager owns QtSessionTransportConnector instances and QTcpServer listeners per RuntimeHost sessionId.
QtSessionTransportConnector can prepare reconnect replacement channels from client-connect profiles or accepted QTcpSocket instances without binding or marking them ready. QtRuntimeTransportManager can either expose those prepared replacements or drive Session reconnect in one call; Session reconnect remains the only path that rebinds replacements into ChannelRegistry. After a successful one-call reconnect, runtime/qt closes and removes the previous active Qt transport for that channel.
QtRuntimeTransportManager can also load a reconnect profile after initial startup and trigger reconnect from the same JSON channel shape, using the loaded TCP channel list as a reconnect request instead of a new startup bind.
If a Qt listener accepts a second connection for a channel that is already bound, runtime/qt treats it as reconnect adoption rather than a fresh startup bind.
runtime/qt can generate matching client-connect and agent-listen profile pairs, serialize and save Qt TCP transport profile JSON files with tcpChannels and tcpListenChannels, validate channel entries against known ChannelSpec entries, and apply connect/listen profiles through manager calls.
runtime/connection owns the pure C++ peer connection plan resolver: it maps requested ChannelKey entries to known ChannelSpec values and rejects duplicate channels/endpoints before concrete transports parse endpoint syntax. runtime/connection now also owns `PeerProfileService`, a control-channel `PacketEnvelope` Request/Response service that carries `FDPP` profile-exchange payloads without JSON, Qt, or sockets. `PeerProfileRuntimeService` owns the caller side of that exchange: it dispatches requests through `NetworkRouter`, subscribes for response kinds, tracks completion through `RequestTracker`, expires timeouts, and exposes snapshots for PC/Qt orchestration. `QtPeerProfileRuntimeService` bridges those FDPP results to `QtRuntimeTransportManager`: agent tcpListenChannels can be applied before the response is sent, and client tcpChannels can be applied from completed responses. QtTcpPeerProfileCoordinator lives under runtime/qt and turns a local multi-endpoint TCP connection plan into validated client-connect and agent-listen profile files; it does not start sockets, negotiate P2P, or mark channels ready. `fusiondesk_pc_profile_plan` exposes this as a startup-script CLI from named MVP channels to paired profile JSON. The old `channelKeys + endpoint` form remains a single-channel shorthand.
General-purpose profile loading requires explicit sessionId; single-session PC shell startup can bind the same profile shape without sessionId to its newly created session.
Listen results expose listeningChannels; ChannelRegistry ready state is published only after an accepted or connected transport is bound.
QtTimerBridge can drive RequestTracker timeout expiry from QTimer without Qt entering core.
QtEventLoopBridge can post callbacks and process events through QCoreApplication without Qt entering core.
PC client and agent shells can create RuntimeHost, start role-specific sessions, and optionally apply --transport-profile or --listen-profile.
PC shell --start-display waits for required module channels before ModuleHost start; --wait-channels-ms controls the wait budget.
PC shell --run-ms supports bounded event-loop execution for smoke and two-peer startup tests.
PC shell --require-display-frame verifies display sent/rendered counters after bounded startup runs.
NetworkRouter::submitIncoming uses a snapshot of matching handlers so reconnect replay or subscription changes during packet handling do not invalidate iteration.
fusiondesk_qt_tcp_transport_tests proves PacketCodec-encoded PacketEnvelope Request/Response traffic over loopback Qt TCP.
fusiondesk_qt_tcp_transport_tests also proves NetworkManager/SocketGroup/ChannelRegistry/NetworkRouter routing through QtChannelBinder and QtPacketChannel.
fusiondesk_qt_tcp_transport_tests proves profile-driven Qt transport connect/adopt through QtSessionTransportConnector.
fusiondesk_qt_tcp_transport_tests proves an agent-side QtRuntimeTransportManager listener can accept a client TCP connection, adopt it into the session NetworkManager, mark a channel ready, and route generic PayloadAck traffic through that bound channel.
fusiondesk_qt_tcp_transport_tests proves coordinator-generated multi-endpoint profile files can start distinct control and video listeners/connectors and mark both ChannelRegistry entries ready.
fusiondesk_qt_tcp_transport_tests proves QtRuntimeTransportManager can prepare reconnect replacement channels from Qt TCP profiles and accepted sockets, can drive Session reconnect in one call, closes/removes the old active Qt transport after rebind, exposes the Qt reconnect teardown close-target adapter, and then routes PayloadAck traffic over the replacement channel.
fusiondesk_reconnect_diagnostics_report_tests proves the pure reconnect diagnostics report can combine plan, replacement, Session rebind, teardown pending state, and timeout expiry, including the ReconnectRuntimeService snapshot path.
fusiondesk_tunnel_reconnect_executor_tests proves the tunnel replacement executor contract can plug into ReconnectCoordinator without changing Session, and that tunnel candidate profiles can select a compatible direct candidate pair and build client/agent transport factory requests with local and peer candidates.
fusiondesk_peer_profile_runtime_service_tests proves PeerProfileRuntimeService can dispatch FDPP profile requests, complete a loopback control-channel response through RequestTracker, expire unanswered requests, and snapshot caller-side state.
fusiondesk_qt_peer_profile_runtime_service_tests proves QtPeerProfileRuntimeService can use a bootstrap control channel to exchange FDPP, start agent listening for a requested main_screen channel, apply the client main_screen connect profile, and leave that requested channel ready on both session registries.
fusiondesk_qt_tcp_transport_tests proves RuntimeHost-mounted display agent/client modules can render the first frame and complete keyframe request/response over JSON-loaded QtRuntimeTransportManager profile data.
fusiondesk_pc_peer_profile_start_display_smoke proves the real PC shell can use JSON only for bootstrap control, then invoke QtPeerProfileRuntimeService to negotiate small_data/main_screen over FDPP before display startup.
fusiondesk_reconnect_teardown_service_tests proves service-level reconnect teardown dispatch, response subscription, Ack interim handling, terminal completion, timeout expiry, peer-side handler routing, and ReconnectRuntimeService stop-time cancellation of pending teardown requests.
fusiondesk_qt_timer_bridge_tests proves QTimer-driven request timeout expiry and ReconnectTeardownService timeout expiry.
fusiondesk_qt_event_loop_bridge_tests proves posted callback execution through the Qt event loop bridge.
Thin PC shells can apply --transport-profile and --listen-profile through QtRuntimeTransportManager against the current shell session, negotiate non-bootstrap startup channels through QtPeerProfileRuntimeService, gate --start-display on required channel readiness, route --reconnect-profile through QtReconnectRuntimeService, print SessionRuntimeDiagnosticsSnapshot with --print-session-diagnostics, print ReconnectRuntimeServiceSnapshot::diagnostics with --print-reconnect-diagnostics, and pass both JSON-profile and FDPP-profile two-process first-frame smokes. runtime/session now exposes SessionRuntimeDiagnosticsSnapshot so future product UI/service code can read session/link/module startup state without walking ChannelRegistry or parsing shell stderr. Production PC UI invocation, peer coordination service execution, product diagnostics presentation, and PC UI-driven reconnect startup remain pending.
```

## First Implementation Gate

The network layer is ready for the first display module when:

```text
NetworkRouter routes by channel id, channel type, and packet type.
ChannelRegistry supports at least control, small_data, and main_screen channels.
PriorityScheduler has explicit classes and queue rules.
Video channel exposes pressure state.
PAYLOAD_ACK Request/Response routes client to server and back with Interactive priority.
VIDEO routes from server to client with Realtime priority.
Reconnect can mark video channel degraded and request a keyframe after rebinding.
```
## Peer Profile Exchange Note

runtime/connection also owns a pure C++ peer profile exchange pair builder that turns a resolved plan into client/agent profile pair data without Qt or sockets.
`PeerProfileService` now exposes that exchange as a pure control-channel service. It subscribes above `NetworkRouter`, validates `PacketEnvelope` Request correlation fields, filters unrelated control payloads by `FDPP` magic, resolves the profile pair through the existing planner, and sends terminal Response/Error packets. It exchanges profile plans/results only; it does not create sockets, bind channels, mark ChannelRegistry ready, or replace Session reconnect ownership.
`PeerProfileRuntimeService` owns caller-side exchange execution. It sends FDPP Request envelopes, keeps RequestTracker as the only pending-request source of truth, treats Ack/Progress as interim, records terminal Response/Error results, expires unanswered requests, and exposes snapshots; it still does not create sockets, bind channels, mark ChannelRegistry ready, or replace Session reconnect ownership.
`QtPeerProfileRuntimeService` is the first concrete runtime/qt consumer of those results. It converts PeerProfileConnectChannel and PeerProfileListenChannel values into Qt TCP profiles and delegates all channel binding/listening to QtRuntimeTransportManager, so Session remains the registry/rebind owner and apps remain thin.
## Peer Coordination Note

`runtime/connection` now also has a pure C++ peer coordination planner that combines peer profile exchange with reconnect hints for known channel plans. `fusiondesk_peer_coordination_tests` covers the success path and invalid reconnect-channel rejection.

## Session Rebind Note

`Session::reconnect` can now accept replacement `IChannel` instances that have already been created by a runtime or adapter layer. The session marks affected channels degraded, pauses reconnect-aware modules through `ModuleHost`, calls `NetworkManager::rebindChannel`, and then resumes modules. runtime/qt now has the first concrete creator and one-call Session reconnect driver for local Qt TCP replacement channels, including old active Qt transport close/removal after successful rebind; service-driven negotiation, cross-process teardown, and tunnel/P2P reconnect remain pending.

## Service Reconnect Plan

`runtime/connection` now provides a transport-agnostic reconnect orchestration plan for service-backed reconnect. The plan is above concrete sockets and below product/UI policy. It selects logical replacement channels by `ChannelKey` and carries the same channel shape for local LAN TCP today and relay/direct/P2P later.
`runtime/qt` now has the first executor adapter for this plan through `QtReconnectExecutor`. It implements `IReconnectReplacementExecutor` by wrapping `QtReconnectOrchestrator`, which maps plan channels to Qt TCP profiles, starts reconnect-aware agent listeners, and starts client replacement reconnects through `QtRuntimeTransportManager`.
`ReconnectCoordinator` now provides the high-level pure C++ frame that sequences reconnect planning, runtime replacement executor calls, client reconnect handoff, and teardown dispatch through executor interfaces. This keeps the network/session rebind boundary stable while Qt local TCP and future tunnel/P2P implementations plug in below the coordinator.
`ReconnectRuntimeService` now owns the common runtime service frame: coordinator, request tracker, reconnect teardown service, and teardown executor. `QtReconnectRuntimeService` adds the Qt replacement executor and `QtTimerBridge`, keeping timer and socket specifics under `runtime/qt`.
`ReconnectRuntimeServiceSnapshot::diagnostics` is the runtime-facing reconnect report. It aggregates the pure coordinator result, optional `Session::lastReconnectReport`, pending teardown requests, expired request count, and timeout state without adding Qt, JSON, or socket types to `runtime/connection`. PC shells can now expose that aggregate with `--print-reconnect-diagnostics` at reconnect completion/failure and exit lifecycle phases; product UI should consume the same owner instead of reconstructing reconnect status from lower-level events.
`runtime/tunnel` now provides `TunnelReconnectExecutor` as the future relay/direct/P2P adapter point for the same coordinator. It also has a pure candidate profile selection contract and `ITunnelTransportFactory` boundary for concrete LAN/relay/direct adapters. The current implementation is still a pure contract and fake-backend smoke; real candidate gathering, relay sockets, direct P2P sockets, NAT traversal, and enterprise selection policy are still pending.

`QtSessionTransportConnector::transports()` returns a snapshot. This is intentional: a transport `poll()` may process Qt events, accept a reconnect listener socket, and cause the connector to close/remove an old active transport. Callers must iterate snapshots instead of live connector storage.

`runtime/connection` also defines the post-rebind old-transport teardown command contract, its first wire shape, a pure dispatch boundary, a peer-side handler boundary, and ReconnectTeardownService as the service execution boundary. The request uses a normal `PacketEnvelope` with `PacketType::Control`, `MessageKind::Request`, `ResponseRequired`, and the available control channel as the envelope route. The target channel to close is carried inside the `FDRT` payload data, not in the envelope route. ReconnectTeardownService can send the request through `NetworkRouter`, subscribe for `Ack`, `Progress`, `Response`, `Error`, and `StreamEnd`, track pending state through `RequestTracker`, keep `Ack Accepted` as interim state, expire timed-out requests through RequestTracker, and summarize only terminal `Response` or `Error` results. ReconnectTeardownHandler subscribes to the control route, ignores unrelated control payloads, rejects malformed FDRT requests, calls `IReconnectTeardownCloseTarget`, and sends terminal Response/Error. QtRuntimeTransportManager implements the current Qt TCP close-target adapter, including idempotent handling after the local reconnect commit path has already closed the old transport. QtReconnectRuntimeService can start timer-driven teardown timeout expiry without Qt entering runtime/connection. PC shell command-line reconnect now routes delayed `--reconnect-profile` execution through this owner while production PC UI and reconnect lifecycle peer-profile invocation remain pending.

Reconnect ordering:

```text
service chooses affected ChannelKey values
  -> resolve peer replacement profile
  -> runtime/qt starts replacement listener for the agent side
  -> prepare runtime-side replacement channel without marking ChannelRegistry ready
  -> Session::reconnect marks old logical channel degraded
  -> Session::reconnect rebinds the replacement logical channel
  -> ModuleHost replays affected ingress routes
  -> DisplayRuntimeService owns client fresh-frame requests
  -> display agent sends a fresh keyframe plus one follow-up delta when requested
  -> runtime/adapter closes old concrete transport after successful rebind
```

Control channel survival remains a hard rule. A failed video replacement must not imply control-channel failure, and future tunnel fallback must preserve the same ChannelRegistry semantics: modules see the same logical channel whether the replacement came from LAN TCP, relay, or direct P2P.
