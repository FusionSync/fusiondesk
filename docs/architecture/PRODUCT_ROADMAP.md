# Product Roadmap

FusionDesk is being repositioned as an enterprise remote desktop capability platform.

The completed feature modules are product assets. The missing work is the architecture loop that turns them into controllable, composable, sellable, and diagnosable capabilities.

## Product Positioning

```text
Enterprise remote desktop capability platform
```

Not a single remote-control tool.

The product should support:

```text
remote desktop
audio and microphone redirection
clipboard redirection
filesystem redirection
printer redirection
camera redirection
keyboard, mouse, touch, and gamepad input
USB and peripheral redirection
enterprise policy and license gating
direct, relay, and future hole-punch tunnel connectivity
Windows and Linux primary delivery
macOS optional delivery
```

## Phase 1: Stable Enterprise Remote Desktop

Goal:

```text
Make the existing function set run through one session architecture.
```

Required outcomes:

```text
Windows package is produced by CI.
Linux package is produced by CI.
Display and input remain stable.
Clipboard, filesystem, printer, camera, audio, and peripheral features can be enabled or disabled independently.
Module failure does not kill the whole session.
Session diagnostics can explain which module failed and why.
```

## Phase 2: Sellable Module Platform

Goal:

```text
Convert features into product modules.
```

Required outcomes:

```text
Each module has a manifest.
Each module has a SKU and feature id.
Policy can allow or deny a module per customer, device, user, and session.
Products are built from module bundles.
Standalone module packages are possible for selected features.
```

Example bundles:

```text
remote-desktop-suite
clipboard-only
filesystem-only
peripheral-suite
support-agent
enterprise-agent
```

## Phase 3: Connection Enhancement

Goal:

```text
Add direct connection and fallback routing without changing feature modules.
```

Required order:

```text
LAN/direct TCP
relay fallback
UDP hole punching
TCP fallback
policy-controlled enterprise relay mode
```

Feature modules must not know whether the current session is using LAN, relay, or P2P tunnel.

## Phase 4: Enterprise Operations

Goal:

```text
Make the product deployable and governable in real enterprise environments.
```

Required outcomes:

```text
device inventory
online state
session history
audit logs
file transfer logs
module diagnostics
policy distribution
silent install
upgrade channel
crash report collection
remote log pull
```

