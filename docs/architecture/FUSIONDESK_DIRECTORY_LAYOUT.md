# FusionDesk Directory Layout

This is the canonical directory layout for the new architecture.

The layout follows the earlier layering rules:

```text
apps -> runtime -> core
runtime -> modules
runtime -> adapters
modules -> core interfaces
adapters -> framework, transport, codec, or platform implementation
platform -> OS services
bindings -> external language or package surfaces
```

## Root Layout

```text
FusionDesk/
  CMakeLists.txt
  README.md
  cmake/
  include/
    fusiondesk/
      core/
      runtime/
      modules/
      platform/
      adapters/
      bindings/
  src/
    core/
    runtime/
    modules/
    platform/
    adapters/
    bindings/
  apps/
    pc/
    android/
  tests/
```

## Core

Core is pure C++ and must not depend on Qt.

```text
include/fusiondesk/core/
  protocol/
  session/
  network/
  module/
  policy/
  diagnostics/

src/core/
  protocol/
  session/
  network/
  module/
  policy/
  diagnostics/
```

Current core code is placed here:

```text
protocol: packet types, channel ids, feature flags, packet envelope
session: session context and lifecycle types
network: channel interface, router, priority model
module: manifest, lifecycle, host, ingress registry, catalog
policy: policy types and policy engine
```

## Runtime

Runtime composes sessions, modules, policy, and network.

```text
include/fusiondesk/runtime/
  host/
  session/
  product/
  diagnostics/
  qt/

src/runtime/
  host/
  session/
  product/
  diagnostics/
  qt/
```

Expected responsibilities:

```text
RuntimeHost
SessionManager
ProductProfile
ModuleComposition
QtEventLoopBridge
QtRuntimeBootstrap
```

## Modules

Modules own feature behavior.

```text
include/fusiondesk/modules/
  display/
  input/
  audio/
  clipboard/
  filesystem/
  printer/
  camera/
  peripheral/

src/modules/
  display/
    common/
    agent/
    client/
    platform/
      windows/
      linux/
      macos/
      android/
  input/
  audio/
  clipboard/
  filesystem/
  printer/
  camera/
  peripheral/
```

The first implemented module is:

```text
modules/display
```

Display owns server capture and client render behavior. It uses core network interfaces and does not own sockets.

## Platform

Platform owns OS services.

```text
include/fusiondesk/platform/
  common/
  windows/
  linux/
  macos/
  android/

src/platform/
  common/
  windows/
  linux/
  macos/
  android/
```

Examples:

```text
window handles
render surface handles
screen enumeration
device permissions
file system special paths
Android activity and surface lifecycle helpers
```

## Adapters

Adapters connect `FusionDesk` to concrete frameworks and external technologies. They are not a plan to run old `Source` modules inside the new architecture.

```text
include/fusiondesk/adapters/
  qt/
  transport/
  display/
  codec/

src/adapters/
  qt/
  transport/
  display/
  codec/
```

Qt adapters may include Qt. Core cannot.

Transport adapters may include concrete socket, relay, or tunnel implementation details. Core cannot.

Codec adapters may include codec library headers. Core cannot.

## Bindings

Bindings expose `FusionDesk` to external language or package surfaces.

```text
include/fusiondesk/bindings/
  android/

src/bindings/
  android/
    jni/
    facade/
```

Android bindings expose Java/Kotlin APIs and load native runtime libraries.

## Apps

Apps are thin process shells.

```text
apps/pc/
  client/
  agent/
  auth/
  tools/

apps/android/
  client_qt/
  controller_library/
```

PC app shells:

```text
parse args
initialize logging
initialize Qt app
create RuntimeHost
load product profile
enter event loop
```

Android app shells:

```text
create Qt Android app or library context
load controller library
attach Android surface
delegate session control to runtime
```

## Tests

```text
tests/
  core/
    network/
    module/
    policy/
    session/
  modules/
    display/
  android/
```

Test priority:

```text
NetworkRouter routing
PriorityScheduler behavior
PolicyEngine decisions
ModuleIngressRegistry registration
Display first-frame flow
Android controller lifecycle facade
```

## Legacy Reference Rule

Old code does not move into `FusionDesk` as the implementation. It may be read to design new code and verify behavior.

New code belongs only in these paths:

```text
modules/<feature>
platform/<os>
adapters/qt
adapters/transport
adapters/codec
bindings/android
```

No old app/controller private dependency may enter core.

No old module private dependency may enter new feature modules unless explicitly approved as a short-lived spike and removed before the stage gate.

## Forbidden Compatibility Roots

Do not place new code in these roots:

```text
include/fusiondesk/module
include/fusiondesk/network
include/fusiondesk/policy
include/fusiondesk/protocol
include/fusiondesk/session
src/module
src/network
src/policy
```

The canonical root is `include/fusiondesk/core/...` for core code. Runtime, modules, platform, adapters, and bindings use their explicit top-level directories. If forwarding headers are ever needed for compatibility, they must be documented and kept as thin forwarding wrappers only.
