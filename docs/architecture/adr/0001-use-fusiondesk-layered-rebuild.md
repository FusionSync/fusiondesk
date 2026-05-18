# ADR 0001: Use FusionDesk Layered Rebuild

## Status

Accepted

## Context

The legacy `Source/` tree contains valuable remote desktop capabilities, but feature behavior, UI controllers, concrete TCP channels, Qt objects, and platform APIs are tightly coupled.

The project needs an enterprise architecture that can support PC full-platform targets, Android client controller, module-level policy, future tunnel/P2P transport, and staged rebuild.

## Decision

Create `FusionDesk` as a clean rebuild root with this dependency direction:

```text
apps -> runtime -> core
runtime -> modules
runtime -> adapters
modules -> core interfaces
adapters -> framework, transport, codec, or platform implementation
platform -> OS services
bindings -> external package surfaces
```

Legacy code is used as reference material for behavior, protocol details, and platform lessons. New `FusionDesk` modules are implemented against new contracts instead of wrapping old `Source` classes.

## Consequences

Positive:

```text
new contracts can be tested independently
core stays portable
future tunnel work can plug below network
Android binding can stay separate from Qt and core
enterprise policy has a central owner
```

Negative:

```text
early work is slower because contracts must land before feature rebuild
framework, transport, codec, and platform adapters are required
some legacy assumptions will be exposed as rebuild blockers
```

## Verification

```text
FusionDesk core builds as an independent CMake target
FusionDesk core has no Qt includes
FusionDesk core has no Source or ThirdParty includes
feature modules only use core interfaces for network and policy
```
