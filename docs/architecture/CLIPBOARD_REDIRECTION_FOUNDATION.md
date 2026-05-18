# Clipboard Redirection Foundation Consensus

This document is the foundation contract for rebuilding clipboard
redirection in `FusionDesk`. It records the implementation principles agreed from
legacy `Source/` behavior, FUSIONDESK architecture rules, and mature remote desktop
clipboard designs.

It is a design baseline, not implementation evidence. The actual state of code
remains tracked in `FUSIONDESK_IMPLEMENTATION_BASELINE.md` and stage completion
remains tracked in `FUSIONDESK_STAGE_GATES.md`.

## Scope

Clipboard redirection belongs to Gate P5 enterprise data redirection.

The feature must be rebuilt as a new FUSIONDESK module:

```text
clipboard.redirect
clipboard.redirect.client
clipboard.redirect.agent
```

It must not reuse old `Source/` runtime classes. Old clipboard code is only a
behavior, protocol, and platform reference.

## Evidence Base

Legacy SRC behavior references:

```text
Source/Protocol/Headers/clipboard_packets.h
Source/Modules/Public/module_catalog.h
Source/Modules/Clipboard/Common/clipboard.h
Source/Modules/Clipboard/Common/clipboard.cpp
Source/Modules/Clipboard/Common/clipboardDataConverter.h
Source/Modules/Clipboard/Client/Platform/*
Source/Modules/Clipboard/Server/Platform/*
Source/Platform/Windows/fusiondeskwinutils.cpp
Source/Platform/Windows/cfusiondeskdataobject.cpp
Source/Platform/Windows/cdatareceiver.cpp
```

External design references:

```text
Microsoft MS-RDPECLIP clipboard virtual channel
FreeRDP cliprdr
Apache Guacamole stream instructions
RustDesk clipboard owner marker and multi-format model
KDE Connect clipboard packet policy
Barrier/Synergy clipboard chunking and sequence model
Qt QClipboard platform behavior
Win32 clipboard delayed rendering and AddClipboardFormatListener
```

Cross-platform API precedents:

```text
Microsoft IDataObject / FORMATETC / STGMEDIUM
https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-getdata

Microsoft Shell Clipboard Formats, including FileGroupDescriptor, FileContents,
and drop-effect formats
https://learn.microsoft.com/en-us/windows/win32/shell/clipboard

Microsoft IDragSourceHelper
https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-idragsourcehelper

Qt QMimeData, QDrag, and QClipboard
https://doc.qt.io/qt-6/qmimedata.html
https://doc.qt.io/qt-6/qdrag.html
https://doc.qt.io/qt-6/qclipboard.html

Wayland core protocol data transfer
https://wayland.freedesktop.org/docs/html/apa.html

GTK GdkContentProvider and GdkClipboard
https://docs.gtk.org/gdk4/class.ContentProvider.html
https://docs.gtk.org/gdk4/method.Clipboard.set_content.html

Apple NSPasteboard and NSPasteboardItemDataProvider
https://developer.apple.com/documentation/appkit/nspasteboard
https://developer.apple.com/documentation/appkit/nspasteboarditemdataprovider
```

## Consensus Invariants

These rules are non-negotiable for FUSIONDESK clipboard work.

1. Clipboard is a data-redirection module, not a display, input, app, or
   network shortcut.
2. The wire model is lazy: announce formats first, fetch content only when
   requested.
3. Platform clipboard ids are local implementation details. Wire payloads use
   canonical format semantics.
4. All request-like operations use `PacketEnvelope` message id, correlation id,
   timeout, response, and error rules.
5. Large payloads and file contents use FUSIONDESK stream semantics, not huge inline
   packets.
6. Direction, format class, size, stream, file, and audit policy are evaluated
   before data leaves a trust boundary.
7. Clipboard content must never be logged. Diagnostics may record format,
   direction, byte counts, status, and hashes only.
8. Reconnect restores only the latest format offer and owner state. It does not
   blindly replay content.
9. OS clipboard complexity stays behind platform or adapter endpoints.
10. Old `CLIPBOARD_DATA_PACKET` structs, Qt types, native byte order, and
    `union` payload layouts must not become the FUSIONDESK wire contract.
11. Clipboard and drag transfer share the same source-provider object model.
    The shared abstraction is `TransferSource` / `TransferSourceBundle`;
    `ClipboardSource` / `ClipboardSourceBundle` are clipboard-module aliases.
12. Clipboard is one publication mode; drag-and-drop is another publication
    mode with presentation and action metadata.
13. Source objects represent immutable transfer snapshots. Mutating clipboard
    or drag content creates a new source bundle with a new owner epoch or
    sequence.

## Ownership

```text
Session
  owns lifecycle, feature toggles, reconnect orchestration, and diagnostics.

Policy
  owns allow, deny, audit, direction, format class, size, and stream decisions.

Network
  owns channel readiness, priority, queue pressure, request tracking, and
  packet ingress/egress. It does not parse FDCL payloads.

ModuleHost
  owns role, platform, dependency, channel, and policy start gates.

clipboard.redirect module
  owns FDCL payload schema, format offer state, request handling, object
  lifetime, compatibility, and module diagnostics.

runtime/feature or runtime/data owner
  owns watcher pumping, policy/audit calls before module API use, and
  service-level lifecycle wiring.

platform/<os>/clipboard
  owns Win32/OLE, X11/Wayland, macOS Pasteboard, Android Clipboard, and other
  OS service behavior.

adapters/qt/clipboard
  owns QClipboard/QMimeData conversion and event-loop integration.

apps
  remain thin startup shells and do not construct clipboard protocol packets.
```

Dependency direction remains:

```text
apps -> runtime -> core
runtime -> modules
runtime -> adapters
modules -> core interfaces
adapters -> framework, transport, codec, or platform implementation
platform -> OS services
bindings -> external package surfaces
```

## Module Shape

The product profile should expose `clipboard.redirect` as the capability alias.
The module factory resolves it by role:

```text
ClientSession -> clipboard.redirect.client
AgentSession  -> clipboard.redirect.agent
```

Both role modules consume and produce `PacketType::Clipboard`.

Initial channel bindings:

```text
small_data
  required for MVP text format offers, read requests, short content, errors,
  and control messages.

large_data
  required only when stream, image, RTF/HTML beyond inline limits, file-list,
  or file-content transfer is enabled.
```

`small_data` and `large_data` channel specs must explicitly allow
`PacketType::Clipboard` before the module can start. Clipboard traffic must not
reuse `PayloadAck` as a generic control packet.

## Transfer Source Object Model

FUSIONDESK clipboard is modeled as a source-provider object graph. The same object
model is also suitable for drag-and-drop. This mirrors the low-level idea used
by Windows `IDataObject`, Qt `QMimeData`, Wayland data sources/offers, GTK
content providers, and macOS pasteboard item providers.

The shared abstraction is named with `Transfer*` types so future drag or data
transfer modules do not depend on `modules/clipboard` names:

```text
TransferSource
  A single content provider. It advertises one or more canonical formats and
  can produce a requested representation lazily.

TransferSourceBundle
  One immutable transfer snapshot. It contains one or more sources, ownership
  metadata, sequence metadata, origin metadata, policy metadata, and optional
  presentation metadata.

TransferOffer
  The publishable view of a bundle. FDCL FormatList is the remote wire
  representation of a clipboard transfer offer.

TransferSourceRegistry
  The local lifetime and lookup owner for active bundles, historical offers,
  pending reads, object locks, stale-offer checks, and loop suppression.
```

Inside `clipboard.redirect`, `ClipboardSource` and `ClipboardSourceBundle` may
be aliases for `TransferSource` and `TransferSourceBundle`. Documentation and
interfaces should keep the alias local to the clipboard module. Shared public
types that serve clipboard plus drag should use `Transfer*` names.

Shared transfer types remain pure C++. They must not contain `QString`,
`QMimeData`, `IDataObject*`, `HWND`, XCB/Wayland handles, Pasteboard handles,
JNI objects, or platform private pointers. OS and framework objects stay behind
platform or adapter endpoints.

Conceptual model:

```cpp
enum class TransferOrigin {
    Clipboard,
    Drag,
    Drop,
    RemoteOffer
};

enum class TransferSide {
    Local,
    Remote
};

enum class TransferEncodingMode {
    CanonicalBytes,
    NativePassthrough,
    Transcoded
};

struct TransferFormatDescriptor {
    std::string canonicalFormat;
    std::string nativeFormatName;
    std::uint32_t localFormatToken = 0;
    std::uint64_t formatId = 0;
    std::uint32_t itemIndex = 0;
    std::uint64_t estimatedBytes = 0;
    bool canInline = true;
    bool canStream = false;
    TransferEncodingMode preferredEncoding = TransferEncodingMode::CanonicalBytes;
};

class TransferSource {
public:
    virtual ~TransferSource() = default;
    virtual TransferSourceId id() const = 0;
    virtual std::vector<TransferFormatDescriptor> formats() const = 0;
    virtual TransferReadResult read(const TransferReadRequest& request) = 0;
    virtual TransferStreamResult openStream(const TransferStreamRequest& request) = 0;
};

struct TransferSourceBundle {
    TransferBundleId bundleId;
    TransferOfferId offerId;
    std::uint64_t ownerEpoch = 0;
    std::uint64_t sequence = 0;
    TransferOrigin origin = TransferOrigin::Clipboard;
    TransferSide side = TransferSide::Local;
    SessionId originSessionId;
    PolicyVersion policyVersion;
    std::uint64_t createdMonotonicUsec = 0;
    std::vector<std::shared_ptr<TransferSource>> sources;
    std::optional<TransferPresentation> presentation;
};

using ClipboardSource = TransferSource;
using ClipboardSourceBundle = TransferSourceBundle;
```

Concrete signatures may differ, but the semantic split should remain stable:

```text
TransferSource
  read/provider side only.

ClipboardEndpoint or ClipboardSink
  installs a bundle into the local OS clipboard, pasteboard, or drag operation.

TransferSourceRegistry
  owns lookup, lifetime, pending operation, and stale-offer policy.

clipboard.redirect module
  owns FDCL serialization and remote offer/request routing.
```

Read and stream requests must validate identity before touching content:

```text
offerId
bundleId
ownerEpoch
sourceId
itemIndex
formatId or local format token
canonical format
```

If any identity field is stale, unknown, or mismatched, the operation returns
`Conflict`, `NotFound`, or `Cancelled`. A stale `TransferSource` handle must
never fall through and read the current clipboard or drag content by accident.

## Format Naming And Conversion Contract

FUSIONDESK clipboard uses a three-layer format model:

```text
source OS native format name/token
  -> FUSIONDESK canonical transfer format
  -> target OS native format name/token
```

The middle layer is mandatory. It is the common language for protocol,
policy, diagnostics, audit, stale-offer validation, and format selection. Same
platform transfer may still use native byte passthrough, but it must remain
described by a canonical format so the module does not split into separate
same-OS and cross-OS protocols.

Terminology:

```text
canonicalFormat
  FUSIONDESK semantic format name. Prefer MIME-style names. Used by FDCL,
  policy, audit, compatibility, and read requests.

nativeFormatName
  Local platform or framework format name, such as CF_UNICODETEXT,
  HTML Format, UTF8_STRING, text/html, or public.utf8-plain-text.
  It is an adapter detail and is not a cross-platform identity.

localFormatToken
  Local OS atom, clipboard id, pasteboard id, or framework token. It is valid
  only inside the endpoint that produced it and must not be interpreted by the
  peer.

formatId
  Bundle-scoped FUSIONDESK id used to bind FormatList records to later
  ReadFormatRequest, stream, and stale-offer checks.

TransferEncodingMode
  Describes the bytes carried for a read or stream:
  CanonicalBytes means bytes are already in the canonical representation.
  NativePassthrough means bytes are still in the source native representation.
  Transcoded means a source-side or target-side conversion was applied.
```

Platform endpoints own both native lookup tables:

```text
Native -> Canonical
Canonical -> Native
```

They must not create direct N x N mappings between every pair of operating
systems. Cross-platform transfer is resolved through the canonical format:

```text
Windows CF_UNICODETEXT
  -> text/plain;charset=utf-8
  -> Linux UTF8_STRING or text/plain;charset=utf-8

macOS public.html
  -> text/html
  -> Windows HTML Format

Windows FileGroupDescriptorW + FileContents
  -> application/x-fdcl-file-list
  -> Linux text/uri-list, Wayland data offer, or platform file promise
```

Remote file publication is OS-specific and must not collapse into a single
"temp directory plus file URL" strategy:

```text
Windows target
  publish FileGroupDescriptorW + FileContents
  FileContents is an OLE lazy stream backed by FDCL LockObject/FileRange/UnlockObject
  do not pre-copy remote files into a local temp directory

macOS target
  publish NSPasteboard items and NSFilePromiseProvider
  promise fulfillment uses NSFileCoordinator on the target URL and streams the
  requested file/directory through FDCL
  do not pre-copy remote files into a local temp directory

Linux target
  X11/Wayland may need local URI semantics for file targets
  the production route is a FUSE/portal-style local path wrapper
  reads against that local path trigger FDCL file-range streams on demand
  eager temp materialization is only a Linux/FUSE adapter fallback or manual
  save/validation tool behavior, not the Windows or macOS native path
```

Name mapping and byte conversion are separate responsibilities:

```text
name mapping
  decides what semantic format a native OS format represents.

byte conversion
  changes the representation, encoding, line endings, container header, image
  container, file descriptor list, or stream framing when the selected source
  and target native formats differ.
```

Same-platform transfers should prefer native passthrough only when all of the
following are true:

```text
source and target OS family are compatible
native format name and registered format semantics are compatible
storage medium semantics are compatible
byte order, terminator, line-ending, and container rules are compatible
policy allows raw native passthrough
target endpoint advertises that it can install the native representation
```

If any condition fails, the module falls back to canonical bytes plus target
native conversion, or rejects the format if no safe conversion exists.

Default canonical registry:

| canonicalFormat | Windows native names | Linux/Qt/Wayland native names | macOS native names | Android native names | Notes |
| --- | --- | --- | --- | --- | --- |
| `text/plain;charset=utf-8` | `CF_UNICODETEXT` | `UTF8_STRING`, `text/plain`, `text/plain;charset=utf-8` | `public.utf8-plain-text`, `public.utf16-plain-text` | `text/plain` | Canonical bytes are UTF-8 text. Windows native bytes are UTF-16LE with Windows line ending and terminator rules. |
| `text/html` | `HTML Format` | `text/html` | `public.html` | `text/html` | Canonical bytes are UTF-8 HTML fragment/content. Windows native bytes may require CF_HTML header offsets. |
| `text/rtf` | `Rich Text Format` | `text/rtf`, `application/rtf` | `public.rtf` | optional | Usually passthrough bytes, policy controlled separately from plain text. |
| `image/png` | registered `PNG` or transcoded from `CF_DIB`/`CF_DIBV5` | `image/png` | `public.png` | `image/png` | Preferred cross-platform image representation. |
| `image/x-dib` | `CF_DIB`, `CF_DIBV5` | optional | optional | optional | Windows-fidelity format. Prefer `image/png` for cross-platform unless same-platform passthrough is negotiated. |
| `application/x-fdcl-file-list` | `FileGroupDescriptorW` + `FileContents`, or local `CF_HDROP` snapshot | `text/uri-list`, `x-special/gnome-copied-files`, `x-special/mate-copied-files` | `public.file-url`, file promise types | `text/uri-list`, content URI adapters | Canonical model is sanitized virtual file descriptors plus object ids. Raw local paths are not the protocol identity. |
| `application/x-fdcl-owner-marker` | custom registered format | custom MIME target | custom pasteboard type | custom metadata | Loop suppression and ownership marker. Never exposed as user content. |

Policy is evaluated on the canonical format, direction, byte count, source
kind, and encoding mode. Policy may force canonical bytes even when
same-platform native passthrough is technically possible. Custom/native-only
formats are denied by default until a policy and transcoding contract exists.

FDCL requests should select by canonical identity first, then by bundle/source
identity:

```text
bundleId
offerId
ownerEpoch
sourceId
itemIndex
formatId
canonicalFormat
accepted encoding modes
```

`nativeFormatName` may be carried for diagnostics, local adapter hints, and
same-platform optimization. It must not be the only selector used by the
remote peer.

## Source Types

The module should model concrete sources as interchangeable providers:

```text
LocalOsTransferSource
  Wraps the current local OS clipboard data object. Reads from Win32/OLE,
  QMimeData, X11/Wayland, Pasteboard, or Android only through an endpoint.

RemoteFdclTransferSource
  Wraps a remote FDCL FormatList. It advertises formats locally, but sends
  ReadFormatRequest only when a local paste or delayed-rendering callback asks
  for bytes.

MaterializedTransferSource
  Holds small data already available in memory. Useful for tests and MVP text.

FileGroupTransferSource
  Represents virtual files. It advertises file-list descriptors and serves file
  content by object id, file index, offset, and length.

PolicyFilteredTransferSource
  Decorates another source and exposes only policy-allowed formats.

TranscodingTransferSource
  Decorates another source and converts native or canonical representations
  such as UTF-16 text, UTF-8 text, HTML, DIB, PNG, and URI lists.
```

Source composition should prefer decorators over mutating the underlying
source. For example:

```text
LocalOsTransferSource
  -> TranscodingTransferSource
  -> PolicyFilteredTransferSource
  -> TransferSourceBundle
  -> TransferOffer
```

## Clipboard And Drag Publication

Clipboard copy/paste and drag-and-drop use the same source bundle, but publish
it differently:

```text
clipboard publication
  endpoint installs TransferSourceBundle as the OS clipboard or pasteboard
  provider. Content remains lazy when the platform supports delayed rendering.

drag publication
  endpoint starts a drag operation from the same TransferSourceBundle and adds
  presentation metadata such as icon, preview image, hotspot, and supported
  actions.
```

Platform mappings:

```text
Windows
  TransferSourceBundle -> IDataObject
  TransferFormatDescriptor -> FORMATETC
  read/openStream -> IDataObject::GetData returning STGMEDIUM
  FileGroupTransferSource -> FileGroupDescriptor + FileContents
  TransferPresentation -> IDragSourceHelper / SHDRAGIMAGE / drag feedback formats

Qt
  TransferSourceBundle -> QMimeData
  drag publication -> QDrag
  TransferPresentation -> QDrag::setPixmap, setHotSpot, setDragCursor

Wayland
  TransferSourceBundle -> wl_data_source / wl_data_offer
  formats -> offered MIME types
  read/openStream -> receive/send over file descriptor
  TransferPresentation -> optional drag icon surface

GTK
  TransferSourceBundle -> GdkContentProvider
  source union -> content provider union

macOS
  TransferSourceBundle -> NSPasteboardItem / data provider
  read/openStream -> pasteboard item provider callback
```

Remote clipboard sync should use the same model:

```text
receive FDCL FormatList
  -> create RemoteFdclTransferSource
  -> place it in a TransferSourceBundle
  -> endpoint installs the bundle as local delayed-rendering clipboard content
  -> local application paste triggers source read
  -> source sends FDCL ReadFormatRequest to the peer
```

This keeps remote clipboard consistent with local OS delayed rendering and
prevents eager transfer of large or sensitive content.

## Remote Drag Redirection Pre-Reserved Contract

The `TransferSourceBundle` model can support remote clipboard or drag content
being published as a native drag operation on the receiving machine. Drag is a
transfer offer plus a coordinate/action stream. It is not a second content
transport.

Core rule:

```text
content path
  TransferSourceBundle -> offer metadata -> lazy read/stream

drag path
  drag session id -> display/surface coordinates -> action state -> drop/cancel
```

Coordinate messages must never carry clipboard or file bytes. Content remains
owned by the transfer source model and is read lazily only when the local OS
drop target asks for data.

Local file dragged into a remote desktop should follow this shape:

```text
local OS drag enters the remote display surface
  -> client endpoint snapshots the local drag data object as TransferSourceBundle
  -> client sends DragStart with bundle/offer ids and presentation metadata
  -> client sends DragMove events with remote display coordinates
  -> agent creates RemoteFdclTransferSource for the incoming offer
  -> agent platform endpoint starts native OS drag publication
  -> agent maps coordinates into the local desktop/session
  -> remote OS drop target requests file list or file contents lazily
  -> agent source sends ReadFormatRequest/FileRangeRequest back to the client
  -> client serves the local file data through bounded stream/range reads
```

Remote content dragged back into the local desktop is symmetric:

```text
agent snapshots local OS clipboard or drag data
  -> sends TransferSourceBundle metadata to client
  -> client creates RemoteFdclTransferSource
  -> client starts native OS drag publication in the local UI process
  -> local OS target reads data lazily through the same transfer source path
```

This requires a future drag module or transfer module, not extra display logic:

```text
drag.redirect.client
  owns local drag capture from the client OS, outgoing DragStart/Move/Drop, and
  incoming remote drag publication through the client platform adapter.

drag.redirect.agent
  owns incoming drag publication into the agent OS session, outgoing agent-side
  drag capture when dragging from remote to client, and drop result reporting.

display.screen
  owns rendered frames and coordinate transforms only. It does not own drag
  content, FDCL, file descriptors, or OS drag objects.
```

Until `display.screen` has a final production surface contract, reserve a small
coordinate interface that can be implemented by a fake test mapper:

```cpp
enum class DragCoordinateSpace {
    RemoteLogical,
    RemotePhysical,
    SurfaceLogical
};

struct DragSurfaceCoordinate {
    DisplaySurfaceId surfaceId;
    DragCoordinateSpace coordinateSpace = DragCoordinateSpace::RemoteLogical;
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
    std::uint32_t buttons = 0;
    std::uint32_t modifiers = 0;
    std::uint64_t timestampUsec = 0;
};

struct DragSessionStart {
    DragSessionId dragSessionId;
    TransferBundleId bundleId;
    TransferOfferId offerId;
    TransferPresentation presentation;
    TransferActionSet allowedActions;
    DragSurfaceCoordinate start;
};

class IRemoteDragCoordinateSink {
public:
    virtual Result dragStart(const DragSessionStart& start) = 0;
    virtual Result dragMove(DragSessionId id, const DragSurfaceCoordinate& point) = 0;
    virtual Result dragDrop(DragSessionId id, const DragSurfaceCoordinate& point) = 0;
    virtual Result dragCancel(DragSessionId id, DragCancelReason reason) = 0;
};

class IRemoteDisplayCoordinateMapper {
public:
    virtual MapCoordinateResult mapToRemoteDesktop(const DragSurfaceCoordinate& point) const = 0;
};
```

The drag endpoint must be platform-owned:

```text
Windows
  Use OLE IDataObject plus DoDragDrop or shell-compatible drag publication in
  the interactive desktop session. The data object is backed by
  RemoteFdclTransferSource and serves FileGroupDescriptor/FileContents lazily.

Qt
  Use QDrag/QMimeData only in adapter or runtime UI code. The QMimeData object
  is backed by TransferSourceBundle and may lazily retrieve data.

Wayland
  Programmatic drag may be compositor-restricted and normally requires a user
  gesture and an active surface. If native drag publication is denied, fall back
  to clipboard/file paste behavior or report UnsupportedByPlatform.

macOS
  Use pasteboard/dragging session APIs from an allowed UI process and keep
  NSPasteboard objects inside the platform adapter.
```

Security and correctness rules:

```text
remote drag publication requires focused/authorized remote-view context
coordinate events are clamped to the active display surface
remote peers cannot start hidden drag operations outside the active session
move/link/delete-on-paste remain denied unless future policy explicitly allows them
drop success is reported as action result, not as permission to delete source data
coordinate rate is bounded and backpressured like other input-like traffic
drag cancel releases TransferSource, stream, and object-lock resources
```

## Transfer Presentation

Presentation metadata is separate from source content. It describes how the
transfer should look or behave during drag-and-drop; it is not required for
plain clipboard copy/paste.

Conceptual model:

```cpp
struct IconRepresentation {
    std::string format;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t bytes = 0;
    bool sensitive = false;
};

struct DragImage {
    std::string format;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double scale = 1.0;
    Point hotspot;
    bool generatedPreview = false;
    bool sensitive = false;
};

struct TransferPresentation {
    std::string displayName;
    std::uint32_t itemCount = 0;
    TransferSourceKind sourceKind = TransferSourceKind::Mixed;
    std::vector<IconRepresentation> icons;
    std::optional<DragImage> dragImage;
    TransferActionSet allowedActions;
    TransferAction preferredAction = TransferAction::Copy;
    bool previewAllowedByPolicy = false;
    PresentationPolicyResult policyResult;
    std::vector<PresentationHint> platformHints;
};
```

Rules:

```text
presentation is optional
presentation never changes source identity or content bytes
presentation is filtered by policy before publication
display names are sanitized and must not default to full paths
icons and drag images are placeholders unless content preview is allowed
raw icon pixels, thumbnails, drag previews, source app titles, and file type
labels are denied by default unless policy allows them
preview bytes are subject to size limits and audit rules
presentation should not expose full paths, document thumbnails, or sensitive
content unless policy explicitly allows it
peer-provided presentation text must never be used directly as a local file
name or trusted UI label
platformHints are adapter-only hints and do not become wire or core semantics
```

Drag actions are part of presentation and operation negotiation, not source
content:

```text
copy
  Always safe default for remote clipboard and remote drag.

move
  Requires explicit policy and completion feedback. Do not delete source data
  unless the platform reports a successful move/delete-on-paste operation and
  the source is local and policy allows deletion.

link
  Requires explicit support from both source and target. Treat as unsupported
  for MVP.

delete-on-paste
  Not part of clipboard MVP. It is a separate audited operation if ever added.
```

Remote FDCL should initially treat drag actions as metadata only. A future drag
operation protocol can add terminal action feedback, but clipboard MVP must not
interpret remote drag metadata as permission to delete or move local data.

## Endpoint Contracts

Clipboard module interfaces should live under `include/fusiondesk/modules`
rather than core. They remain pure C++ and expose no Qt, Win32, XCB, Wayland,
Pasteboard, JNI, or platform handle types.

Minimum endpoint concepts:

```cpp
struct ClipboardSnapshot {
    std::uint64_t ownerEpoch;
    std::uint64_t sequence;
    TransferSourceBundle bundle;
};

class IClipboardEndpoint {
public:
    virtual ClipboardSnapshot snapshot() = 0;
    virtual Result publishBundle(const ClipboardPublishRequest& request) = 0;
    virtual Result clearPublishedBundle(ClipboardOfferId offerId) = 0;
    virtual StreamResult openFileContents(const ClipboardFileReadRequest& request) = 0;
};

class IClipboardWatcher {
public:
    virtual Result start() = 0;
    virtual void stop() = 0;
    virtual std::optional<ClipboardChange> pollChange() = 0;
};
```

Concrete signatures may change during implementation, but the ownership must
not change.

## FDCL Payload Model

FDCL is the module-owned clipboard payload family. The common envelope remains
`PacketEnvelope`; FDCL is only the payload body.

FDCL payloads should begin with a stable module header:

```text
magic: FDCL
schemaMajor
schemaMinor
operation
operationFlags
payloadLength
```

Minimum operations:

```text
Capabilities
FormatList
ReadFormatRequest
ReadFormatResponse
LockObject
UnlockObject
FileRangeRequest
FileRangeResponse
Cancel
ErrorDetail
```

Operation semantics:

```text
Capabilities
  Request/Response when production feature negotiation needs format classes,
  stream limits, compression, owner marker, lock, or file-range support.

FormatList
  Event with NoResponseRequired. Announces the latest available formats,
  ownerEpoch, sequence, offer id, source ids, item indexes, and format ids. It
  carries metadata, not content, thumbnails, drag preview bytes, or raw icons.

ReadFormatRequest
  Request with ResponseRequired. Requests one canonical format from one offer.
  It includes offer id, bundle id, ownerEpoch, source id, item index, format id
  or local token, canonical format, accepted maximum bytes, and whether stream
  response is accepted.

ReadFormatResponse
  Terminal Response for inline content. It carries canonical format, byte
  length, optional compression flag, and content bytes.

StreamStart / StreamChunk / StreamEnd
  Used when content exceeds inline limits or when policy requires streaming.
  Stream messages use the common envelope stream kinds and carry FDCL stream
  metadata in payload.

FileRangeRequest
  Request for file clipboard data by object id, file index, offset, and length.
  It must obey negotiated chunk limits and policy limits.

LockObject / UnlockObject
  Request/Response operations that keep a file clipboard object alive while a
  paste or stream is active. The payload carries bundle id, offer id,
  ownerEpoch, source id, object id, file index, optional lock id, and lease
  duration. LockObject may allocate a lock id in its response; UnlockObject
  must carry a non-zero lock id.

Cancel
  Cancels a pending read or stream. The peer must release object locks and
  return terminal status for tracked requests when possible.
```

Every request must have non-zero `messageId`, `correlationId`, and
`timeoutMs`. Every terminal response or error must set `responseTo` to the
original request.

## Format Model

Wire format names are canonical. Native names and ids are adapter metadata only.

Initial canonical formats:

```text
text/plain;charset=utf-8
text/html
text/rtf
image/png
image/x-dib
application/x-file-list
application/octet-stream
```

Platform mapping examples:

```text
Windows
  CF_UNICODETEXT          -> text/plain;charset=utf-8
  HTML Format             -> text/html
  CF_DIB / CF_DIBV5       -> image/x-dib or image/png after conversion
  FileGroupDescriptorW    -> application/x-file-list
  FileContents            -> file range stream source

Linux X11 / Wayland
  UTF8_STRING
  text/plain;charset=UTF-8
  text/html
  text/uri-list
  x-special/gnome-copied-files
  x-special/mate-copied-files

macOS
  public.utf8-plain-text
  public.utf16-plain-text
  public.html
  public.file-url

Android
  text/plain
  text/html where supported
  text/uri-list where supported
```

Format negotiation must prefer the safest useful format:

```text
plain text before rich text
PNG before platform bitmap when both are available
file metadata before file content
known canonical formats before custom formats
```

Custom or vendor formats require explicit policy allowlist.

## Lazy Clipboard Flow

Local clipboard change:

```text
OS clipboard changes
  -> endpoint snapshots local formats
  -> runtime policy filters direction and format classes
  -> clipboard module emits FormatList Event on small_data
  -> peer stores remote offer without reading content
```

Remote paste or delayed rendering:

```text
local app requests one advertised format
  -> endpoint asks module for remote format data
  -> module sends ReadFormatRequest
  -> peer policy rechecks content send
  -> peer endpoint reads local data
  -> peer module returns inline Response or stream
  -> requester writes content to local OS clipboard/render target
```

File paste:

```text
app requests file list
  -> ReadFormatRequest(application/x-file-list)
  -> response returns descriptors, object ids, sanitized names, and sizes
  -> app requests file contents
  -> FileRangeRequest(objectId, fileIndex, offset, length)
  -> stream or bounded response returns bytes
  -> LockObject/UnlockObject protect object lifetime
```

## Policy

Policy must be directional and type-aware.

Required policy dimensions:

```text
allowClientToAgent
allowAgentToClient
allowPlainText
allowHtml
allowRtf
allowImage
allowFileList
allowFileContents
allowCustomFormats
allowPresentationMetadata
allowPreviewImage
allowRawIconPixels
allowSourceAppMetadata
allowedTransferActions
allowDeleteOnPaste
maxInlineBytes
maxStreamBytes
maxChunkBytes
maxPresentationBytes
maxFileCount
maxSingleFileBytes
stripFilePaths
stripDisplayNamePaths
auditFormatList
auditRead
auditContent
auditDenied
```

Policy checkpoints:

```text
before announcing formats
before serving ReadFormatRequest
before writing remote content into the local clipboard
before starting a stream
before each file range read
before exposing file names or source paths
before publishing presentation metadata
before generating or transmitting a preview image
before accepting a drag action other than copy
before any delete-on-paste or move completion handling
```

If policy denies an operation, the module returns `DeniedByPolicy` for request
traffic or drops unsafe events with diagnostics for fire-and-forget traffic.
If policy filtering removes every source or every format, the module drops the
offer and records diagnostics instead of sending a misleading empty FormatList.

## Loop Prevention

Loop prevention requires two mechanisms:

```text
owner marker
  If the platform supports custom clipboard formats or metadata, write a
  FusionDesk owner marker containing session id, direction, offer id, and
  sequence when injecting remote content locally.

sequence suppression
  The runtime keeps a short-lived suppression record for the most recent remote
  injection. If the watcher sees the same owner marker or sequence, it ignores
  the change instead of announcing it back to the peer.
```

Every local clipboard ownership change increments `ownerEpoch` or sequence.
Requests for stale offers should fail with `Conflict` or `NotFound`, not serve
possibly unrelated clipboard data.

## Large Payload And File Rules

The peer controls neither allocation size nor file read size.

Rules:

```text
never allocate directly from peer-provided wantLen
cap every inline response by maxInlineBytes
cap every stream chunk by maxChunkBytes
cap every stream by maxStreamBytes
pause or fail streams under BackPressure
release locks on timeout, cancel, disconnect, and reconnect
sanitize file names before exposing them locally
do not expose source full paths unless policy explicitly allows it
validate file index, object id, offset, and length before reading
return TooLarge, NotFound, Conflict, or BackPressure rather than blocking
```

File clipboard is not general filesystem redirection. It is an entry point into
file content reads associated with a clipboard object. Long-lived filesystem
mounts belong to `filesystem.redirect`.

## Pending Operation Lifecycle

Every delayed read, stream, and object lock is tracked by the clipboard module
or its transfer runtime owner. The state machine is explicit:

```text
Created
Sent
ReadingLocal
Streaming
Completing
Completed
Cancelled
TimedOut
Conflict
ChannelLost
```

Lifecycle rules:

```text
RequestTracker owns timeout expiry for request-like operations
Cancel releases local resources before waiting for peer acknowledgement
ownerEpoch changes invalidate pending reads, streams, and locks
channel degraded or reconnect moves affected operations to ChannelLost or Cancelled
stale offer, bundle, source, item, or format identity moves to Conflict
object locks are released on completion, cancel, timeout, disconnect, and reconnect
old stream ids are not resumed after reconnect unless a future resumable stream
protocol is explicitly negotiated
```

No endpoint callback may block indefinitely waiting for the remote peer. If the
platform API is synchronous, the adapter must bridge it to the bounded FUSIONDESK
request or stream model and return a platform-appropriate failure on timeout.

## Network And Priority

Default priority mapping:

```text
FormatList metadata        Normal
ReadFormatRequest          Normal
short text response        Normal
policy/audit control       Normal or Critical based on subtype
stream chunks              Bulk
file content chunks        Bulk
cancel/error               Interactive or Normal
```

Bulk clipboard traffic must not block display video, input, heartbeat, channel
init, reconnect, or policy revoke traffic.

`large_data` pressure must be visible to the clipboard runtime so it can reduce
stream windows or fail with `BackPressure`.

## Reconnect

Clipboard is non-realtime. Reconnect behavior:

```text
pause new outbound FormatList and ReadFormat traffic while affected channels are degraded
cancel or timeout pending reads and streams on degraded channels
release object locks for cancelled streams
increment reconnect generation so stale operations cannot complete later
after channel rebind, replay module ingress through ModuleHost
announce the latest local format list again if policy still allows it
announce only current bundle metadata, not historical bundles
do not replay clipboard content automatically
let the local paste operation request data again if it is still needed
```

If only `large_data` is degraded, metadata on `small_data` may continue only if
the module can honestly reject stream-required content with `ChannelUnavailable`
or `BackPressure`.

## Platform Adapter Requirements

Windows adapter:

```text
use AddClipboardFormatListener or an equivalent watcher
support delayed rendering for remote offers
support CF_UNICODETEXT, HTML Format, Rich Text Format, registered PNG,
CF_DIB/CF_DIBV5 image transcoding, FileGroupDescriptorW, and FileContents in
the staged Windows endpoint implementation
use bounded OpenClipboard retry rather than infinite waits
write an owner marker custom format when possible
hide service/session helper processes behind the endpoint boundary if needed
```

Linux adapter:

```text
support X11 selection ownership and MIME targets first where available
plan for Wayland compositor constraints separately
map UTF8_STRING, text/plain, text/html, and text/uri-list to canonical formats
avoid assuming clipboard data is immediately available
for remote files, expose local URI semantics through a FUSE/portal wrapper and
serve file bytes lazily from FDCL range reads when the local path is opened
```

macOS adapter:

```text
use Pasteboard types and promises behind the endpoint
respect application activation and platform notification limits
map public text/html/file-url UTI values to canonical formats
```

Android adapter:

```text
start with client-side text formats only
keep Qt internals hidden from the Android controller library API
respect Android clipboard privacy and lifecycle restrictions
```

Qt adapter:

```text
QClipboard and QMimeData are allowed only in adapters or runtime Qt code
convert Qt strings, byte arrays, and MIME names at the adapter boundary
do not expose QObject, QString, QByteArray, QVariant, or QMimeData to modules
```

## Diagnostics And Audit

Module diagnostics should expose counters and latest statuses:

```text
formatListsSent
formatListsReceived
readRequestsSent
readRequestsReceived
inlineResponsesSent
inlineResponsesReceived
streamsStarted
streamChunksSent
streamChunksReceived
streamsCompleted
policyDenials
tooLargeFailures
backPressureFailures
timeoutFailures
staleOfferFailures
decodeFailures
loopSuppressions
activeObjectLocks
bundlesCreated
bundlesExpired
sourcesRead
sourceStaleFailures
ownerEpochMismatches
offerIdMismatches
presentationStripped
presentationPolicyDenials
presentationSanitizeFailures
dragOffersReceived
dragStartsReceived
dragMovesReceived
dragCoordinatesClamped
dragDropsReceived
dragCancelsReceived
dragNativePublicationFailures
dragCoordinateMapFailures
dragActionsDenied
dragActionDowngrades
moveRequestsDenied
deleteOnPasteDenied
pendingReadsCancelledOnReconnect
streamsCancelledOnReconnect
locksReleasedOnEpochChange
objectLockLeaksPrevented
```

Audit events should include:

```text
session id
trace id
module id
direction
operation
bundle id
offer id
owner epoch
sequence
source id, object id, and stream id as opaque ids
drag session id when applicable
display surface id and coordinate space when applicable
canonical format
format class
bytes
file count when applicable
policy decision
policy version
response status
content hash when configured
presentation fields present or stripped
requested, selected, and effective transfer action
delete-on-paste requested, allowed, and executed flags
channel id and type
peer device id and local role
stale reason
lock lifetime ms
cancel reason
reconnect generation
reason
```

Audit and diagnostics must not include clipboard plaintext, HTML content,
image bytes, file bytes, or unsanitized local paths.

## Drag Pre-Reserved Tests

Drag tests can start before the production display module is complete. Use fake
coordinate and fake platform endpoints first; replace them with real display
and OS adapters later.

Module-level fake test:

```text
client fake local drag source exposes a file list and file contents
client builds TransferSourceBundle with FileGroupTransferSource
client sends DragStart with bundle/offer ids and first coordinate
client sends bounded DragMove events with surface coordinates only
agent receives the offer and builds RemoteFdclTransferSource
agent fake OS drag endpoint records native drag publication start
agent fake coordinate mapper maps surface coordinates to remote desktop points
agent fake drop target requests application/x-file-list
agent sends ReadFormatRequest back to client
client returns sanitized file descriptors
agent fake drop target requests first file range
client returns bounded stream or FileRangeResponse
agent completes Drop with effective action Copy
```

Assertions:

```text
DragMove and DragDrop packets carry only drag id, coordinates, modifiers, and action state
file names are sanitized before remote exposure
file contents are not transferred until fake drop target reads them
ownerEpoch and bundle id mismatches fail as Conflict
cancel releases pending reads, streams, and object locks
coordinate mapper can be replaced without changing transfer source code
```

Platform smoke after module contract stabilizes:

```text
Windows opt-in smoke
  Client process drags a local test file into a fake remote display window.
  Agent process publishes an OLE IDataObject-backed native drag in its local
  session and a test drop target reads FileGroupDescriptor/FileContents lazily.

Qt adapter smoke
  QDrag/QMimeData stays in adapter/runtime UI code. The module sees only
  TransferSourceBundle plus coordinate events.
```

## Development Flow

Build this module from coarse to fine. Each slice should land one bounded
capability, one focused proof, and one evidence update before the next slice
starts. Do not broaden into every platform or every format before the pure
module contract is stable.

Batch cadence:

```text
architecture contract
  -> pure module skeleton
  -> fake endpoint proof
  -> one real endpoint smoke
  -> rich formats
  -> file and stream transfer
  -> drag pre-reserved control path
  -> product hardening
```

Per-item closure rule:

```text
define owned files
define packet/API surface
implement the smallest vertical path
run focused tests for that path
update architecture or tracker evidence
leave unrelated dirty work untouched
```

Engineering structure guardrails:

```text
keep each production file focused on one responsibility
prefer a soft 700-900 line budget for production .cpp/.h files
split before a file turns into a mixed module, codec, policy, and platform bag
keep OS native helpers in platform-private files, not module code
split tests by scenario once smoke coverage stops being easy to scan
```

Batch completion rule:

```text
configure and build FusionDesk
run focused module tests
run affected runtime/platform smokes
run core/module purity scans
review policy, reconnect, audit, and stale-offer behavior
```

Recommended implementation sequence:

```text
CLIP-00 Architecture closure
  Commit this foundation document and keep it as the baseline for later design
  deltas.

CLIP-01 Pure transfer model
  Add TransferSource, TransferSourceBundle, TransferFormatDescriptor,
  TransferPresentation, ids, origin, and lifecycle enums as pure C++ module
  interfaces. No Qt, Win32, JNI, or old Source includes.

CLIP-02 Clipboard module skeleton
  Add clipboard.redirect.client and clipboard.redirect.agent manifests,
  factories, role gating, PacketType::Clipboard consumes/produces declarations,
  small_data channel requirements, and empty lifecycle tests.

CLIP-03 FDCL v1 codec and validator
  Add FDCL header, Capabilities, FormatList, ReadFormatRequest,
  ReadFormatResponse, Cancel, and ErrorDetail encode/decode validation.

CLIP-04 Fake endpoint text MVP
  Use MaterializedTransferSource and fake clipboard endpoints to prove local
  change -> FormatList -> remote delayed read -> ReadFormatResponse. Cover
  policy deny, TooLarge, timeout, stale ownerEpoch, and loop suppression.

CLIP-05 Runtime owner
  Add the runtime/feature or runtime/data owner that pumps endpoint changes,
  applies policy/audit, owns active bundle registry wiring, and coordinates
  start/stop/reconnect without parsing platform objects. Current
  implementation evidence: optional pure `IClipboardChangeMonitor` support lets
  runtime skip idle endpoint snapshots while endpoints without native watcher
  support keep the old polling behavior.

CLIP-06 First platform endpoint
  Implement one Windows or Qt text endpoint smoke. Keep QClipboard, QMimeData,
  OLE, and OS handles inside adapters/platform code. Current implementation
  evidence: Windows endpoint supports text/html/file-list snapshots and publish,
  delayed rendering, owner marker suppression, and `AddClipboardFormatListener`
  backed pending-change tracking. PC shell startup now has clipboard
  start/pump wiring, dry-run seed text, endpoint-published seed text,
  text-requirement verification, owner-suppression/delayed-render controls, and
  line-oriented diagnostics. Native Windows `OpenClipboard` calls use bounded,
  configurable retry through endpoint options and PC shell
  `--clipboard-open-retry-count` / `--clipboard-open-retry-delay-ms`.
  `--clipboard-require-wait-ms` keeps the PC shell pumping transports and
  clipboard runtime while waiting for required text.
  `fusiondesk_pc_two_peer_clipboard_text_smoke`
  starts real PC agent/client processes over Qt TCP profiles with distinct
  session ids and validates dry-run Windows endpoint text copy through the
  app/runtime/module/network path. Setting
  `FUSIONDESK_VALIDATE_PC_NATIVE_CLIPBOARD_TEXT=1` turns the same smoke into a
  manual native Windows clipboard gate. Current non-interactive validation
  environments may fail this gate with `OpenClipboard` access errors; passing it
  requires a logged-in interactive desktop session. The native smoke can pass
  optional retry overrides through
  `FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_COUNT` and
  `FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_DELAY_MS`.
  PC shell also exposes `--clipboard-seed-image-png` and
  `--require-clipboard-image-png` for canonical PNG closure through the same
  endpoint/runtime/module/network path. `fusiondesk_pc_two_peer_clipboard_image_smoke`
  starts real PC agent/client processes over generated Qt TCP profiles, seeds a
  local PNG file on the agent, and requires exact `image/png` bytes on the
  client dry-run endpoint.
  PC shell also exposes `--clipboard-seed-html-file`,
  `--clipboard-seed-rtf-file`, `--require-clipboard-html-file`, and
  `--require-clipboard-rtf-file`. `fusiondesk_pc_two_peer_clipboard_formatted_text_smoke`
  proves real PC agent/client processes can copy exact `text/html` and
  same-platform native-passthrough `text/rtf` bytes over the normal
  endpoint/runtime/module/network path.
  `tools/clipboard_windows_validation.ps1` is the manual Windows desktop
  entry point for local native text validation and two-machine agent/client
  clipboard validation. It now supports `-Scenario Html`, `-Scenario Rtf`, and
  `-Scenario Image`, generating default payloads or accepting `-HtmlPath`,
  `-RtfPath`, or `-ImagePath`; the dry-run RTF and image wrappers are covered
  by CTest so packaged profile/process/module rich-content flows do not
  regress. Its non-interactive dry-run text path is also covered by CTest. For
  non-dry-run Text, Html, Rtf, and Image scenarios, the wrapper now preflights
  `OpenClipboard` before launching agent/client processes and reports the
  native error code when the current process/session cannot access the OS
  clipboard. The wrapper packages with the Windows release zip and writes
  profile, stdout, and stderr logs under a validation log directory.
  Qt endpoint text, HTML, RTF, and PNG image publication verify that the
  published QMimeData or image is observable on QClipboard before reporting
  success. Qt snapshots now preserve local `text/html` or `text/rtf` plus
  plain-text fallback in one source bundle, and local QClipboard images as
  canonical `image/png` bundles. The common Qt adapter can carry these formats
  across non-Windows clipboard implementations that expose them through
  QMimeData/QClipboard. The PC text smoke has an opt-in
  `FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_TEXT=1` path that forces the client to
  use `--clipboard-endpoint qt`; it is an environment gate because desktop
  clipboard access must be available for QClipboard publication.
  macOS now has a first native AppKit endpoint skeleton in
  `platform/macos/clipboard`. It snapshots `NSPasteboard` text, HTML, RTF,
  PNG/TIFF-as-PNG, and file URL offers into `TransferSourceBundle`, publishes
  remote text/rich/image bundles through `NSPasteboardItemDataProvider` lazy
  callbacks, polls `changeCount` through `IClipboardChangeMonitor`, and exposes
  PC shell selection through `--clipboard-endpoint macos`. Remote file lists are
  published as AppKit `NSFilePromiseProvider` objects; the promise delegate does
  not create local temporary files at publish time, and instead streams the
  promised file or directory from the remote peer through FDCL
  LockObject/FileRange/UnlockObject when the local target application fulfills
  the promise.

CLIP-07 Rich formats
  Add HTML, RTF, image/png, image/x-dib, transcoding decorators, format policy,
  and bounded inline/stream thresholds. Current implementation evidence:
  Windows supports HTML Format, Rich Text Format, and registered PNG
  mapping/publication, including delayed-rendered `text/rtf` and `image/png`
  offers through registered native clipboard formats. Windows also transcodes
  CF_DIB/CF_DIBV5 image data to and from canonical `image/png`, while Qt
  supports `text/html`, `text/rtf`, and `image/png` snapshot/publication with
  plain-text fallback where applicable. Windows `image/x-dib` same-platform
  passthrough is available for DIB snapshots and remote DIB offers. PC
  two-process dry-run formatted text and image copy are now covered by
  `fusiondesk_pc_two_peer_clipboard_formatted_text_smoke` and
  `fusiondesk_pc_two_peer_clipboard_image_smoke`; richer transcoding policy
  remains follow-up work.

CLIP-08 File clipboard
  Add FileGroupTransferSource, sanitized descriptors, LockObject/UnlockObject,
  FileRangeRequest/FileRangeResponse, large_data channel use, stream pressure,
  and object lock cleanup. Current implementation evidence: pure
  FileGroupTransferSource/file-list codec, FDCL FileRange and object-lock
  codec, in-memory source registry object locks with retired-offer retention,
  module request/response handling, runtime lock/unlock API, timeout cancel
  path, large_data channel selection and BackPressure strategy for file range
  responses, large_data in-flight window/backpressure foundation with FDCL
  Ack-driven release, Windows local file path recursion/range provider,
  Qt adapter local `file://` URL recursion/range provider for QMimeData
  clipboard snapshots,
  Windows OLE FileContents lazy stream object locking/unlocking, reconnect lock
  release, RuntimeHost/SessionManager reconnect cleanup smoke for pending
  large_data reservations and object locks, in-process two-peer session
  reconnect smoke that exercises bridged FDCL request/response traffic and
  dropped large_data Ack cleanup, and real PC two-process Qt/TCP large_data
  reconnect smoke with clipboard runtime active and display keyframe request
  suppressed. PC shell now has dry-run local file seed and repeatable remote
  file-text verification options, and
  `fusiondesk_pc_two_peer_clipboard_file_smoke` proves a real PC agent/client
  process pair can announce recursive file clipboard metadata and satisfy
  multiple FDCL LockObject/FileRange/UnlockObject reads across directory and
  loose-file seed paths from the agent's local file source. The smoke and
  packaged manual validation wrapper now force small read windows so the same
  file-copy path consumes each file through chunked FileRange requests while
  object lock/unlock remains once per file. The packaged wrapper can run the
  same generated file scenario locally or across two Windows desktops,
  materialize the remote file offer into a local output directory, assert saved
  file contents, assert both object lock/unlock and file-range counters, and
  the default generated-file wrapper path is covered by CTest when PowerShell
  is available. The Qt clipboard endpoint still has a non-production validation
  fallback that can publish a remote FDCL FileList by materializing files into
  an endpoint-owned temporary directory and placing local `file://` URLs into
  QClipboard; this evidence is kept for Qt desktop plumbing only. The
  production target contract is stricter: Windows uses OLE FileContents lazy
  streams, macOS uses `NSFilePromiseProvider`, and Linux owns the local
  URI/file-path presentation through the future FUSE/portal adapter. PC shell
  can explicitly select this adapter with
  `--clipboard-endpoint qt`; the shell creates a `QGuiApplication` for that
  path and emits `clipboard.endpoint kind=qt` diagnostics, while `auto` keeps
  Windows as the Windows default. PC shell also has
  `--require-clipboard-endpoint-file-text` to verify file contents through the
  selected endpoint's native/local provider. Focused
  codec/transfer/module/runtime/platform tests cover the lower-level file and
  object-lock contracts. macOS native remote file publication now uses AppKit
  file promises for lazy fulfillment; compile/runtime validation on a real
  macOS worker and interactive Finder/drop-target validation remain external
  gates.

CLIP-09 Drag pre-reserved path
  Add drag.redirect client/agent control messages for DragStart, DragMove,
  DragDrop, and DragCancel. Use fake coordinate mapper and fake native drag
  endpoint first. Content remains TransferSourceBundle and lazy FDCL reads.
  Current implementation evidence: FDCL drag operation codec, module
  send/receive paths, endpoint coordinate sink contract, pure display-surface
  coordinate mapper seam, Windows endpoint mapped-coordinate consumption,
  PC shell static viewport mapper wiring for pre-display-module integration,
  PC shell provider-based dynamic Qt display-window viewport binding,
  terminal Drop/Cancel lifecycle cleanup, Windows dry-run coordinate sink
  validation, Windows native drag OLE preflight for FileGroupDescriptor and
  FileContents advertisement plus a bounded lazy IStream read without entering
  the blocking DoDragDrop loop, manual DragPreflight validation that asserts
  client-side LockObject/FileRange/UnlockObject requests and agent-side
  responses, CTest coverage for the non-blocking native DragPreflight wrapper,
  focused
  module/platform/app drag lifecycle tests, and
  `fusiondesk_pc_two_peer_clipboard_drag_smoke`, which sends real PC
  agent/client drag start/move/drop events for the current clipboard offer and
  verifies the peer dry-run Windows endpoint consumes the coordinates without
  carrying content bytes in those drag messages. Remaining: opt-in interactive
  native DoDragDrop/drop-target validation.

Qt adapter clipboard smokes are guarded by an explicit native QClipboard text
roundtrip probe. In non-interactive Windows sessions where OLE clipboard access
returns `CLIPBRD_E_CANT_OPEN`, the Qt adapter smoke reports an environment
skip instead of treating the unavailable desktop clipboard as a module failure.

CLIP-10 Native drag smoke
  Add an opt-in Windows or Qt smoke where a local file drag into a fake remote
  display surface becomes native drag publication on the peer and the drop
  target reads file data lazily. Current implementation evidence: Windows can
  preflight the native OLE drag data object, advertised formats, and a small
  lazy FileContents read without blocking. The Windows platform remote-file
  stream smoke now also drives a test `IDropTarget` through `DragEnter`,
  `DragOver`, and `Drop`, where the target reads `FileGroupDescriptorW` and the
  first `FileContents` `IStream` lazily, proving the drop-target side of the
  OLE contract without entering the blocking DoDragDrop loop. The manual
  DragPreflight wrapper asserts the remote LockObject/FileRange/UnlockObject
  path that backs that FileContents stream read and is now covered by CTest. The
  manual DragLoop wrapper is an opt-in interactive desktop gate for the blocking
  DoDragDrop path: it starts the real native drag, waits for the user to move it
  over a local target and release the mouse, and then asserts native drop plus
  lazy file-stream counters.
  It is not a CTest because it requires user input and a real desktop target.

CLIP-11 Product hardening
  Complete reconnect behavior, diagnostics, audit fields, policy UI/service
  hooks, platform fallback, enterprise defaults, and performance limits.
  Current implementation evidence: `ConfigurableClipboardRuntimePolicy`
  provides operation-level allow/deny rules, allowed-operation audit, denied
  audit, a bounded recent audit-event snapshot safe for diagnostics, and
  last-operation metadata; PC shell creates one runtime policy and shares it
  between `ClipboardRuntimeService` and `ClipboardRuntimeRemoteReader`;
  `--print-clipboard-diagnostics` emits `clipboard.runtime_policy` summary rows
  plus per-operation `clipboard.audit` rows without logging clipboard content.
  `ProductClipboardPolicy` is now part of `ProductProfile`: it stores the
  clipboard module allow/deny and size limits beside runtime allow/deny/audit
  rules. PC shell clipboard switches populate that product object, RuntimeHost
  uses it to mount clipboard modules when no explicit dependency override is
  supplied, and `PcProductSessionController` creates the default runtime policy
  from it. The bounded audit-event window is configurable through
  `--clipboard-runtime-max-audit-events` and the JSON runtime field
  `maxRecentAuditEvents`. PC shell can also load this product policy from a
  JSON file through `--clipboard-policy-file`, applies CLI switches as later
  overrides, and blocks startup on invalid policy JSON or invalid field types.
  The app-layer policy file helper can now serialize and atomically save the
  normalized effective `ProductClipboardPolicy` back to the same schema, and PC
  shell exposes that through `--clipboard-policy-export-file` after CLI
  overrides have been applied.
  `ClipboardProductPolicyPresentation` now turns the effective product policy
  into stable mode/action/direction/content/file/drag/runtime/audit fields,
  `PcProductSessionSnapshot` carries that presentation, and PC shell
  diagnostics emit it as `clipboard.policy`.
  `PcProductSessionController` can now start/stop the clipboard runtime, pump
  endpoint changes, expire pending reads, and expose clipboard runtime plus
  runtime-policy snapshots through `PcProductSessionSnapshot`.
  `ClipboardProductHealthPresentation` converts runtime and policy snapshots
  into stable status/action/runtime/policy state codes, and PC shell
  diagnostics emit matching `clipboard.health` rows. Remaining: polished
  product UI/service presentation, enterprise policy storage, and platform
  validation gates.
```

Early test pyramid:

```text
pure codec tests
pure transfer model tests
module fake endpoint tests
runtime owner tests
adapter smoke tests
two-peer opt-in smoke tests
manual platform validation gates for drag/file edge cases
```

The first large push should stop at `CLIP-04`: pure module skeleton, FDCL text
MVP, fake endpoints, and policy/stale/loop tests. That gives a stable
contract before platform event-loop, file stream, or native drag complexity is
introduced.

## MVP

The first implementation slice should be deliberately small:

```text
role-specific clipboard.redirect client/agent modules
FDCL v1 header and text operations
text/plain;charset=utf-8 only
FormatList Event
ReadFormatRequest Request
ReadFormatResponse terminal Response
Error path for policy deny, stale offer, timeout, and TooLarge
small_data channel only
direction policy and byte limit policy
owner marker or sequence-based loop suppression
fake endpoint tests
one Windows or Qt endpoint smoke when the module contract is stable
```

MVP non-goals:

```text
HTML
RTF
images
file clipboard
large_data streaming
compression
custom formats
Android clipboard
drag publication
presentation preview transfer
move, link, or delete-on-paste semantics
production Wayland support
service-session helper hardening
```

## Later Stages

Stage 2:

```text
large_data stream model
chunk ACK policy where needed
HTML and RTF with format policy
image/png and image/x-dib
compression flag and size accounting
```

Stage 3:

```text
file list descriptors
file content range reads
LockObject and UnlockObject
path stripping and filename sanitization
file count and per-file limits
filesystem.redirect coordination where required
```

Stage 4:

```text
Linux X11/Wayland production endpoint
macOS Pasteboard promise endpoint
Android client endpoint
drag publication from TransferSourceBundle
policy-governed TransferPresentation metadata
enterprise UI policy controls
product diagnostics presentation
```

## Forbidden Implementation Moves

Do not:

```text
include Qt headers in FusionDesk core or clipboard modules
include old Source headers from FusionDesk core or clipboard modules
copy old clipboard directories into FusionDesk as implementation
send old CLIPBOARD_DATA_PACKET on the FUSIONDESK wire
hide policy checks inside endpoint adapters
let clipboard modules own sockets or transport profiles
let apps construct FDCL packets directly
log clipboard content
wait forever on OS clipboard locks or remote responses
trust peer-provided lengths, offsets, indexes, or format ids
make file clipboard a hidden filesystem mount
replay clipboard content automatically after reconnect
treat TransferPresentation as trusted content or execution semantics
transmit thumbnails, drag preview bytes, raw icon pixels, full paths, or source
app titles without policy approval
let the remote side force move, link, delete-on-paste, or source deletion
delete a source file after an ordinary clipboard paste succeeds
reuse stale ClipboardSource, TransferSource, object id, stream id, or lock id
serve old read, stream, or file-range requests after ownerEpoch changes
use peer-provided display names directly as file names or trusted UI text
treat a presentation hash as a content hash or DLP decision
```

## Initial Landing Points

Expected implementation areas:

```text
include/fusiondesk/modules/transfer/        if shared transfer owner exists
src/modules/transfer/                        if shared transfer owner exists
include/fusiondesk/modules/drag/            if drag.redirect lands
src/modules/drag/                            if drag.redirect lands
include/fusiondesk/modules/clipboard/
src/modules/clipboard/
tests/modules/clipboard/
tests/modules/drag/
include/fusiondesk/adapters/qt/clipboard/
src/adapters/qt/clipboard/
include/fusiondesk/adapters/qt/drag/
src/adapters/qt/drag/
include/fusiondesk/platform/windows/clipboard/
src/platform/windows/clipboard/
include/fusiondesk/platform/windows/drag/
src/platform/windows/drag/
tests/adapters/qt/clipboard/
tests/adapters/qt/drag/
tests/platform/windows/clipboard/
tests/platform/windows/drag/
include/fusiondesk/runtime/feature/ or runtime/data/
src/runtime/feature/ or runtime/data/
```

Shared existing files that will likely need small, explicit updates:

```text
src/core/network/channel_defaults.cpp
src/core/module/module_catalog.cpp
src/runtime/runtime_host.cpp
apps/pc/common/pc_app_shell.cpp
docs/architecture/FUSIONDESK_IMPLEMENTATION_BASELINE.md
docs/architecture/FUSIONDESK_STAGE_GATES.md
docs/architecture/FUSIONDESK_TODO_TRACKER.md
```

Those shared updates must not move FDCL payload compatibility into core,
network, policy, session, or module composition.
