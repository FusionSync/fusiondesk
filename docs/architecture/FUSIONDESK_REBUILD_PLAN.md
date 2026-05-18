# FusionDesk Rebuild Plan

The legacy `Source/` tree is a reference, not the implementation source of truth.

`FusionDesk` becomes the architecture source of truth.

This document is a coarse rebuild map. The active execution order is tracked in `GOAL_AUTOPILOT_PLAN.md`; when the two differ, the goal plan and implementation baseline are authoritative for the next step.

Current execution overlay:

```text
G1 Protocol Core: current core slice complete.
G2 Network Core: current core slice complete.
G3 Session Runtime: current core slice complete.
G4 Module System: in progress; role/platform/dependency/channel/policy gates exist.
G5 Display MVP: current next slice after role-scoped display manifests and ingress lifecycle landed.
```

## Rebuild Principle

```text
Do not copy old modules.
Do not wrap old classes as the target design.
Read old behavior when needed.
Design the FusionDesk contract.
Implement the new module against the FusionDesk contract.
Use better designs when the old design is coupled, platform-specific, or outdated.
```

## Stage 0: Reference Legacy Behavior

Allowed use of legacy `Source/`:

```text
behavior archaeology
protocol and packet field confirmation
platform edge case confirmation
performance baseline comparison
regression scenario discovery
```

Forbidden:

```text
new FusionDesk runtime dependency on Source private classes
new FusionDesk module wrapping a Source module as the implementation
new app-controller access from FusionDesk modules
new direct transport-manager access from FusionDesk modules
```

## Stage 1: FusionDesk Core Contracts

Deliver:

```text
network packet envelope
network channel interface
network router
module ingress registry
module manifest
module lifecycle interface
module host
policy model
policy engine
```

Exit criteria:

```text
network/module/policy compile as a standalone library
no dependency on app private headers
no dependency on third-party code
old feature ids and transfer types are represented where protocol compatibility requires them
module manifests can be registered into network subscriptions without concrete transport access
```

## Stage 2: Pilot New Modules

Enter this stage only after G4 module composition has enough lifecycle closure for the target module.

Implementation order:

```text
display.screen
input.mouse
input.keyboard
clipboard
filesystem
printer
camera control
audio control
gamepad
peripheral bridge
```

Why this order:

```text
display.screen validates the enterprise product's core value path and the video channel
input.mouse and input.keyboard validate interactive control after first-frame rendering
clipboard validates shared large-data routing
filesystem validates multi-packet shared routing
printer validates optional module routing
camera and audio touch realtime paths but can start with control plane
gamepad validates a dedicated feature channel
peripheral bridge remains process-out and should attach through a proxy module
```

Legacy code may be consulted during each module, but the deliverable is new `FusionDesk` implementation code.

## Stage 3: RuntimeHost

The basic RuntimeHost and SessionManager already exist in the current core slice. The remaining Stage 3 work is product-profile-driven module composition, not initial class creation.

Move product assembly out of UI and controller code:

```text
Apps parse args and initialize process services.
RuntimeHost creates sessions and module composition.
ModuleHost starts and stops modules.
PolicyEngine gates every module.
NetworkRouter owns ingress and egress.
```

Exit criteria:

```text
App code does not directly construct feature modules.
Controller code does not expose network managers to modules for new paths.
Product profile determines enabled modules.
```

## Stage 4: Transport Implementations

Implement new transport classes behind `IChannel`.

Initial transports:

```text
standard TCP channel
video TCP channel
audio TCP channel
UDP message path
user-auth channel
```

Future transports:

```text
relay channel
P2P tunnel channel
enterprise forced-relay channel
```

## Stage 5: Build and Delivery

Primary:

```text
Windows build and package
Linux build and package
```

Optional:

```text
macOS build and package
```

Exit criteria:

```text
CI uploads Windows artifacts.
CI uploads Linux artifacts.
Release tags publish Windows and Linux assets.
FusionDesk library is built before rebuilt apps.
```
