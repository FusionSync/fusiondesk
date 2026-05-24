# Clipboard Format Matrix

This document is the current `clipboard.redirect` format contract. It is based
on the active SRC2 implementation, not on legacy product compatibility.

## Rules

```text
native OS format name/token
  -> FusionDesk canonicalFormat
  -> target native OS format name/token
```

Cross-OS clipboard transfer must use canonical bytes. Native passthrough is
only allowed for same-platform fidelity formats after both endpoints agree that
the native storage semantics are compatible.

## Canonical Formats

| canonicalFormat | Class | Scope | Canonical byte representation | Current policy |
| --- | --- | --- | --- | --- |
| `text/plain;charset=utf-8` | Plain text | Cross OS | UTF-8 text bytes, no NUL terminator. Canonical line ending is LF; endpoints may convert native CRLF locally. | Default cross-OS format. |
| `text/html` | HTML | Cross OS | UTF-8 HTML fragment/content bytes. | Default cross-OS format. |
| `text/rtf` | RTF | Cross OS | RTF payload bytes. | Default cross-OS rich-text format, policy controlled separately from plain text. |
| `image/png` | Image | Cross OS | PNG file bytes. | Default cross-OS image format. |
| `application/x-fdcl-file-list` | File list | Cross OS | FDCL encoded sanitized virtual file descriptors and object ids. File bytes move through `LockObject` / `FileRange` / `UnlockObject`. | Default cross-OS file and directory format. |
| `image/x-dib` | Image | Same platform | Windows DIB or DIBV5 bytes. | Windows-fidelity same-platform format only. Cross OS must use `image/png`. |
| `application/x-fdcl-owner-marker` | Owner marker | Internal | Internal marker bytes. | Loop suppression only. Never exposed as user content. |

## Native Format Mapping

| canonicalFormat | Windows native formats | Linux / Qt / X11 / Wayland native formats | macOS native formats | Android native formats | Notes |
| --- | --- | --- | --- | --- | --- |
| `text/plain;charset=utf-8` | `CF_UNICODETEXT` | `text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING` | `public.utf8-plain-text`, `public.utf16-plain-text`, `NSPasteboardTypeString` | `text/plain` | Qt endpoint uses `QClipboard::setText` / `QMimeData::text`. |
| `text/html` | registered `HTML Format` | `text/html` | `public.html` | `text/html` | Windows uses CF_HTML header conversion. Qt/macOS keep UTF-8 HTML bytes. |
| `text/rtf` | registered `Rich Text Format` | `text/rtf`, `application/rtf` | `public.rtf` | Not mapped now | RTF bytes are passed through unless a later validator/transcoder rejects them. |
| `image/png` | registered `PNG`; also renders companion `CF_DIBV5` and `CF_DIB` | `image/png` / `QImage` encoded as PNG | `public.png`; snapshot may transcode `public.tiff` to PNG | `image/png` | Preferred cross-OS image format. |
| `application/x-fdcl-file-list` | local `CF_HDROP`; remote `FileGroupDescriptorW` + `FileContents` | `text/uri-list`, `x-special/gnome-copied-files`, `x-special/mate-copied-files`, Qt URL list | `public.file-url`; remote `NSFilePromiseProvider` | `text/uri-list` / content URI adapter | Native path strings are never the protocol identity. |
| `image/x-dib` | `CF_DIBV5`, `CF_DIB` | Not a default target | Not a default target | Not a default target | Same-platform Windows fidelity only. |
| `application/x-fdcl-owner-marker` | FusionDesk private registered clipboard format | Not implemented as user format | Not implemented as user format | Not implemented as user format | Internal loop suppression. |

## Current Endpoint Support

| canonicalFormat | Windows endpoint | Qt / Linux endpoint | macOS endpoint | Android endpoint |
| --- | --- | --- | --- | --- |
| `text/plain;charset=utf-8` | Snapshot and publish implemented. Delayed render supports `CF_UNICODETEXT`. | Snapshot and publish implemented through Qt text APIs. | Snapshot and delayed publish implemented through `NSPasteboardItemDataProvider`. | Mapping contract only. Endpoint pending. |
| `text/html` | Snapshot and delayed publish implemented through `HTML Format`. | Snapshot and publish implemented through `QMimeData::html`. | Snapshot and delayed publish implemented through `public.html`. | Mapping contract only. Endpoint pending. |
| `text/rtf` | Snapshot and delayed publish implemented through `Rich Text Format`. | Snapshot and publish implemented through `text/rtf` / `application/rtf` MIME bytes. | Snapshot and delayed publish implemented through `public.rtf`. | Not implemented. |
| `image/png` | Snapshot and publish implemented. Can transcode `CF_DIB` / `CF_DIBV5` to PNG and PNG back to DIB targets. | Snapshot and publish implemented through `QImage` encoded/decoded as PNG. | Snapshot and delayed publish implemented through `public.png`; `public.tiff` snapshot is converted to PNG. | Mapping contract only. Endpoint pending. |
| `application/x-fdcl-file-list` | Local `CF_HDROP` snapshot implemented. Remote publish uses `FileGroupDescriptorW` + OLE `FileContents` lazy streams. | Local URL snapshot implemented. Remote publish currently materializes remote files to local temp URLs; production Linux should replace this with FUSE/portal lazy paths. | Local file URL snapshot implemented. Remote publish uses `NSFilePromiseProvider` and streams on promise fulfillment. | Mapping contract only. Endpoint pending. |
| `image/x-dib` | Snapshot, publish, and delayed render implemented for Windows only. | Not implemented. | Not implemented. | Not implemented. |
| `application/x-fdcl-owner-marker` | Internal owner marker write/read implemented for suppressing self-originated clipboard updates. | Not implemented. | Not implemented. | Not implemented. |

## Cross-OS Byte Conversion

| canonicalFormat | Source native -> canonical bytes | Canonical bytes -> target native | Current implementation notes |
| --- | --- | --- | --- |
| `text/plain;charset=utf-8` | Windows reads `CF_UNICODETEXT`, converts UTF-16LE to UTF-8, removes NUL terminator, and normalizes CRLF to LF. Qt/macOS read native string values as UTF-8 bytes. | Windows converts UTF-8 to UTF-16LE, normalizes LF to CRLF, and adds NUL terminator for `CF_UNICODETEXT`. Qt/macOS publish UTF-8 text through native text APIs or pasteboard data provider. | Implemented for Windows, Qt, and macOS. |
| `text/html` | Windows parses `HTML Format` and extracts the CF_HTML fragment when offsets or fragment markers exist. Qt/macOS read `text/html` / `public.html` bytes as canonical UTF-8 HTML. | Windows wraps canonical HTML with CF_HTML header and fragment offsets. Qt/macOS publish the canonical bytes as `text/html` / `public.html`. | Implemented for Windows, Qt, and macOS. |
| `text/rtf` | Read native RTF bytes as canonical RTF bytes. | Publish canonical RTF bytes as `Rich Text Format`, `text/rtf`, or `public.rtf`. | Implemented as byte passthrough for Windows, Qt, and macOS. |
| `image/png` | Windows registered `PNG` is canonical. Windows `CF_DIB` / `CF_DIBV5` is decoded through WIC and encoded as PNG. Qt encodes `QImage` to PNG. macOS `public.png` is canonical; `public.tiff` is converted through `NSImage` / `NSBitmapImageRep` to PNG. | Windows can publish `PNG` and companion `CF_DIBV5` / `CF_DIB` by decoding PNG through WIC. Qt decodes PNG into `QImage` and sets the clipboard image. macOS publishes `public.png` through the pasteboard data provider. | Implemented for Windows, Qt, and macOS. |
| `application/x-fdcl-file-list` | Local file formats are converted to FDCL sanitized descriptors: names, relative paths, directory flags, size, time, and object ids. Windows reads `CF_HDROP`; Qt reads URL lists; macOS reads `public.file-url`. | Windows publishes `FileGroupDescriptorW` plus lazy `FileContents` streams. macOS publishes `NSFilePromiseProvider` and streams files on promise fulfillment. Qt currently materializes remote files to a temp directory and publishes local URLs; Linux production should replace this with FUSE/portal lazy paths. | Implemented on Windows/Qt/macOS with different native strategies. Linux lazy FUSE path remains pending. |
| `image/x-dib` | Windows reads `CF_DIB` / `CF_DIBV5` as same-platform native bytes and can also produce companion `image/png`. | Windows writes `CF_DIB` / `CF_DIBV5` directly or converts PNG to DIB through WIC. | Same-platform Windows only. Not cross OS. |
| `application/x-fdcl-owner-marker` | Internal marker read only for owner-loop suppression. | Internal marker write only when the endpoint publishes its own offer. | Not content. Never requested by remote apps. |

## Selection Rules

| Situation | Preferred selection |
| --- | --- |
| Same content exists as plain text and rich text | Prefer plain text for maximum compatibility unless the target asks for HTML/RTF. |
| Same image exists as PNG and DIB | Prefer `image/png` for cross OS. Use `image/x-dib` only for Windows same-platform fidelity. |
| File copy or drag | Transfer `application/x-fdcl-file-list` first, then file bytes through object locks and file-range reads. |
| Unknown native format | Do not transfer by default. It needs an explicit canonical format and converter contract. |
| Same OS native passthrough | Allowed only when source OS, target OS, native format name, storage medium, byte representation, and policy all agree. |
