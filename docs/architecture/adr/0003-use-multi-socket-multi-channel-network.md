# ADR 0003: Use Multi-Socket Multi-Channel Network

## Status

Accepted

## Context

Remote desktop traffic mixes critical control, realtime video, audio, interactive input, and bulk redirection. A single undifferentiated queue causes stale video frames, input latency, and data redirection interference.

The future tunnel/P2P layer must not change feature modules.

## Decision

Use a multi-socket, multi-channel network model:

```text
control socket
video socket
audio socket
input socket
bulk socket
optional UDP or future tunnel socket
```

Every logical stream is a channel with:

```text
channel id
channel type
socket class
priority
reliability
ordering
queue policy
packet allowlist
owner module
```

Feature modules send and receive only through `NetworkRouter`.

## Consequences

Positive:

```text
control remains responsive under video pressure
bulk transfer cannot block input
display can drop stale deltas while preserving keyframes
future tunnel implementation stays below channel contracts
```

Negative:

```text
more channel state to test
more reconnect cases
transport adapters need channel binding logic
```

## Verification

```text
ChannelRegistry tests for readiness and degraded states
PriorityScheduler tests for critical, realtime, interactive, normal, bulk ordering
NetworkRouter tests for channel id + channel type + packet type routing
display tests for VIDEO and PAYLOAD_ACK directions
```

