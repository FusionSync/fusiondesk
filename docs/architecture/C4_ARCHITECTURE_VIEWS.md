# C4 Architecture Views

This document provides lightweight C4-style views for the `FusionDesk` rebuild. Keep this file updated when ownership or runtime boundaries change.

## System Context

```mermaid
flowchart LR
    User[Remote Operator]
    Admin[Enterprise Admin]
    Client[PC or Android Client]
    Agent[Remote Agent]
    Auth[Auth and Policy Service]
    Relay[Relay or Future P2P Tunnel]
    Audit[Audit and Diagnostics Backend]

    User --> Client
    Admin --> Auth
    Client --> Auth
    Client --> Relay
    Relay --> Agent
    Client --> Agent
    Agent --> Audit
    Client --> Audit
    Auth --> Audit
```

System responsibilities:

```text
Client controls remote desktop sessions.
Agent exposes authorized remote control capabilities.
Auth and Policy authorizes users, features, modules, and transport modes.
Relay or P2P Tunnel provides transport when direct LAN is unavailable.
Audit backend receives enterprise diagnostics and data movement events.
```

## Container View

```mermaid
flowchart TB
    subgraph PCClient[PC Client App]
        ClientShell[Thin App Shell]
        ClientRuntime[RuntimeHost]
        ClientSession[ClientSession]
        ClientModules[Client Modules]
    end

    subgraph PCAgent[PC Agent App]
        AgentShell[Thin App Shell]
        AgentRuntime[RuntimeHost]
        AgentSession[AgentSession]
        AgentModules[Agent Modules]
    end

    subgraph Core[FusionDesk Core]
        Protocol[Protocol]
        Network[NetworkRouter]
        ModuleHost[ModuleHost]
        Policy[PolicyEngine]
        Diagnostics[Diagnostics]
    end

    subgraph Platform[Platform and Adapters]
        QtBridge[Qt Runtime Bridge]
        TransportAdapters[Transport and Codec Adapters]
        OSAdapters[Windows Linux macOS Android]
    end

    ClientShell --> ClientRuntime
    ClientRuntime --> ClientSession
    ClientSession --> Network
    ClientSession --> ModuleHost
    ClientSession --> Policy
    ClientModules --> Network
    ClientModules --> QtBridge
    ClientModules --> OSAdapters

    AgentShell --> AgentRuntime
    AgentRuntime --> AgentSession
    AgentSession --> Network
    AgentSession --> ModuleHost
    AgentSession --> Policy
    AgentModules --> Network
    AgentModules --> TransportAdapters
    AgentModules --> OSAdapters

    Network --> Protocol
    ModuleHost --> Diagnostics
    Policy --> Diagnostics
```

Container rules:

```text
apps are thin
runtime composes
core contracts stay pure C++
modules implement features
platform and adapters hold Qt, OS, transport, codec, and external technology dependencies
```

## Component View - Session Runtime

```mermaid
flowchart LR
    RuntimeHost --> ProductProfile
    RuntimeHost --> SessionManager
    SessionManager --> ClientSession
    SessionManager --> AgentSession
    ClientSession --> NetworkManager
    ClientSession --> ModuleHost
    ClientSession --> PolicyEngine
    ClientSession --> DiagnosticsSink
    AgentSession --> NetworkManager
    AgentSession --> ModuleHost
    AgentSession --> PolicyEngine
    AgentSession --> DiagnosticsSink
    ModuleHost --> ModuleIngressRegistry
    ModuleIngressRegistry --> NetworkRouter
    NetworkManager --> ChannelRegistry
    NetworkManager --> PriorityScheduler
    NetworkManager --> NetworkRouter
```

## Component View - Display MVP

```mermaid
sequenceDiagram
    participant Capture as Agent IDisplayCapture
    participant Encoder as Agent IVideoEncoder
    participant RouterA as Agent NetworkRouter
    participant RouterC as Client NetworkRouter
    participant Decoder as Client IVideoDecoder
    participant Renderer as Client IDisplayRenderer

    Capture->>Encoder: CapturedFrame
    Encoder->>RouterA: VIDEO PacketEnvelope
    RouterA->>RouterC: video channel
    RouterC->>Decoder: VIDEO payload
    Decoder->>Renderer: DecodedFrame
    Renderer->>RouterC: first frame rendered
    RouterC->>RouterA: PAYLOAD_ACK Request keyframe
    RouterA->>RouterC: PAYLOAD_ACK Response
    RouterA->>Encoder: requestKeyFrame
    Encoder->>RouterA: keyframe VIDEO PacketEnvelope
```

## Component View - Android Controller Library

```mermaid
flowchart TB
    AndroidApp[Native Android App]
    JavaFacade[FusionDeskController Java or Kotlin Facade]
    JNI[JNI Opaque Handle]
    QtAndroid[Qt Android Runtime Bridge]
    RuntimeHost[RuntimeHost]
    ClientSession[ClientSession]
    DisplayClient[DisplayClientModule]
    Surface[Android Surface]

    AndroidApp --> JavaFacade
    JavaFacade --> JNI
    JNI --> QtAndroid
    QtAndroid --> RuntimeHost
    RuntimeHost --> ClientSession
    ClientSession --> DisplayClient
    AndroidApp --> Surface
    Surface --> JavaFacade
    JavaFacade --> DisplayClient
```

Android rules:

```text
public API exposes Android types only
JNI handle is opaque
Surface lifecycle maps to renderer attach and detach
session lifecycle is not the same as Activity lifecycle
Qt stays behind runtime and binding boundaries
```

## Deployment View

```text
Windows x64
  pc client
  pc agent
  auth or service components

Linux x86_64 and arm64
  pc client
  pc agent
  auth or service components

macOS x86_64 and arm64
  planned client first, agent by capability

Android arm64-v8a
  Qt client
  embeddable controller AAR

Android x86_64
  emulator and CI test ABI
```
