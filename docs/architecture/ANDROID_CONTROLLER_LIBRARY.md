# Android Controller Library

The Android controller library is the embeddable Android surface for `FusionDesk`.

It is not the same product as the Qt Android client application. The Qt Android client is a complete APK/AAB. The controller library is an AAR that native Android applications can reference.

## Product Output

```text
fusiondesk_android_controller_aar
```

The AAR contains:

```text
Java/Kotlin facade classes
JNI bridge
native .so libraries under jniLibs
AndroidManifest permission declarations
consumer ProGuard/R8 rules
version metadata
optional native debug symbols package
```

## Public API Shape

The public API is controller-oriented.

```text
FusionDeskController.create(context)
controller.connect(request)
controller.disconnect()
controller.attachSurface(surface)
controller.resizeSurface(width, height)
controller.sendKey(event)
controller.sendPointer(event)
controller.sendTouch(event)
controller.state()
controller.setListener(listener)
controller.close()
```

The public API must not expose:

```text
C++ pointers
JNI method names
Qt private UI classes
native socket classes
module implementation objects
```

## Java/Kotlin Boundary

Recommended package:

```text
com.fusiondesk.controller
```

Facade classes:

```text
FusionDeskController
FusionDeskConnectRequest
FusionDeskSessionState
FusionDeskSurfaceConfig
FusionDeskInputEvent
FusionDeskDiagnosticsEvent
FusionDeskControllerListener
FusionDeskError
```

The Java/Kotlin layer owns:

```text
Android context access
permission checks
Surface lifecycle entry points
callback thread handoff
error mapping
native library loading
```

The Java/Kotlin layer must not implement:

```text
network routing
display decoding
module lifecycle
policy decisions
packet parsing
```

## JNI Boundary

JNI owns parameter conversion and lifecycle handles.

```text
bindings/android/jni
  native_controller_jni.cpp
  native_controller_handle.h
```

JNI responsibilities:

```text
create native controller handle
destroy native controller handle
convert Java request objects to C++ request structs
convert diagnostics events to Java callbacks
attach and detach Android Surface
map C++ status to Java error object
```

JNI must not:

```text
own business logic
parse display payloads
own long-lived Java global references without explicit release
call Java callbacks from arbitrary native threads without dispatch policy
```

## Native Controller Boundary

The native controller lives under runtime or bindings facade implementation, not in core.

```text
runtime/android
  AndroidControllerRuntime
  AndroidControllerSession

bindings/android/facade
  ControllerFacade
```

Native controller responsibilities:

```text
create RuntimeHost
create ClientSession
load Android client product profile
attach render surface
connect network
start display.screen.client
route input events
publish diagnostics
```

## Threading Model

Thread groups:

```text
Android main thread
controller API thread
network IO thread
decode thread
render thread
diagnostics callback thread
```

Rules:

```text
Public Java/Kotlin API is safe to call from Android main thread.
Blocking connect work must move off the Android main thread.
Listener callbacks return on the configured callback executor.
Surface attach/detach must synchronize with render thread.
Native close waits for worker shutdown or enters a bounded async teardown.
```

## Surface Lifecycle

Surface events:

```text
surface created
surface changed
surface destroyed
activity paused
activity resumed
network changed
controller closed
```

Required behavior:

```text
surface destroyed pauses rendering but does not force session disconnect.
surface recreated reattaches render backend and requests keyframe.
activity paused may reduce frame rate or pause render depending on policy.
network changed enters reconnect flow through SessionManager.
```

## Error Model

Error groups:

```text
InvalidState
PermissionDenied
NetworkUnavailable
AuthFailed
PolicyDenied
SurfaceUnavailable
DecoderFailed
RenderFailed
NativeFailure
```

Errors must include:

```text
code
message
recoverable flag
session id when available
module id when available
```

## First AAR Scope

Included:

```text
create and close controller
connect and disconnect session
attach Android Surface
start display.screen.client
send pointer, touch, and key events through placeholder input API
diagnostics callback
arm64-v8a native library package
x86_64 emulator package
```

Deferred:

```text
Android agent
background service mode
filesystem redirection
camera redirection
microphone redirection
MDM policy integration
hardware decoder specialization
```
