# Linux Clipboard Implementation Plan

This plan translates the current `clipboard.redirect` module, the existing
multi-channel runtime, and `/mnt/e/workspace/git_code/github/fuse-promise` into
a Linux clipboard endpoint design.

It is an implementation plan, not implementation evidence. The source of truth
for completed work remains `FUSIONDESK_IMPLEMENTATION_BASELINE.md` and the
current worktree.

## Objective

Build a Linux clipboard implementation that:

```text
uses the existing clipboard.redirect module and FDCL protocol
keeps Linux/X11/FUSE details behind platform or adapter endpoints
supports lazy format offers and lazy content reads
uses small_data for metadata and short content
uses large_data for file ranges and bulk content
uses fuse-promise for Linux remote file promises instead of eager temp copies
keeps the FusionDesk integration surface in C++17 with no Qt dependency
allows an external Rust clipboard event library behind a C ABI
does not use Qt QClipboard as the Linux production clipboard backend
preserves policy, audit, reconnect, stale-offer, and channel-pressure rules
```

The Linux endpoint must not move platform objects, FUSE handles, or
fuse-promise public headers into core or clipboard modules. Production Linux
clipboard code must not introduce QtCore, QtGui, QtWidgets, QObject, QThread,
QTimer, QString, QByteArray, QSocketNotifier, QClipboard, QMimeData,
QGuiApplication, or Qt platform plugin dependencies.

The external Rust clipboard event library is an external dependency, not
FusionDesk core. It must expose a stable C ABI and must be consumed only by the
Linux platform clipboard wrapper.

This plan scopes Linux production clipboard support to X11/XCB. Wayland and
portal clipboard integration are intentionally out of scope because compositor,
seat, focus, and portal permission rules are a different platform model from
X11 selection ownership.

## Existing Channel Logic

Current channel ownership already supports clipboard redirection:

```text
src/core/network/channel_defaults.cpp
  small_data:
    ChannelIdValue::SmallData
    ChannelType::Standard
    SocketClass::Auxiliary
    Reliable + Ordered + Pressure + Bounded
    allowlist includes PacketType::Clipboard

  large_data:
    ChannelIdValue::LargeData
    ChannelType::Standard
    SocketClass::Bulk
    Reliable + Ordered + Pressure + Bounded
    allowlist includes PacketType::Clipboard
    required = false

  defaultSendOptions:
    Clipboard + Bulk priority gets Bulk queue behavior
    other Clipboard packets get Interactive queue behavior
```

`src/core/module/module_catalog.cpp` declares role-specific clipboard modules:

```text
clipboard.redirect.client
clipboard.redirect.agent

required shared channel:
  small_data consumes/produces PacketType::Clipboard

optional shared channel:
  large_data consumes/produces PacketType::Clipboard
```

`src/runtime/runtime_host.cpp` adds `defaultLargeDataChannelSpec()` when the
product profile requests `clipboard.redirect` or the Clipboard feature. This is
the correct place for channel availability to enter startup. Linux code should
not register channels directly.

The practical implication is:

```text
FormatList, ReadFormatRequest, short ReadFormatResponse, errors, cancel,
LockObject, UnlockObject, and control-like FDCL traffic stay on small_data.

FileRangeResponse and other bulk responses try large_data first when the
channel is registered, ready, allowlisted, and not pressured. Small responses
may fall back to small_data. Large responses fail with ChannelUnavailable or
BackPressure instead of blocking realtime channels.
```

## Existing Clipboard Module Implementation

The reusable module contract is already in place.

Module and transfer interfaces:

```text
include/fusiondesk/modules/clipboard/clipboard_types.h
  TransferSource
  TransferSourceBundle
  TransferFormatDescriptor
  IClipboardEndpoint
  IClipboardRemoteReader
  IRemoteDragCoordinateSink
  MaterializedTransferSource
  RemoteFdclTransferSource

include/fusiondesk/modules/clipboard/clipboard_transfer.h
  ITransferFormatMapper
  ITransferFileContentProvider
  IClipboardRemoteFileReader
  IClipboardRemoteObjectLocker
  IClipboardChangeMonitor
  FileGroupTransferSource
  InMemoryTransferSourceRegistry
```

FDCL protocol ownership:

```text
include/fusiondesk/modules/clipboard/protocol.h
  Capabilities
  FormatList
  ReadFormatRequest
  ReadFormatResponse
  FileRangeRequest
  FileRangeResponse
  LockObject
  UnlockObject
  DragStart/Move/Drop/Cancel
  Cancel
  ErrorDetail
```

Runtime flow:

```text
ClipboardRuntimeService
  polls IClipboardChangeMonitor when present
  snapshots IClipboardEndpoint only when needed
  authorizes/audits local announce
  calls ClipboardModuleBase::announceLocalBundle

ClipboardRuntimeRemoteReader
  turns endpoint delayed-rendering reads into FDCL requests
  pumps transport/event processing until response or timeout
  sends Cancel on timeout
```

Module flow:

```text
ClipboardModuleBase
  owns FDCL encode/decode dispatch
  stores local/remote TransferSourceBundle in a source registry
  validates bundleId/offerId/ownerEpoch/sourceId/formatId before reads
  enforces module policy before announce, receive, and content send
  uses RequestTracker for request/response timeouts
  releases locks and large_data reservations on stop, detach, and reconnect
  republishes only the latest local bundle after reconnect when requested
```

Current platform coverage:

```text
platform/windows/clipboard
  native Win32/OLE endpoint, delayed rendering, owner marker, file contents,
  drag preflight, local-file range provider

platform/macos/clipboard
  AppKit endpoint, pasteboard data provider, file promises

adapters/qt/clipboard
  QClipboard/QMimeData endpoint for text, rich text, image, local file URL
  snapshot, and validation fallback publication

apps/pc/common/pc_clipboard_shell.cpp
  supports endpoint kinds auto, windows, macos, and qt
  has no linux endpoint kind yet
```

Linux should attach through the same `IClipboardEndpoint`,
`IClipboardChangeMonitor`, `IClipboardRemoteReader`,
`IClipboardRemoteFileReader`, and `IClipboardRemoteObjectLocker` seams.
The existing Qt endpoint remains useful for cross-platform smoke tests and
compatibility fallback only; it is too opaque for the Linux production path
because it hides X11 selection request timing, target negotiation, ownership
loss, INCR transfer handling, owner-marker targets, and platform-specific
clipboard event timing.

## Legacy X11 Reference Findings

The restored legacy tree at
`/mnt/e/workspace/GIT_CODE/ASTUTE/Production-HSR2/HSR-Windows/HSR-Windows-restored/Source`
contains the Linux clipboard behavior reference. It must stay external
reference material and must not be copied into FusionDesk.

Relevant legacy files:

```text
HSRCommon/platform/linux/xcbconnection.*
HSRCommon/platform/linux/xcbeventloop.*
HSRCommon/platform/linux/xcbatom.*
HSRCommon/platform/linux/xcbselection.*
HSRCommon/platform/linux/xcbclipboard.*
HSRCommon/platform/linux/xcbmime.*
HSRCommon/platform/linux/cliprdrfilemanager.*
HSRCommon/platform/linux/cliprdrfilesystem.*
HSRClient/PC/Source/Clipboard/x11/cclipboardctrl.*
HSRServer/Source/HSRAgent/Clipboard/x11/cclipboardctrl.*
```

Observed behavior:

```text
XcbConnection owns xcb_connect, atom initialization, XFixes discovery, and a
dedicated xcb_poll_for_event loop.

XcbClipboard specializes XcbSelection for the CLIPBOARD atom and reacts to
XFixes selection-owner changes by requesting TARGETS.

XcbSelection handles SelectionRequest, SelectionNotify, PropertyNotify, TARGETS,
TIMESTAMP, conversion requests, delayed selection rendering, and property writes
followed by SelectionNotify.

XcbSelectionMime owns atom-name to format-name mapping and tracks the offered
target set.

CClipboardCtrl bridges local X11 selection changes to remote format-list
messages and bridges local paste SelectionRequest events to remote format-data
requests.

Remote file offers become local URI-style file lists through a file proxy. File
opens trigger range reads, plus lock/unlock notifications around proxy
lifetime.
```

Target FusionDesk mapping:

```text
legacy FORMAT_LIST_REQUEST
  -> FDCL FormatList Event

legacy FORMAT_DATA_REQUEST/RESPONSE
  -> FDCL ReadFormatRequest/ReadFormatResponse

legacy FILECONTENTS_REQUEST/RESPONSE
  -> FDCL FileRangeRequest/FileRangeResponse

legacy LOCK_CLIPDATA/UNLOCK_CLIPDATA
  -> FDCL LockObject/UnlockObject

legacy temp/FUSE-like file proxy
  -> fuse-promise visible path plus FDCL range reads
```

The reusable idea is the XCB event and selection ownership model, not the old
packet structs, Qt thread wiring, temp path policy, or old file-copy behavior.

## fuse-promise Findings

`fuse-promise` is a Linux user-session Promise filesystem component, not a
clipboard product. It is suitable as the Linux remote-file clipboard path
presentation layer because it publishes file metadata first and serves bytes on
demand.

Important properties from the current repository:

```text
public API:
  include/fuse-promise/fuse-promise.h
  libfusepromise.so
  pkg-config package: fuse-promise

runtime:
  fuse-promised user-session daemon
  default mount: $XDG_RUNTIME_DIR/fuse-promise/
  private daemon IPC is not public API
  Rust implementation stays behind the C ABI

provider model:
  fp_context_open
  fp_provider_register(read callback)
  fp_promise_builder_new
  fp_promise_add_dir
  fp_promise_add_file
  fp_promise_commit -> visible path
  fp_materialize for explicit local materialization

read model:
  FUSE open is read-only
  files are opened with direct I/O
  read callback receives promise_id, provider node_id, relative_path, offset,
  and length
  read responses are bounded by runtime-owned buffer length

security:
  XDG_RUNTIME_DIR must be absolute, owned by the current user, and 0700-style
  private
  paths are normalized relative paths
  absolute paths, parent traversal, NUL bytes, duplicate nodes, and unsafe
  materialize targets are rejected
  provider disconnect marks non-materialized promises unavailable
```

Important integration constraints:

```text
fuse-promise has no public per-promise destroy API today.
fuse-promise has no public open/release callback today.
The provider read callback runs on a libfusepromise helper thread.
The public C ABI is synchronous.
The daemon and private IPC must remain external implementation details.
```

These constraints affect FusionDesk:

```text
Do not call FusionDesk session, transport, or module APIs directly from the
fuse-promise provider callback thread.

Use a bounded thread-safe dispatch bridge from the fuse-promise read callback
to the FusionDesk runtime/event-loop owner. The callback waits for a response
or timeout and returns an fp_status_t.

Until fuse-promise grows open/release callbacks, object locks should be
range-scoped or lease-cached with explicit timeout cleanup. Range-scoped
lock/read/unlock is simpler and safer for the first implementation.

Until fuse-promise grows a destroy/expiry API, replacing a remote file
publication should either unregister the publication provider or leave old
promise paths to fail deterministically through stale FDCL identity checks.
```

## Target Linux Architecture

Add a Linux feature-adapter target:

```text
fusiondesk_platform_linux_feature_adapters
```

Expected files:

```text
include/fusiondesk/platform/linux/clipboard/linux_clipboard_endpoint.h
src/platform/linux/clipboard/linux_clipboard_endpoint.cpp
src/platform/linux/clipboard/linux_clipboard_formats.cpp
src/platform/linux/clipboard/linux_clipboard_local_files.cpp
src/platform/linux/clipboard/linux_clipboard_fuse_promise.h
src/platform/linux/clipboard/linux_clipboard_fuse_promise.cpp
src/platform/linux/clipboard/linux_clipboard_xcb_rust.h
src/platform/linux/clipboard/linux_clipboard_xcb_rust.cpp
tests/platform/linux/feature/linux_clipboard_endpoint_smoke.cpp
tests/platform/linux/feature/linux_clipboard_fuse_promise_smoke.cpp
```

Optional backend split after the endpoint contract is stable:

```text
src/platform/linux/clipboard/linux_clipboard_x11_backend.*
```

Wayland and portal clipboard integration must be designed separately if needed
later; they are not part of the current Linux clipboard endpoint scope and are
not routed through the X11 selection backend.

Initial public class:

```cpp
class LinuxClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IClipboardChangeMonitor {
public:
    ClipboardSnapshot snapshot() override;
    protocol::ResponseStatus publishBundle(
        const ClipboardPublishRequest& request) override;
    protocol::ResponseStatus clearPublishedBundle(
        TransferOfferId offerId) override;

    bool hasPendingClipboardChange() const override;
    void markClipboardChangeConsumed() override;
};
```

Endpoint dependencies should include:

```text
IClipboardRemoteReader
IClipboardRemoteFileReader
IClipboardRemoteObjectLocker
ILinuxClipboardBackend
ILinuxFusePromisePublisher
ILinuxClipboardReadDispatcher
LinuxClipboardEndpointOptions
```

Do not expose XCB, Qt, or fuse-promise types in clipboard module headers.

This target is a non-Qt target. Its public and FusionDesk-facing integration
surface remains C++17. It may use standard C++ concurrency primitives,
POSIX file-descriptor polling, fuse-promise, pthread/system libraries,
FusionDesk pure C++ interfaces, and the external `clipbus` library through C
ABI. It must not link QtCore, QtGui, QtWidgets, `FUSIONDESK_QT_LIBS`, or the
existing `adapters/qt/clipboard` implementation.

## External Rust Clipboard Event Library

Create the X11/XCB clipboard event engine as a separate Rust project and
package it like a third-party library. The name stays generic, but FusionDesk
uses only its X11/XCB backend in this plan. Project name:

```text
clipbus
```

Naming and package surface:

```text
external repo path: /mnt/e/workspace/git_code/clipbus
Rust crate: clipbus
C ABI prefix: clipbus_
C header: clipbus.h
pkg-config package: clipbus
CMake target: Clipbus::Clipbus
library: libclipbus.a or libclipbus.so
```

For its initial X11/XCB backend, `clipbus` owns only native clipboard
mechanics:

```text
xcb connection and hidden owner/requestor window
atom cache and native target names
CLIPBOARD ownership and owner-loss detection
XFixes selection owner notifications
SelectionRequest, SelectionNotify, PropertyNotify
TARGETS, TIMESTAMP, SAVE_TARGETS
owner marker target publication and loop-suppression hints
delayed target rendering state
INCR state machine when that milestone is enabled
```

It must not own FusionDesk product semantics:

```text
FDCL encode/decode
FormatList, ReadFormatRequest, ReadFormatResponse, FileRangeRequest, or
FileRangeResponse
small_data or large_data channel choice
session lifecycle, reconnect, policy, audit, diagnostics ownership
bundleId, offerId, ownerEpoch, or source registry validation
fuse-promise provider lifecycle
remote file LockObject or UnlockObject
```

FusionDesk consumes it through a narrow C ABI:

```c
typedef struct clipbus_clipboard clipbus_clipboard_t;
typedef uint64_t clipbus_request_id_t;

typedef enum clipbus_status {
    CLIPBUS_OK = 0,
    CLIPBUS_PENDING = 1,
    CLIPBUS_UNSUPPORTED = 2,
    CLIPBUS_TIMEOUT = 3,
    CLIPBUS_PLATFORM_ERROR = 4
} clipbus_status_t;

typedef clipbus_status_t (*clipbus_target_request_cb)(
    void* user,
    clipbus_request_id_t request_id,
    const char* native_target,
    uint64_t max_inline_bytes,
    uint64_t timeout_ms);

typedef void (*clipbus_targets_changed_cb)(void* user);
typedef void (*clipbus_owner_lost_cb)(void* user);
typedef void (*clipbus_error_cb)(void* user, int code, const char* message);

clipbus_status_t clipbus_clipboard_create(
    const clipbus_options_t* options,
    const clipbus_callbacks_t* callbacks,
    void* user,
    clipbus_clipboard_t** out);

clipbus_status_t clipbus_clipboard_start(clipbus_clipboard_t* clipboard);
clipbus_status_t clipbus_clipboard_stop(clipbus_clipboard_t* clipboard);
void clipbus_clipboard_destroy(clipbus_clipboard_t* clipboard);

clipbus_status_t clipbus_clipboard_publish_targets(
    clipbus_clipboard_t* clipboard,
    const clipbus_target_offer_t* targets,
    size_t target_count);

clipbus_status_t clipbus_clipboard_complete_request(
    clipbus_clipboard_t* clipboard,
    clipbus_request_id_t request_id,
    clipbus_status_t status,
    const uint8_t* data,
    size_t data_len);
```

The target request callback is a promise-style callback. When a local X11
application requests a target that FusionDesk published lazily, the Rust
library records the pending X11 request and calls `clipbus_target_request_cb`.
The C++ wrapper may return immediate data, deny the request, or return
`CLIPBUS_PENDING`. For pending requests, the C++ wrapper must later call
`clipbus_clipboard_complete_request` before the request deadline. Rust then
writes the property and sends the correct SelectionNotify, or fails the X11
request deterministically on timeout/cancel.

For INCR, Rust owns the native X11 chunking state. C++ still provides canonical
target bytes or an explicitly bounded stream handle through the C ABI; Rust
must not call FDCL or session/network APIs directly.

FFI requirements:

```text
Rust panic must not cross the C ABI.
C++ exceptions must not cross into Rust.
all strings are UTF-8 plus explicit length where practical
all byte buffers use pointer + length + explicit free/complete ownership
callbacks must be bounded and may return Pending instead of blocking forever
callbacks must tolerate stop/reconnect races and stale request ids
```

## Format Mapping

Use the existing canonical format model:

```text
text/plain;charset=utf-8
text/html
text/rtf
image/png
application/x-fdcl-file-list
application/x-fdcl-owner-marker
```

Linux native names:

```text
UTF8_STRING
TEXT
STRING
text/plain
text/plain;charset=utf-8
text/html
text/rtf
application/rtf
image/png
text/uri-list
x-special/gnome-copied-files
x-special/mate-copied-files
application/x-fdcl-owner-marker
```

Policy is evaluated only on canonical format, direction, byte count, source
kind, and encoding mode. Native target names are local adapter details.

## Clipboard Flows

### Local Linux Clipboard To Remote Peer

```text
native clipboard changes
  -> LinuxClipboardEndpoint marks pending change
  -> ClipboardRuntimeService::pumpOnce observes IClipboardChangeMonitor
  -> LinuxClipboardEndpoint::snapshot reads native targets
  -> endpoint maps native targets to TransferSourceBundle
  -> runtime policy authorizes LocalSnapshotAnnounce
  -> ClipboardModuleBase::announceLocalBundle sends FDCL FormatList on small_data
```

Local file targets:

```text
text/uri-list or x-special/gnome-copied-files
  -> sanitized TransferFileList
  -> LinuxLocalFileTransferSource implements ITransferFileContentProvider
  -> remote peer reads file bytes through FDCL FileRangeRequest
```

### Remote Format Offer To Local Linux Clipboard

```text
FDCL FormatList received on small_data
  -> ClipboardModuleBase stores RemoteFdclTransferSource
  -> LinuxClipboardEndpoint::publishBundle installs native delayed offer
  -> local app requests a MIME target
  -> clipbus XCB backend issues a promise-style target request callback
  -> LinuxClipboardEndpoint maps native target to canonical format
  -> endpoint calls IClipboardRemoteReader::readRemoteFormat
  -> ClipboardRuntimeRemoteReader sends FDCL ReadFormatRequest
  -> peer returns ReadFormatResponse or Error
  -> endpoint completes the pending clipbus request with bytes or error
  -> clipbus writes the X11 property and sends SelectionNotify
```

Text, HTML, RTF, and PNG should remain delayed when the selected backend
supports delayed delivery. If the backend cannot delay a format safely, it may
return `Unsupported` rather than eagerly reading large or sensitive content.

### Remote File Offer To Local Linux Path

Remote file clipboard is the primary fuse-promise use case:

```text
FDCL FormatList contains application/x-fdcl-file-list
  -> endpoint decodes sanitized TransferFileList descriptors
  -> LinuxFusePromisePublisher opens fp_context_t
  -> provider is registered with a read callback
  -> builder adds directories and files from TransferFileList
  -> provider_node_id encodes offer/source/object/file identity
  -> fp_promise_commit returns visible root path
  -> endpoint asks clipbus XCB backend to publish text/uri-list and x-special/*
     MIME targets containing file:// URIs under the fuse-promise visible path
```

When a local application opens a promised file:

```text
read(2)
  -> FUSE
  -> fuse-promised
  -> libfusepromise provider callback thread
  -> LinuxFusePromiseReadDispatcher enqueues request to FusionDesk runtime owner
  -> runtime owner performs LockObject, FileRangeRequest, UnlockObject
  -> response bytes are copied into fp_read_response_t
  -> FUSE returns bytes to the local application
```

The first implementation should use range-scoped object lifetime:

```text
LockObject for the file/range identity
FileRangeRequest for the requested offset and length
UnlockObject in all success and failure paths
```

If this proves too expensive, add a lease cache later:

```text
first read locks object with a bounded lease
subsequent reads reuse the lock until lease expiry or publication clear
clear/reconnect/timeout releases all cached locks
```

Do not materialize remote files into a temporary directory for production
publication. Materialization is allowed only for explicit save/validation tools
or an opt-in fallback mode that is clearly diagnosed as non-production.

## Threading Model

The fuse-promise C callback is synchronous and may run on a provider helper
thread. FusionDesk runtime and transport pumping are not guaranteed to be
thread-safe from that callback.

Add a dispatcher:

```text
LinuxFusePromiseReadDispatcher
  accepts provider read requests from fuse-promise callback thread
  validates request length, offset, provider_node_id, and publication state
  posts work to the runtime/event-loop owner
  waits on a condition variable or future with a bounded timeout
  returns fp_status_t and byte count to fuse-promise
```

Runtime-side execution:

```text
PcClipboardRuntimeReadPump or a Linux-specific pump extension
  drains pending fuse read tasks
  calls ClipboardRuntimeRemoteReader / IClipboardRemoteFileReader
  maps protocol::ResponseStatus to dispatcher result
```

Timeouts must send FDCL Cancel where the existing remote reader supports it.
No endpoint callback may wait indefinitely.

## X11 And Qt Boundary

Phase order:

```text
1. Headless/fake Linux backend for endpoint tests.
2. External clipbus XCB backend for CLIPBOARD selection, TARGETS, UTF8_STRING,
   text/html, image/png, text/uri-list, XFixes owner-change notifications,
   delayed SelectionRequest rendering, and owner marker targets.
```

Wayland and portal are not current backends:

```text
do not proxy Wayland clipboard through the X11/XCB backend
do not add Wayland or portal code to clipbus for the current FusionDesk scope
if Wayland support is required later, create a separate platform integration
plan based on compositor/seat/focus/portal permission constraints
under native Wayland sessions without XWayland clipboard bridging, return
UnsupportedByPlatform/Unsupported with diagnostics
```

X11 requirements:

```text
use the external clipbus XCB backend; do not use QClipboard or QMimeData
own CLIPBOARD selection through an internal requestor/owner window
run event processing inside the Rust library with XCB polling and explicit
wakeups, hidden behind the C ABI
do not use QObject, QThread, QTimer, QSocketNotifier, Qt signals, or a
QGuiApplication event loop
watch owner changes with XFixes when available
answer TARGETS requests with supported MIME targets
serve requested data lazily
handle selection clear and ownership loss
handle SelectionRequest, SelectionNotify, PropertyNotify, TARGETS, TIMESTAMP,
and SAVE_TARGETS explicitly
support owner marker as a private target for loop suppression
support INCR for rich inline payloads in a later slice, or fail oversized
inline payloads deterministically while file contents use fuse-promise
```

The existing `adapters/qt/clipboard/QtClipboardEndpoint` may remain for tests
or explicit fallback, but `platform/linux/clipboard` must not depend on it,
include its headers, or share implementation code with it.

## Reconnect And Cleanup

Use existing module reconnect behavior:

```text
pause new reads when small_data or large_data is degraded
cancel pending reads and release object locks
release large_data reservations
after rebind, announce only the latest local bundle
do not replay content automatically
```

Linux endpoint cleanup:

```text
clear native owner marker and suppression records
invalidate current fuse-promise publication on clear/replacement
release all cached object locks
unregister the active fuse-promise provider if the publication is obsolete
return ProviderGone/IO for stale promised paths
```

If old promised paths remain visible because fuse-promise lacks per-promise
destroy, stale FDCL identity must still prevent serving unrelated current
clipboard data.

## Build Integration

CMake additions:

```text
if(UNIX AND NOT APPLE)
  find_package(PkgConfig)
  pkg_check_modules(FUSEPROMISE fuse-promise)
  pkg_check_modules(CLIPBUS clipbus)

  add_library(fusiondesk_platform_linux_feature_adapters ...)
  target_link_libraries(... fusiondesk_core
      ${FUSEPROMISE_LIBRARIES}
      ${CLIPBUS_LIBRARIES})
  target_include_directories(... ${FUSEPROMISE_INCLUDE_DIRS}
      ${CLIPBUS_INCLUDE_DIRS})
  # Do not link Qt::Core, Qt::Gui, Qt::Widgets, FUSIONDESK_QT_LIBS, or
  # FUSIONDESK_QT_GUI_LIB from this target.
endif()
```

The Rust library is not vendored into FusionDesk core and must not be copied
from legacy `Source/`. Packaging may either install a prebuilt `clipbus`
library or build the external Rust project before configuring FusionDesk.
FusionDesk consumes only the installed C ABI header, library, and
pkg-config/CMake metadata.

PC shell integration:

```text
--clipboard-endpoint linux
--linux-clipboard-backend auto|x11|fake
--clipboard-linux-fuse-runtime-dir <dir>
--clipboard-linux-fuse-required
--clipboard-linux-file-fallback deny|materialize-for-validation
```

`auto` on Linux should prefer the Linux endpoint when the target is linked and
fall back to `qt` only when explicitly acceptable. Diagnostics must print the
selected Linux backend and fuse-promise availability.

## Test Plan

Keep tests layered:

```text
existing pure module tests
  fusiondesk_clipboard_fdcl_codec_tests
  fusiondesk_clipboard_transfer_tests
  fusiondesk_clipboard_module_tests
  fusiondesk_clipboard_module_file_transfer_tests
  fusiondesk_clipboard_module_object_lock_tests
  fusiondesk_clipboard_large_data_scheduler_tests
  fusiondesk_clipboard_runtime_service_tests
  fusiondesk_clipboard_runtime_reconnect_tests

new Linux endpoint tests
  linux fake backend text/html/png snapshot and publish
  rust XCB wrapper maps callback events to LinuxClipboardEndpoint events
  clipbus promise callback completes immediate, pending, timeout, and cancel
  linux text/uri-list local file snapshot to TransferFileList
  linux remote file publication builds fuse-promise metadata from TransferFileList
  fuse-promise read callback maps offset/length to FDCL FileRangeRequest
  object lock/unlock runs on success, read failure, timeout, and clear
  stale offer returns deterministic provider failure

opt-in environment gates
  real X11 clipboard smoke through clipbus
  real fuse-promised + /dev/fuse read smoke
  two-peer PC Linux clipboard file smoke
```

Required purity checks:

```text
rg -n "#include <Q|QString|QByteArray|QObject|QTcpSocket|QThread|QVariant|QJson|QWindow|QAndroid" include/fusiondesk/core src/core
rg -n "fuse-promise|xcb|wayland|gtk|QClipboard|QMimeData" include/fusiondesk/core src/core src/modules/clipboard include/fusiondesk/modules/clipboard
rg -n "#include <Q|QString|QByteArray|QObject|QThread|QTimer|QSocketNotifier|QClipboard|QMimeData|QGuiApplication" include/fusiondesk/platform/linux src/platform/linux
rg -n "clipbus_|clipbus" include/fusiondesk/core src/core include/fusiondesk/modules src/modules
```

The second scan should produce no matches outside platform/adapters/tests.
The third scan should produce no matches in the Linux production target.
The fourth scan should produce no matches outside platform/adapters/tests.

## Implementation Milestones

### LCLIP-01 Linux Endpoint Skeleton

```text
add LinuxClipboardEndpoint with fake backend
support pending-change monitor
snapshot and publish text/plain;charset=utf-8
wire --clipboard-endpoint linux in PC shell when target exists
tests: fake endpoint text snapshot/publish and runtime pump
```

### LCLIP-02 Linux MIME Formats

```text
map UTF8_STRING, text/plain, text/html, text/rtf, image/png
add owner marker suppression
add C++ wrapper for external clipbus XCB backend
support CLIPBOARD ownership, TARGETS, delayed target request callbacks, and
owner-change events
tests: fake backend, wrapper callback tests, plus opt-in X11 smoke
```

### LCLIP-03 Local Linux File Offers

```text
parse text/uri-list and x-special/gnome-copied-files
sanitize names and relative paths
build FileGroupTransferSource with Linux local file range provider
tests: local file and directory snapshot/range read
```

### LCLIP-04 Remote File Promise Publication

```text
wrap libfusepromise C ABI in RAII platform code
create remote file promise tree from TransferFileList
publish file:// URI list pointing at fuse-promise visible paths through the
clipbus target promise callback
bridge provider read callback to runtime owner
range-scoped LockObject/FileRangeRequest/UnlockObject
tests: fake fuse publisher, dispatcher timeout/cancel, opt-in real FUSE smoke
```

### LCLIP-05 Reconnect, Diagnostics, And Packaging

```text
release locks and invalidate publication on reconnect
emit endpoint diagnostics for backend, fuse path, promise id, provider state,
read counts, bytes, failures, and stale reads
add Linux package dependency notes for fuse-promise/fuse3
add two-peer PC Linux clipboard smoke where environment supports /dev/fuse
```

## Non-Goals

```text
do not rewrite clipboard.redirect module for Linux
do not send Linux native target names as the wire identity
do not proxy Wayland or portal clipboard through the X11/XCB backend
do not treat Wayland clipboard support as part of the current Linux endpoint
do not move the clipbus library into FusionDesk core or modules
do not let the clipbus library own FDCL, policy, session, network, or
fuse-promise lifecycle
do not copy fuse-promise source into FusionDesk
do not use fuse-promise private IPC
do not make QtCore, QtGui, QtWidgets, QObject, QThread, QTimer, QSocketNotifier,
QClipboard, QMimeData, or Qt platform plugins required for platform/linux
do not eagerly materialize remote files for production clipboard publication
do not serve stale promised paths with current clipboard data
do not let FUSE callbacks mutate FusionDesk session/network state directly
```
