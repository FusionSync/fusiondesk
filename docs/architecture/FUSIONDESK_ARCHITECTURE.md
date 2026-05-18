# FusionDesk Architecture

`FusionDesk` is the clean rebuild root. It does not move or wrap legacy code as the target implementation. It defines new contracts first, then new modules are implemented against those contracts.

The old `Source/` tree is reference-only:

```text
study behavior
study protocol constants and packet semantics
study platform edge cases
study product workflows
write new FusionDesk code
```

Do not treat old classes as runtime building blocks for the new architecture.

## Design Rule

```text
Modules own feature behavior.
Network owns packet ingress and egress.
Policy owns permission decisions.
Session owns orchestration.
Apps own process startup only.
```

## Dependency Direction

Allowed:

```text
apps -> runtime
runtime -> session
session -> network
session -> modules
session -> policy
modules -> network interfaces
modules -> policy data types
modules -> protocol data types
network -> protocol data types
policy -> module manifests
```

Forbidden:

```text
network -> concrete modules
policy -> concrete modules
modules -> app private headers
modules -> concrete transport managers
modules -> other module private headers
session -> app private headers
```

## Target Runtime Loop

```text
App
  -> RuntimeHost
    -> SessionManager
      -> Session
        -> PolicyEngine
        -> NetworkRouter
        -> ModuleHost
```

Startup order:

```text
create session context
load product profile
load policy and license grants
create network router
register transport channels
load module manifests
resolve role-specific module variants
evaluate policy
attach modules
start allowed modules through ModuleHost gates
activate module ingress for successfully running modules
publish diagnostics
```

## First Implementation Slice

`FusionDesk` starts with:

```text
include/fusiondesk/core/protocol
include/fusiondesk/core/session
include/fusiondesk/core/network
include/fusiondesk/core/module
include/fusiondesk/core/policy
src/core/protocol
src/core/session
src/core/network
src/core/module
src/core/policy
```

The first slice intentionally avoids UI, codecs, device implementation, and tunnel code. Those come after the contracts are stable.

The full directory layout is canonical in `FUSIONDESK_DIRECTORY_LAYOUT.md`.

Platform target support is canonical in `PLATFORM_TARGETS.md`.

## Network Contract

Detailed network rules are canonical in `NETWORK_MODEL.md`.

All incoming packets use one path:

```text
transport implementation
  -> NetworkRouter::submitIncoming
  -> module subscription callback
```

All outgoing packets use one path:

```text
module
  -> NetworkRouter::send
  -> registered channel
```

Routing key:

```text
channel id
channel type
transfer type
```

Module ingress registration:

```text
ModuleManifest::channels
  -> ModuleIngressRegistry
  -> NetworkRouter subscriptions
  -> module packet handler
```

This preserves the current shared-channel behavior where clipboard, filesystem, and printer can share a large-data channel while still being routed by packet type.

Ingress lifecycle rule:

```text
attach prepares module state only.
ingress routes become active only after the module starts successfully.
stop and detach must unregister ingress routes before module state is released.
reconnect may replay active subscriptions for running modules, but stopped modules must not remain registered.
```

## Module Contract

Every feature has:

```text
ModuleManifest
IModule
ModuleHost registration
ModuleIngressRegistry registration
network ingress bindings
policy metadata
diagnostics state
```

Required manifest fields:

```text
module id
display name
SKU
feature id
role flags
run mode flags
platform support
required modules
optional modules
channel bindings
consumed transfer types
produced transfer types
```

## Policy Contract

Policy answers one question:

```text
Can this module run in this session?
```

The decision must include:

```text
allowed or denied
reason code
human-readable reason
effective feature mask
transport constraints
```

Policy sources:

```text
license grant
customer profile
device profile
user role
session security mode
peer platform
runtime health
```

## Future Tunnel Boundary

P2P and relay support belongs below the network contract:

```text
NetworkRouter
  -> IChannel
    -> LanTcpChannel
    -> RelayChannel
    -> P2PTunnelChannel
```

Feature modules must not change when tunnel support is added.

## First Module

The first `FusionDesk` feature module is `display.screen`.

Detailed server capture and client render rules are canonical in `DISPLAY_MODULE_DESIGN.md`.
