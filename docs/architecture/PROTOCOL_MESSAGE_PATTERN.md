# Protocol Message Pattern

The old message style was mostly fire-and-forget. `FusionDesk` must use a fixed protocol pattern with explicit request, response, acknowledgement, error, correlation, timeout, and stream semantics.

This design lightly borrows ideas from RDP:

```text
layered transport and logical channels
capability negotiation before feature traffic
channel-based routing
strict packet length and type validation
state messages for connection and activation
feature traffic carried above a common envelope
```

`FusionDesk` does not copy RDP. It keeps our product protocol, but makes the envelope and message lifecycle strict.

## Goals

```text
every request can be correlated with a response
every error has a standard shape
every long operation can report progress or timeout
every stream chunk belongs to a request or stream id
every module uses the same envelope
every packet is routed by session, channel, type, and message metadata
```

## Envelope V1

Every routed packet uses `PacketEnvelope`:

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

Field rules:

```text
messageId is unique within a session direction.
correlationId groups a workflow. For a simple request, it equals the request messageId.
responseTo is zero for original messages and set to the original request messageId for response, ack, error, or progress messages.
stream messages must carry correlationId; module stream contracts set responseTo when the stream is attached to a request.
traceId follows the whole user or system operation across modules.
sequence is ordered per channel and direction.
timeoutMs is meaningful for requests and long-running stream operations.
```

## Message Kinds

```text
Event: notification; no response required by default
Request: asks peer to do something; response is required unless NoResponseRequired is set
Response: successful business response to a request
Ack: confirms receipt, acceptance, or state transition; not final business result unless documented
Error: failed response to a request or failed processing of an event
Progress: interim status for a long-running request
StreamStart: starts a chunked payload stream
StreamChunk: carries a chunk of a stream
StreamEnd: finishes a stream and reports final stream status
Cancel: asks peer to cancel a running request or stream
```

## Response Status

All responses and errors use a common status:

```text
Ok
Accepted
Progress
InvalidArgument
Unauthorized
DeniedByPolicy
Unsupported
NotFound
Conflict
Busy
Timeout
Cancelled
TooLarge
BackPressure
ChannelUnavailable
Failed
InternalError
ProtocolError
```

Business-specific details belong in payload, but the envelope status must still be set.

## Required Response Rules

Default:

```text
Request -> Response or Error
StreamStart -> Ack or Error
StreamChunk -> Ack only when reliable chunk ACK is enabled
StreamEnd -> Response or Error
Cancel -> Ack or Error
Event -> no response unless ResponseRequired flag is set
```

Forbidden:

```text
Request without timeout
Request without messageId
Response without responseTo
Error without responseTo when caused by a request
stream chunk without correlationId
business failure encoded only as text payload while envelope status remains Ok
```

## Ack Is Not Response

`Ack` means the peer received or accepted a message. It does not mean the business operation succeeded.

Example:

```text
filesystem.openFile Request
  -> Ack Accepted
  -> Progress
  -> Response Ok with handle

display.requestKeyframe Request
  -> Ack Accepted
  -> Response Ok after keyframe is scheduled or sent

reconnect.teardownOldTransport Request
  -> Ack Accepted
  -> Response Ok after the old concrete transport is closed
  -> Error if the remote side cannot close or identify the target channel
```

Current `FusionDesk` reconnect teardown wire shape:

```text
Envelope:
  packetType: Control
  messageKind: Request
  channel: control path, normally userauth_main/control
  flags: ResponseRequired
  messageId/correlationId/timeoutMs: required

Payload:
  magic: FDRT
  version: 1
  targetChannelId
  targetChannelType
  reason
```

The target channel being closed is payload data. It is not the envelope
routing channel, because the old target channel may already be degraded or
rebound. The peer may send `Ack Accepted` to confirm receipt, but teardown is
complete only after a terminal `Response Ok` or `Error`.

The current helper can synthesize a non-zero response `messageId` when tests do
not provide one, but production dispatch must use the session direction's
normal message-id allocator before the response enters RequestTracker,
diagnostics, or wire IO.

The current `ReconnectTeardownDispatcher` can send the request through
`NetworkRouter`, track pending state through `RequestTracker`, record `Ack` as
interim state, and complete only on terminal `Response` or `Error`.
The current `ReconnectTeardownHandler` can receive the peer-side FDRT request,
call an `IReconnectTeardownCloseTarget`, and return terminal Response/Error.
`ReconnectTeardownService` is the current runtime execution boundary: it owns
the response subscriptions, routes Ack/Progress/Response/Error/StreamEnd into
the dispatcher, and exposes timeout expiry through `RequestTracker`.
QtRuntimeTransportManager currently implements that close-target interface for
Qt TCP sessions, so the protocol boundary can be exercised without leaking Qt
types into the common reconnect command/handler code.

For low-latency modules, the feature may define a compressed lifecycle:

Current `FusionDesk` peer profile exchange wire shape:

```text
Envelope:
  packetType: Control
  messageKind: Request
  channel: control path, normally userauth_main/control
  flags: ResponseRequired
  messageId/correlationId/timeoutMs: required

Payload:
  magic: FDPP
  version: 1
  kind: request or response
  clientSessionId
  agentSessionId
  known ChannelSpec list
  requested connection channels or resolved profile pair
```

`PeerProfileService` uses terminal `Response Ok` with an `FDPP` response
payload when the plan resolves, and terminal `Error` when the request is
malformed or cannot be resolved. It subscribes to the same control route as
other control services, so it first checks the `FDPP` magic and ignores
unrelated payloads such as `FDRT`. FDPP is not JSON; JSON profile files remain
local `runtime/qt` PC startup and smoke-test tooling.

Current `FusionDesk` module inventory exchange wire shape:

```text
Envelope:
  packetType: Exchange
  messageKind: Request
  channel: control path, normally userauth_main/control
  flags: ResponseRequired
  messageId/correlationId/timeoutMs: required

Payload:
  magic: FDMI
  version: 1
  kind: request or response
  sessionId
  module manifest subset list:
    moduleId
    module version
    feature
    roleFlags
    runModeFlags
    requiredModules
    compatible peer module id and version ranges
    required/shared channel bindings and packet type lists
```

`ModuleInventoryService` returns the local manifest inventory as terminal
`Response Ok`. The inventory is declaration data for the mainline. It is not a
place to interpret FDIN, FDCL, display frame, or other module payload schemas.
Callers can convert the remote inventory to `ModulePeerVersion` values and pass
them into `ModuleHost::startAllowedModules(options)`, where the target module's
own manifest is used to recompute compatibility before `module.start`.
`ModuleInventoryRuntimeService` is the current caller-side owner: it dispatches
FDMI requests, tracks terminal Response/Error packets through RequestTracker,
expires timeouts, and snapshots completions without moving FDMI parsing into
SessionMainline or PC app code. PC startup can run it after profile modules are
mounted and before SessionMainline starts the modules. After a successful FDMI
exchange, the shell can persist the remote declaration set into the Session
remote inventory snapshot for runtime diagnostics without making Session parse
FDMI payload bytes.

For low-latency modules, the feature may define a compressed lifecycle:

```text
input.mouse Event NoResponseRequired
display.video Event NoResponseRequired
display.payloadAck Request ResponseRequired
```

Current feature payload schemas:

```text
FDIN v1: input.mouse/input.keyboard payloads.
  Header carries magic, version, kind, sequence, timestamp, and modifiers.
  Mouse body carries action, button, coordinate space, x/y, and wheel deltas.
  Keyboard body carries action, native scan code, virtual key, and Unicode codepoint.

FDCL v1: clipboard.redirect payloads.
  FormatList is Event NoResponseRequired.
  ReadRequest is Request ResponseRequired.
  Content is terminal Response for a read request.
  Error is terminal Error with responseTo/correlationId when an endpoint cannot serve the request.
```

## Protocol Phases

The session protocol uses explicit phases:

```text
Hello
CapabilityExchange
Authenticate
PolicySync
ChannelInit
ModuleMount
Running
Reconnect
Drain
Close
```

Phase messages use the same envelope.

Example startup:

```text
Client Hello Request
  -> Agent/Auth Hello Response

Server Capabilities Event or Request
  -> Client Capabilities Response

Login Request
  -> Login Response or Error

ChannelInit Request
  -> ChannelInit Response

ModuleMount Request
  -> ModuleMount Response
```

## Capability Negotiation

Capability negotiation happens before feature traffic.

Current core status:

```text
FeatureMask and FeatureSet define feature bits.
ProtocolCapabilities describes local and remote protocol support.
CapabilityPayloadCodec serializes capability payloads.
CapabilityNegotiator computes negotiated capabilities and denial reasons.
ProtocolLimits captures payload, stream chunk, timeout, channel, and pending request limits.
CapabilityExchange creates request/response envelopes that carry capability payloads.
Runtime and session still own when the exchange is sent and how negotiation results are stored.
```

Capability data includes:

```text
protocol version
supported channel types
supported packet types
supported modules
supported codecs
supported render backends
supported input devices
supported security modes
maximum payload sizes
stream chunk limits
request timeout policy
optional direct tunnel support
```

Module data in capability or module-mount exchange is limited to identity and version facts:

```text
module id
module version
compatible peer module id and version range
opaque module compatibility mode
required channel bindings
policy-visible feature flags
```

The generic capability negotiator can carry and store those facts, but it must not interpret module payload schemas or operation-level compatibility. That decision belongs to the module.

Session stores:

```text
local capabilities
remote capabilities
negotiated capabilities
denied capabilities with reasons
```

Modules receive only negotiated capabilities.

## Standard Payload Header

Payload can be module-specific, but the first bytes of a structured payload should follow the module schema version:

```text
module id or numeric module kind
payload schema version
operation code
operation flags
payload body
```

Large payloads must use stream messages rather than one huge request.

## Module Payload Compatibility Rule

`PacketEnvelope.protocolMajor` and `protocolMinor` describe the common product envelope. They are not a module payload schema version.

Every structured module payload owns its own header:

```text
module payload magic
payload schema major/minor
operation code
operation flags
payload body
```

Module payload compatibility is resolved inside the module:

```text
schema major/minor compatibility
operation availability
downgrade or translation mode
unsupported-operation rejection
malformed-payload diagnostics
request failure response shape
```

When a request-like module payload is rejected, the module returns `Error` or terminal `Response` with the original `responseTo`, `correlationId`, and a non-Ok `ResponseStatus` such as `Unsupported`, `InvalidArgument`, or `ProtocolError`. Event payloads that cannot be safely handled are dropped with diagnostics unless the envelope asked for a response.

## Request Examples

Display keyframe:

```text
Client -> Agent
kind: Request
packetType: PayloadAck
messageId: 1001
correlationId: 1001
responseTo: 0
timeoutMs: 1000
payload: requestKeyframe(reason=decoderReset)

Agent -> Client
kind: Response
packetType: PayloadAck
messageId: 2001
correlationId: 1001
responseTo: 1001
status: Ok
payload: keyframeScheduled(frameId=77)
```

Filesystem read:

```text
Client -> Agent
kind: Request
packetType: FilesystemIrp
messageId: 3100
correlationId: 3100
timeoutMs: 30000
payload: read(pathHandle, offset, length)

Agent -> Client
kind: StreamStart
responseTo: 3100
correlationId: 3100

Agent -> Client
kind: StreamChunk
responseTo: 3100
correlationId: 3100

Agent -> Client
kind: StreamEnd
responseTo: 3100
correlationId: 3100
status: Ok
```

Mouse move:

```text
Client -> Agent
kind: Event
packetType: Mouse
flags: NoResponseRequired | Coalescable
payload: mouseMove(x, y)
```

## Request Tracker

Network/runtime should provide a request tracker above `NetworkRouter`.

Responsibilities:

```text
assign message ids
track pending requests
match responseTo to original messageId
enforce timeout
surface errors to module callbacks
cancel pending requests on channel close
publish diagnostics
```

Suggested contract:

```cpp
class IRequestTracker {
public:
    virtual ~IRequestTracker() = default;
    virtual MessageId nextMessageId() = 0;
    virtual TrackResult track(const PacketEnvelope& request, ResponseHandler handler) = 0;
    virtual bool complete(const PacketEnvelope& response) = 0;
    virtual std::size_t cancelByChannel(ChannelId channelId, ChannelType channelType, ResponseStatus status) = 0;
    virtual std::size_t expire(std::uint64_t nowUsec) = 0;
};
```

`NetworkRouter` routes packets. `IRequestTracker` owns response matching and timeouts.

## Wire Format V1

`FusionDesk` serializes `PacketEnvelope` through `PacketCodec`.

Wire rules:

```text
wire magic is FD2P.
numeric fields are encoded in network byte order, big endian.
wire header length is fixed for protocol v1.
payload length is explicit and must match the byte stream length.
protocol major version must match before payload routing.
header CRC protects fixed envelope metadata.
payload CRC protects payload bytes.
decoded packets are passed through ProtocolValidator before routing.
```

V1 header layout:

```text
offset  size  field
0       4     magic
4       2     headerLength
6       2     protocolMajor
8       2     protocolMinor
10      2     channelId
12      2     channelType
14      2     packetType
16      2     messageKind
18      1     priority
19      1     reserved0, must be zero in v1
20      2     responseStatus
22      2     reserved1, must be zero in v1
24      4     flags
28      8     sessionId
36      8     traceId
44      8     messageId
52      8     correlationId
60      8     responseTo
68      8     sequence
76      8     monotonicTimestampUsec
84      4     timeoutMs
88      4     payloadLength
92      4     headerCrc32
96      4     payloadCrc32
```

Reserved bytes are zero-filled by `PacketCodec` and covered by `headerCrc32`.

TCP stream framing:

```text
PacketCodec can inspect the fixed v1 header to determine the complete frame size before decode.
inspectFrame returns incomplete until headerLength + payloadLength bytes are available.
QtPacketChannel uses this inspection to split TCP byte streams into PacketEnvelope frames before NetworkRouter ingress.
```

The first implementation keeps the codec in pure C++ core because it only
serializes the product envelope. Media compression, compatibility codecs,
encryption, Qt socket framing, and tunnel framing stay outside core adapters.

## Validation Rules

Every inbound packet must be validated before routing:

```text
supported protocol version
known session id
valid channel id and channel type
known packet type
valid message kind
payload length within negotiated limits
responseTo exists for response, ack, error, and progress messages
stream messages carry correlationId; request-attached stream responseTo is enforced by module stream contracts
packet type is allowed on the channel
message kind is allowed for the packet type
```

`ProtocolValidator` intentionally does not validate module payload magic, schema, operation, or field compatibility. It validates the common envelope only. Module-specific codecs and validators must run after routing and before business behavior.

Invalid packet behavior:

```text
ProtocolError response when the peer can be safely answered
drop packet when unsafe
close channel or session on repeated or security-sensitive violations
publish diagnostics
```

`FusionDesk` implements the generic envelope validation in `ProtocolValidator`.

Responsibilities:

```text
validate protocol version
validate session id presence when required
validate channel id, channel type, packet type, message kind, priority, and status values
validate payload size limit
validate request fields: messageId, correlationId, timeoutMs
validate response fields: responseTo
validate response-required events
reject mutually exclusive response flags
```

`ProtocolValidator` is intentionally generic. It accepts `maxPayloadBytes`
through validation options, and `CapabilityExchange::validationOptions` derives
those options from negotiated capabilities. Channel allowlists and policy checks
are applied by `ChannelRegistry`, `PolicyEngine`, and module-specific payload
validators.

## First Stage Gate

P0 protocol is ready when:

```text
PacketEnvelope v1 exists in core.
MessageKind, PacketPriority, ResponseStatus, PacketFlags exist in core.
Request/response correlation rules are documented.
NetworkRouter can route by packet type and optionally message kind.
RequestTracker design is documented.
ProtocolValidator validates generic envelope semantics before routing.
PacketCodec serializes and decodes PacketEnvelope with fixed magic, big-endian fields, payload length, and CRC validation.
CapabilityPayloadCodec and CapabilityNegotiator exist for capability payload roundtrip, negotiated protocol limits, and denial reasons.
CapabilityExchange creates standard request/response envelopes for capability payloads and derives validator/codec options from negotiation.
Display keyframe request uses request/response semantics.
Bulk modules are required to use stream semantics.
```
