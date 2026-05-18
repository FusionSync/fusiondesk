# Network Channel Registry And Scheduler

This document expands `NETWORK_MODEL.md` into concrete contracts.

## Network Components

```text
NetworkManager
  -> SocketGroup
  -> ChannelRegistry
  -> PriorityScheduler
  -> NetworkRouter
  -> ReconnectManager
```

Responsibilities:

```text
NetworkManager owns session network lifecycle.
SocketGroup owns concrete transport sockets.
ChannelRegistry owns channel metadata and readiness.
PriorityScheduler owns outbound queues and pressure state.
NetworkRouter owns packet ingress and feature subscriptions.
ReconnectManager owns channel rebind after socket failure.
```

## PacketEnvelope V1

All new packets should route through this envelope:

```cpp
struct PacketEnvelope {
    std::uint16_t protocolMajor;
    std::uint16_t protocolMinor;
    SessionId sessionId;
    TraceId traceId;
    MessageId messageId;
    MessageId correlationId;
    MessageId responseTo;
    ChannelId channelId;
    ChannelType channelType;
    PacketType packetType;
    MessageKind messageKind;
    PacketPriority priority;
    ResponseStatus responseStatus;
    std::uint64_t sequence;
    std::uint64_t monotonicTimestampUsec;
    std::uint32_t timeoutMs;
    PacketFlags flags;
    ByteBuffer payload;
};
```

Compatibility parsers translate existing wire frames:

```text
TRANSFER_DATA_HEADER
  -> PacketEnvelope.channelId
  -> PacketEnvelope.packetType
  -> PacketEnvelope.payload
```

The parser may preserve the old payload unchanged while new modules are being implemented. This does not permit wrapping old `Source` transport or module classes as the target implementation.

Request/response correlation is handled above raw routing by the request tracker described in `PROTOCOL_MESSAGE_PATTERN.md`.

## ChannelSpec

```cpp
struct ChannelSpec {
    ChannelKey key;
    std::string name;
    SocketClass socketClass;
    PacketPriority defaultPriority;
    ReliabilityMode reliability;
    OrderingMode ordering;
    FlowControlMode flowControl;
    QueuePolicy queuePolicy;
    std::vector<PacketType> allowlist;
    std::string ownerModuleId;
    bool required;
};
```

Required MVP channels:

```text
control
small_data
main_screen
```

Optional early channels:

```text
input
audio
microphone
large_data
second_screen
```

Current implementation note:

```text
small_data is the first shared reliable feature channel for input events.
large_data is available as defaultLargeDataChannelSpec for redirection startup.
large_data is still added explicitly by product profiles rather than the display MVP minimum channel set; clipboard is not part of the current active slice.
```

## ChannelRegistry

Responsibilities:

```text
register logical channels
bind channel to socket
track ready, degraded, closed, failed states
expose pressure state
check required channel readiness before module start
```

Current core status:

```text
ChannelAllowlistValidator exists as the negotiated allowlist boundary.
It checks negotiated channel types, packet types, and message kinds before routing.
ChannelRegistry exists and owns channel specs, channel ids, readiness,
pressure, degraded state, and binding.
PriorityScheduler exists and orders packets by priority.
NetworkManager exists as a small composition root for registry, scheduler,
and router in core tests.
NetworkManager can mark one channel degraded, rebind it to a replacement
channel, and preserve NetworkRouter subscriptions across that rebind in tests.
ITransportSocket and SocketGroup exist as pure core transport contracts.
SocketGroup can register, open, write, close, and snapshot fake transport
sockets by SocketClass in tests. NetworkManager can write packet bytes through
SocketGroup based on ChannelSpec.socketClass, and tests prove realtime socket
failure does not block control socket writes.
QtTcpTransportSocket exists under adapters/qt as the first concrete transport
adapter and detaches adopted QTcpSocket instances from their Qt parent before
taking ownership. QtPacketChannel implements IChannel on top of
QtTcpTransportSocket and PacketCodec. QtChannelBinder registers the Qt transport
into SocketGroup, registers or reuses ChannelSpec, binds QtPacketChannel into
NetworkManager, and marks ChannelRegistry ready. QtSessionTransportConnector
creates client-side Qt TCP transports from profile entries and adopts accepted
server-side QTcpSocket instances before driving QtChannelBinder.
QtRuntimeTransportManager owns QtSessionTransportConnector instances and
QTcpServer listeners per RuntimeHost sessionId. runtime/qt can load Qt TCP
transport profile JSON files and validate tcpChannels/tcpListenChannels against
known ChannelSpec entries. QtTcpPeerProfileCoordinator can generate validated
local multi-endpoint client-connect and agent-listen profile files from known
ChannelSpec values and requested channel plans without starting sockets or marking channels
ready. fusiondesk_pc_profile_plan exposes that local profile planning path for
startup scripts. PC shell startup can bind no-sessionId profiles to
the RuntimeHost session it just created. Listener startup reports
listeningChannels; only connected/adopted transports mark ChannelRegistry ready. QtRuntimeTransportManager can prepare reconnect replacement channels from Qt TCP connect profiles or accepted sockets without marking them ready, and can also call Session reconnect after preparation; Session reconnect performs the actual rebind and ready transition. After one-call reconnect succeeds, runtime/qt closes and removes the previous active Qt transport for that channel. The optional
fusiondesk_qt_tcp_transport_tests target proves a PacketEnvelope
Request/Response can be encoded by PacketCodec, sent over Qt TCP, received,
decoded back into PacketEnvelope, and routed through
NetworkManager/SocketGroup/ChannelRegistry/NetworkRouter.
The same target also proves a RuntimeHost agent session can listen, accept an
inbound TCP channel, adopt it into NetworkManager, mark ChannelRegistry ready,
and that RuntimeHost-mounted display agent/client modules can render the first
frame and complete keyframe request/response through JSON-loaded
QtRuntimeTransportManager profile data.
It also proves Qt TCP replacement channels can be prepared by runtime/qt and
then rebound only through Session::reconnect, including the one-call runtime/qt
reconnect wrapper and old active Qt transport teardown.

Remote peer profile exchange does not change ChannelRegistry readiness.
`PeerProfileService` exchanges channel plans and resolved profile pairs over the
control route, but only accepted or connected concrete transports mark channels
ready through the existing binder and Session reconnect path.
Reconnect diagnostics also stay above ChannelRegistry. `ReconnectDiagnosticsReport`
summarizes plan, replacement, Session rebind, teardown, pending, and timeout
state for runtime/UI consumers, but ChannelRegistry remains responsible only for
logical channel readiness, degraded state, pressure, and binding.
`LinkChannelBindingReport` now sits above ChannelRegistry under runtime/session.
It combines ChannelRegistry snapshots, module-required ChannelBinding values,
and runtime/Qt listening-channel hints into one startup report. The report does
not write `Blocked` back into ChannelRegistry; `blocked` is a SessionMainline
gate derived from `moduleRequired && !ready`, with `listening`, `bound`,
`ready`, `degraded`, `closed`, and `failed` kept as separate facts.
PC shell uses this report for `--wait-channels-ms` before `SessionMainline`
starts ProductProfile modules.
`SessionRuntimeDiagnosticsSnapshot` wraps the same LinkChannelBindingReport
with SessionSnapshot, module counts, and session diagnostics for product
UI/service readers. This keeps UI diagnostics above ChannelRegistry instead of
walking the registry directly.
PC shell `--print-session-diagnostics` currently exposes that aggregate as the
minimal product-reader surface; production UI should consume the same runtime
reader rather than ChannelRegistry internals.
```

Module boundary:

```text
ChannelRegistry exposes required channel readiness to ModuleHost.
ChannelRegistry does not authorize modules.
ChannelRegistry does not start modules.
NetworkRouter stores active packet subscriptions.
ModuleIngressRegistry decides which running-module subscriptions should exist.
Reconnect replay means replay active subscriptions only; it is not a permanent registration bypass.
```

Design contract:

```cpp
class IChannelRegistry {
public:
    virtual ~IChannelRegistry() = default;
    virtual Result registerSpec(const ChannelSpec& spec) = 0;
    virtual Result bind(ChannelKey key, std::shared_ptr<IChannel> channel) = 0;
    virtual void unbind(ChannelKey key, const CloseReason& reason) = 0;
    virtual void markReady(ChannelKey key, const ChannelReadyInfo& ready) = 0;
    virtual void markDegraded(ChannelKey key, const DegradedReason& reason) = 0;
    virtual void updatePressure(ChannelKey key, ChannelPressure pressure) = 0;
    virtual bool isReady(ChannelKey key) const = 0;
    virtual ChannelSnapshot snapshot(ChannelKey key) const = 0;
    virtual std::vector<ChannelSnapshot> snapshots() const = 0;
};
```

## PriorityScheduler

Priority classes:

```text
Critical = 0
Realtime = 1
Interactive = 2
Normal = 3
Bulk = 4
Background = 5
```

Design contract:

```cpp
class IPriorityScheduler {
public:
    virtual ~IPriorityScheduler() = default;
    virtual EnqueueResult enqueue(const PacketEnvelope& packet, const SendOptions& options) = 0;
    virtual std::optional<PacketEnvelope> next(SocketClass socketClass) = 0;
    virtual void acknowledge(ChannelKey key, std::uint64_t sequence) = 0;
    virtual void drop(ChannelKey key, DropPolicy policy) = 0;
    virtual ChannelPressure pressure(ChannelKey key) const = 0;
    virtual SchedulerSnapshot snapshot() const = 0;
};
```

Scheduling rules:

```text
Critical bypasses normal queue limits.
Realtime video keeps keyframes and may drop stale delta frames.
Interactive packets stay ordered and low latency.
Normal packets are reliable ordered.
Bulk packets are reliable, chunked, and throttled.
Background packets only send when higher classes are healthy.
```

## SendOptions

```cpp
struct SendOptions {
    PacketPriority priority;
    ReliabilityMode reliability;
    OrderingMode ordering;
    DropPolicy dropPolicy;
    std::uint32_t maxQueueDepth;
    std::uint32_t timeoutMs;
    bool allowCoalesce;
    bool requireChannelReady;
};
```

Default mapping:

```text
VIDEO -> Realtime, BestEffortLatest, keep keyframes
PAYLOAD_ACK Request/Response -> Interactive, ReliableOrdered
MOUSE -> Interactive, ReliableOrdered, coalesce moves
KEYBOARD -> Interactive, ReliableOrdered, no coalesce
CLIPBOARD metadata -> Normal, ReliableOrdered
FILESYSTEM data -> Bulk, ReliableOrdered, chunked
PRINTER data -> Bulk, ReliableOrdered, chunked
HEARTBEAT -> Critical, ReliableOrdered
CONTROL -> Critical or Normal based on subtype
```

## Pressure States

```text
Healthy
BuildingQueue
Congested
Draining
Closed
Failed
```

Display behavior:

```text
Healthy: target FPS
BuildingQueue: reduce delta rate
Congested: keep latest frame, preserve keyframe
Draining: pause capture or send low FPS keyframes
Closed: stop sender
Failed: enter reconnect flow
```

Bulk behavior:

```text
Healthy: normal chunk window
BuildingQueue: reduce window
Congested: pause new chunks
Draining: resume after higher priority clears
Closed or Failed: fail or retry request according to module policy
```

## Reconnect Rebind

Reconnect is a channel operation:

```text
socket failed
  -> mark affected channels degraded
  -> keep unaffected channels running
  -> create replacement socket
  -> send channel init
  -> verify session id and channel key
  -> rebind ChannelRegistry
  -> replay NetworkRouter subscriptions
  -> notify modules
```

Module callback:

```cpp
class IChannelStateListener {
public:
    virtual ~IChannelStateListener() = default;
    virtual void onChannelReady(ChannelKey key) = 0;
    virtual void onChannelDegraded(ChannelKey key, DegradedReason reason) = 0;
    virtual void onChannelRebound(ChannelKey key) = 0;
};
```

Display reaction:

```text
main_screen rebound
  -> client decoder reset if needed
  -> client sends request keyframe
  -> agent sends keyframe
  -> rendering resumes
```

## Verification Tests

Required tests:

```text
register required channel and mark ready
register control, small_data, and main_screen default channels
deny module start when required channel not ready
route incoming VIDEO to display client
route incoming PAYLOAD_ACK to display agent
route PAYLOAD_ACK Response back to display client
prove the VIDEO/PAYLOAD_ACK route in fusiondesk_display_mvp_tests
prove display delta drop and keyframe preservation under congested pressure
schedule Critical before Realtime and Bulk
drop stale video delta when congested
preserve keyframe when congested
coalesce mouse move packets
pause bulk when realtime channel is congested
enqueue through NetworkManager only after channel is ready
flush Critical before Bulk through NetworkManager
rebind channel and replay subscriptions
keep control ready when main_screen is degraded and rebound
register, open, write, and close fake transport sockets through SocketGroup
write packet bytes through NetworkManager to the ChannelSpec socket class
```

## Reconnect Plan And Teardown Rules

Service-backed reconnect planning is now represented in pure C++ under `runtime/connection`. ChannelRegistry still only sees logical channel state.

Rules:

```text
replacement channels are prepared by runtime/adapters but are not marked ready before Session::reconnect
the old logical binding remains identifiable until a replacement rebind succeeds
Session::reconnect is the only code path that swaps the logical ChannelRegistry binding
NetworkRouter subscriptions are replayed after the logical rebind
adapter/runtime owns concrete old transport close and removal after successful rebind
core session records rebind and replay results, not Qt/socket-specific close results
QtReconnectOrchestrator may start replacement listeners and client replacement connects, but it must still enter logical rebind through Session::reconnect
Qt runtime transport iteration uses snapshots because polling can process Qt events and mutate active transport storage during reconnect
ReconnectRuntimeServiceSnapshot::diagnostics aggregates coordinator, Session, teardown, pending, and timeout state; UI code should consume this aggregate instead of walking ChannelRegistry internals
TunnelReconnectExecutor may choose LAN, relay, or direct P2P replacement candidates, but the backend must still hand rebind-ready channels to Session rather than mutating ChannelRegistry directly
```

Required evidence for this area:

```text
reconnect plan rejects empty, unknown, duplicate, or unplanned degraded channels
reconnect plan emits client and agent side replacement intent for affected channels
QtReconnectOrchestrator starts and drives the local Qt TCP replacement form of the reconnect plan
Session reconnect preserves unaffected channel readiness
module ingress replay does not duplicate delivery
failed replacement preparation does not enter Session::reconnect
failed rebind is visible in SessionSnapshot.lastReconnect
old concrete transport teardown is covered by runtime/adapter tests
combined reconnect lifecycle reporting is covered by fusiondesk_reconnect_diagnostics_report_tests
tunnel replacement executor contract, candidate profile selection, and factory request boundary with local/peer candidates are covered by fusiondesk_tunnel_reconnect_executor_tests
```
