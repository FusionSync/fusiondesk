# PC Apps

PC app shells:

```text
client
agent
auth
tools
```

Supported PC targets start with Windows x64, Linux x86_64, Linux arm64, and macOS where the existing product permits.

Current shell status:

```text
fusiondesk_pc_client: QCoreApplication shell for ClientSession startup
fusiondesk_pc_agent: QCoreApplication shell for AgentSession startup
fusiondesk_pc_profile_plan: pre-start tool that writes paired client/agent TCP profile JSON
--smoke: initialize RuntimeHost and session, then exit
--transport-profile <path>: load and apply runtime/qt TCP transport profile JSON
--listen-profile <path>: start runtime/qt TCP listeners from profile JSON
--peer-profile-service: handle FDPP peer-profile requests on the control channel
--peer-profile-channel <name=endpoint>: client-side FDPP request for a channel to connect after the agent listens
--peer-profile-wait-ms <ms>: wait budget for the client-side FDPP exchange
--peer-profile-timeout-ms <ms>: request timeout carried in the FDPP PacketEnvelope
--module-inventory-service: handle FDMI module inventory requests after profile modules are mounted
--module-inventory-request: request the peer's FDMI module inventory before starting profile modules
--module-inventory-wait-ms <ms>: wait budget for FDMI response or responder-side remote inventory receipt, including empty inventories
--module-inventory-timeout-ms <ms>: request timeout carried in the FDMI PacketEnvelope
--print-module-inventory-diagnostics: print FDMI pending/completion/status/responder counters
--reconnect-profile <path>: load a reconnect profile and trigger session reconnect after startup
--reconnect-after-ms <ms>: delay before reconnect-profile is applied
--reconnect-reason <text>: reconnect reason recorded in session diagnostics
--reconnect-no-display-keyframe: do not request display fresh-state/keyframe during non-display reconnect validation
--print-reconnect-diagnostics: print ReconnectRuntimeService diagnostics at reconnect and exit lifecycle points
--print-session-diagnostics: print SessionRuntimeDiagnosticsSnapshot at startup, module start, reconnect, and exit lifecycle points, including display_capture and display_codec rows for product UI/service handoff
--print-display-runtime-diagnostics: print DisplayRuntimeService diagnostics at display-runtime start and exit lifecycle points, including pump/frame attempts, frame timestamps, last observed frame age, effective FPS x1000, and consecutive misses
--mount-display: mount role-specific display adapter dependencies
--pump-display: agent-side bounded display frame pump after display module start
--display-fps <n>: target agent display pump frame rate; also enables --pump-display on agent
--display-first-frame-timeout-ms <ms>: client-side first-frame timeout before DisplayRuntimeService requests a keyframe over small_data
--show-display-window: client-side QWidget display window bound to the Qt image renderer, with a runtime health status line
--display-source-type <monitor|window|virtual-display|mobile-projection>: source kind requested from the capture backend selector; defaults to monitor in the current PC shell
--display-source-id <n>: selected display source id for agent capture adapters; invalid Windows source ids fail with SourceNotFound instead of falling back to primary
--display-native-source-handle <n|0xhex>: optional native capture source handle for adapters such as WGC window/HWND capture
--display-target-width <px>: optional target capture width for resolution-compatible downscale or stretch
--display-target-height <px>: optional target capture height for resolution-compatible downscale or stretch
--display-scale-mode <source|fit|stretch>: source keeps native capture size, fit preserves aspect ratio within target size, stretch uses exact target size
--display-no-cursor: disable default software cursor composition in Windows GDI/DXGI BGRA capture for diagnostics or cursor sideband experiments
--display-target-platform <name>: optional capture platform-family hint for selector diagnostics; defaults to windows_desktop in the current PC shell
--print-display-capture-plan: print role, platform, backend candidates, selected adapter, rejection reasons, and capability-source diagnostics for display capture planning
--print-display-sources: print selected capture backend source catalog rows for monitor/window source selection diagnostics, including source type and native handle when available
--print-display-codec-plan: print codec direction, preference, selected adapter, selected codec, fallback/hardware/zero-copy flags, candidates, and rejection diagnostics
--display-codec <raw|h264|h265|av1>: preferred display codec family; current fallback remains raw until production codec adapters are linked
--display-codec-backend <id>: optional exact codec backend id, such as raw, windows.media_foundation.h264, or a future adapter id
--display-codec-negotiate-local: run the runtime two-peer codec negotiation helper against local agent-encoder and client-decoder capability views, then pin the current role to the negotiated adapter for diagnostics
--display-codec-negotiate-fdpp: use FDPP display.codec.v1 to send the client decoder inventory to the agent, let the agent negotiate against its encoder inventory, return the selected inventory, and pin both roles before display module dependencies are created
--display-codec-fdpp-wait-ms <ms>: agent-side wait budget for the first FDPP display codec negotiation request when --display-codec-negotiate-fdpp is enabled
--display-codec-no-hardware / --display-codec-no-software / --display-codec-prefer-software / --display-codec-no-zerocopy: codec selector policy diagnostics
--mount-profile-modules: mount the selected RuntimeHost product profile modules
--profile-module <id>: add one product profile module such as input.mouse, input.keyboard, test.echo, or display.screen
--mount-input: add input.mouse and input.keyboard profile modules and feature adapter dependencies
--start-clipboard: wait for required module channels, then start clipboard modules
--pump-clipboard: start the pure ClipboardRuntimeService owner for endpoint snapshots and remote reads
--clipboard-endpoint <auto|windows|macos|qt>: select the PC clipboard endpoint adapter; auto keeps the native OS endpoint as the default where one is linked and falls back to Qt where that is the linked adapter
--clipboard-dry-run-text <text>: seed the Windows clipboard endpoint dry-run store before clipboard startup
--clipboard-seed-text <text>: publish a local text bundle through the selected clipboard endpoint before clipboard startup
--clipboard-seed-html-file <path>: publish a local canonical text/html bundle through the selected clipboard endpoint before clipboard startup
--clipboard-seed-rtf-file <path>: publish a local text/rtf bundle through the selected clipboard endpoint before clipboard startup
--clipboard-seed-image-png <path>: publish a local canonical image/png bundle through the selected clipboard endpoint before clipboard startup
--clipboard-seed-file <path>: seed a dry-run Windows local file source before clipboard startup; may be repeated and can expand directories
--clipboard-send-drag-drop: after clipboard startup, send a dry-run drag start/move/drop lifecycle for the current local clipboard offer
--clipboard-send-drag-start-only: with --clipboard-send-drag-drop, stop after DragStart; useful for native drag preflight where the receiving endpoint owns the native drag publication check
--clipboard-drag-offer-wait-ms <ms>: wait budget for --clipboard-send-drag-drop to observe a local clipboard offer
--clipboard-drag-session-id <n>: drag session id used by --clipboard-send-drag-drop
--clipboard-drag-start-x/y, --clipboard-drag-move-x/y, --clipboard-drag-drop-x/y: remote logical coordinates for drag lifecycle smoke paths
--require-clipboard-text <text>: verify the selected clipboard endpoint exposes the expected text before exit
--require-clipboard-html-file <path>: verify the selected clipboard endpoint exposes text/html bytes matching a local file before exit
--require-clipboard-rtf-file <path>: verify the selected clipboard endpoint exposes text/rtf bytes matching a local file before exit
--require-clipboard-image-png <path>: verify the selected clipboard endpoint exposes image/png bytes matching a local file before exit
--require-clipboard-endpoint-file-text <relativePath=text|text>: verify the selected clipboard endpoint exposes local file-list contents through its native/local file provider before exit
--require-clipboard-file-text <relativePath=text|text>: verify remote file clipboard metadata and read file bytes through FDCL LockObject/FileRange/UnlockObject before exit
--save-clipboard-files-dir <dir>: materialize the remote file clipboard offer into a local directory through FDCL LockObject/FileRange/UnlockObject
--clipboard-require-wait-ms <ms>: keep pumping runtime/transports while waiting for clipboard text, rich text, image, file, or save requirements
--clipboard-file-read-timeout-ms <ms>: timeout for --require-clipboard-file-text remote file range reads
--clipboard-file-read-chunk-bytes <bytes>: cap each --require-clipboard-file-text FileRange read window so large files are consumed in multiple requests
--clipboard-policy-file <path>: load ProductProfile clipboard module/runtime policy from JSON before CLI overrides are applied
--clipboard-policy-export-file <path>: write the effective ProductProfile clipboard module/runtime policy JSON after file loading and CLI overrides
--clipboard-runtime-audit: audit allowed ClipboardRuntimeService and remote-reader operations into bounded runtime policy diagnostics and `clipboard.audit` rows
--clipboard-runtime-max-audit-events <count>: cap retained recent runtime audit events; 0 disables recent event storage while keeping counters
--clipboard-runtime-deny-announce/read/file-range/object-lock/object-unlock/expiry: deny specific runtime clipboard operations before module dispatch
Clipboard module and runtime policy options are folded into the current
`ProductProfile.clipboardPolicy`; RuntimeHost-mounted clipboard modules,
`ClipboardRuntimeService`, and `ClipboardRuntimeRemoteReader` now derive their
module and runtime policy from that product object unless a caller injects an
explicit override.
When `--clipboard-endpoint qt` is selected, the PC shell creates a
`QGuiApplication` instead of the default `QCoreApplication` so the Qt endpoint
can access `QClipboard`. The Qt endpoint carries plain text, `text/html`,
`text/rtf`, `image/png`, and file URL offers. Rich text keeps a plain-text
fallback where available. Qt endpoint
diagnostics are printed as
`clipboard.endpoint kind=qt` rows and include local/remote file-list
materialization counters. Remote-file materialization in this Qt path is a
validation/fallback path only; production Windows uses OLE FileContents lazy
streams, production macOS uses `NSFilePromiseProvider`, and production Linux
will expose local URI/file-path semantics through a FUSE/portal-style adapter.
The macOS endpoint uses AppKit `NSPasteboard` directly. It snapshots
`public.utf8-plain-text`, `public.html`, `public.rtf`, `public.png`,
`public.tiff` transcoded to canonical `image/png`, and `public.file-url`
offers. Remote text, HTML, RTF, and PNG are published through an
`NSPasteboardItemDataProvider` so bytes are fetched lazily from the current
`TransferSourceBundle`; remote file lists are published as
`NSFilePromiseProvider` objects, and the promise delegate streams the requested
file or directory from the remote peer through FDCL
LockObject/FileRange/UnlockObject only when the local target app fulfills the
promise. Diagnostics are printed as `clipboard.endpoint kind=macos` rows and
include promise publication/provider counters.
The Windows endpoint carries `CF_UNICODETEXT`, `HTML Format`,
`Rich Text Format`, registered `PNG`, CF_DIB/CF_DIBV5 image data,
CF_HDROP/local file-list snapshots, and remote FileGroupDescriptor/FileContents
publication. RTF and registered PNG are byte-pass-through as canonical
`text/rtf` and `image/png`; CF_DIB/CF_DIBV5 image data is transcoded to and
from canonical `image/png` on the Windows native path, and `image/x-dib`
preserves native DIB bytes for same-Windows passthrough.
The policy file accepts a top-level `clipboardPolicy` object with `module`
fields such as `allowFileContents`, `allowImage`, `maxFileRangeBytes`, and
`maxFileCount`, plus `runtime` fields such as `auditAllowed`,
`maxRecentAuditEvents`,
`allowRemoteFormatRead`, and `denialReason`. Invalid JSON or invalid field
types block PC shell startup instead of silently falling back.
`--clipboard-policy-export-file` writes the normalized effective policy back to
the same schema, including CLI overrides and file-content dependent runtime
denials, so product services can persist what the shell actually used.
`--print-clipboard-diagnostics` also emits a `clipboard.policy` row with stable
fields for future UI/service use: `mode`, `action`, `direction`, `content`,
`file`, `drag`, `runtime`, `audit`, allowed content booleans, and size limits.
--windows-clipboard-native: use the real Windows clipboard endpoint instead of dry-run mode
--clipboard-open-retry-count <n>: bounded OpenClipboard retry attempts for native Windows endpoint operations
--clipboard-open-retry-delay-ms <ms>: delay between native Windows OpenClipboard retry attempts
--clipboard-no-owner-suppression: allow verification reads of clipboard content owned by the current process
--clipboard-no-delayed-rendering: materialize text into the native clipboard immediately instead of delayed rendering
--windows-clipboard-native-drag-preflight: opt into the native Windows drag publication preflight; it creates the OLE file drag data object, validates advertised FileGroupDescriptor/FileContents formats, and reads a small FileContents stream window without entering the blocking DoDragDrop loop
--windows-clipboard-native-drag-loop: opt into the interactive native Windows DoDragDrop loop for manual desktop validation
--print-clipboard-diagnostics: print clipboard product policy, health, runtime, runtime policy, module, and endpoint diagnostics at startup and exit checks
FUSIONDESK_VALIDATE_PC_NATIVE_CLIPBOARD_TEXT=1: switch fusiondesk_pc_two_peer_clipboard_text_smoke to the native Windows clipboard path
FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_TEXT=1: switch fusiondesk_pc_two_peer_clipboard_text_smoke client side to --clipboard-endpoint qt and validate QClipboard text publication; this requires an available desktop clipboard
FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_COUNT: optional native smoke retry-count override passed as --clipboard-open-retry-count
FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_DELAY_MS: optional native smoke retry-delay override passed as --clipboard-open-retry-delay-ms
FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE_DRAG_PREFLIGHT=1: environment equivalent for --windows-clipboard-native-drag-preflight
FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE_DRAG_LOOP=1: environment equivalent for --windows-clipboard-native-drag-loop
--start-profile-modules: wait for required channels, then start all mounted profile modules
--pump-profile-modules: start the pure runtime feature pump owner for selected input modules
--feature-pump-interval-ms <ms>: Qt timer interval for --pump-profile-modules
--max-input-events-per-pump <n>: maximum input events drained per feature pump tick
--send-test-echo <text>: mount/start test.echo and send a unified request/response probe
--require-test-echo-response: wait for the peer test.echo response before continuing
--test-echo-wait-ms <ms>: wait budget for the test.echo response
--test-echo-timeout-ms <ms>: request timeout carried on the test.echo PacketEnvelope
--require-profile-module <id>: assert that a selected module was mounted for the current role
--start-display: wait for required module channels, then start display modules
--wait-channels-ms <ms>: readiness wait budget for --start-display or --start-profile-modules
--run-ms <ms>: bounded Qt event-loop run for smoke and integration tests
--require-display-frame: verify sent/rendered display frame progress during bounded runs
```

Windows codec diagnostics include a rollout-gated MediaFoundation H.264
candidate before raw fallback. `--display-codec-policy windows-h264-production`
sets the current PC ProductProfile policy to prefer
`windows.media_foundation.h264`, enable the P-frame stream, and preserve raw
fallback when the host probe fails. Set `FUSIONDESK_ENABLE_MF_CODEC=1` only
for probe diagnostics and direct codec-object validation. Set
`FUSIONDESK_SELECT_MF_H264=1` as an additional validation-only gate when you
need the selector to choose `windows.media_foundation.h264` outside product
policy. `FUSIONDESK_VALIDATE_PC_H264_DISPLAY=1` enables the two-peer PC smoke
to exercise a Windows-Windows H.264 first-frame and reconnect fresh-frame path.
The pure runtime negotiation helper already
defines the two-peer encoder/decoder intersection rules. The PC shell can
exercise those rules locally with `--display-codec-negotiate-local`, which is
useful for Windows-Windows diagnostics and H.264 gate testing. It can also use
`--display-codec-negotiate-fdpp` on both peers when the client requests
channels with `--peer-profile-channel`: the request carries the client decoder
inventory, the agent computes the negotiated codec from its encoder inventory,
and both roles pin the selected adapter before display dependencies are
created. This is the live startup carrier; production H.264 now comes from the
ProductProfile display codec policy, while environment variables remain as
validation overrides.
When `--print-session-diagnostics` is enabled, selected codec state is also
published as `session.diagnostics.display_codec` rows with adapter, codec,
backend, selection mode, fallback reason, H.264 delta/P-frame state, payload
and error counters, and delayed decoder counters. The same diagnostics pass
also emits `session.diagnostics.display_health`, a product-facing summary that
collapses session, link/channel, display module, capture, and codec state into
`ok`, `warning`, `degraded`, or `blocked` plus a usable flag and stable
`status`, `action`, `captureState`, `codecState`, fallback, delay, and recovery
warning fields for future PC UI/service readers.

Current test tooling can generate matching multi-endpoint no-sessionId
client-connect and agent-listen profile files through
`QtTcpPeerProfileCoordinator`. `fusiondesk_pc_profile_plan` exposes that
same local planning path for scripts:

```text
fusiondesk_pc_profile_plan --client-profile client.json --agent-profile agent.json --channel control=127.0.0.1:47101 --channel small_data=127.0.0.1:47102 --channel main_screen=127.0.0.1:47103
```

The profile plan tool also accepts `large_data` for early redirection startup
profiles. Display control/recovery and input events use `small_data`.

When `--pump-profile-modules` is used, the runtime feature owner keeps the
input capture lifecycle instead of leaving each input module to open the same
capture source independently. Concrete global input ownership is still pending.

Reconnect profiles should contain only degraded replacement channels. For
example, the two-peer display smoke starts with `control`, `small_data`, and
`main_screen` profiles, then reconnects only `main_screen` so control-channel
continuity is preserved. `--print-reconnect-diagnostics` emits a stable line-oriented summary
of `ReconnectRuntimeServiceSnapshot::diagnostics` for shell scripts and future
PC UI lifecycle integration.
The clipboard reconnect smoke follows the same rule for `large_data` and uses
`--reconnect-no-display-keyframe` because non-display reconnects must not ask
display modules for fresh state.
The clipboard formatted-text smoke uses `--clipboard-seed-html-file` and
`--clipboard-seed-rtf-file` on the agent and matching
`--require-clipboard-html-file` / `--require-clipboard-rtf-file` checks on the
client to prove real PC processes can announce `text/html` and native
passthrough `text/rtf` bundles and materialize exact bytes on the peer dry-run
Windows endpoint.
The clipboard image smoke uses `--clipboard-seed-image-png` on the agent and
`--require-clipboard-image-png` on the client to prove a real PC process can
announce a canonical `image/png` bundle and materialize exact PNG bytes on the
peer dry-run endpoint over the normal runtime/module/network path.
The clipboard file smoke uses `--clipboard-seed-file` on the agent and
`--require-clipboard-file-text` on the client to prove a real PC process can
announce recursive file clipboard metadata and satisfy multiple remote FDCL
LockObject/FileRange/UnlockObject reads across directory and loose-file seed
paths. It also uses `--save-clipboard-files-dir` to materialize the remote
offer into a local output directory and verify the saved file contents.
The same file smoke uses `--require-clipboard-endpoint-file-text` on the agent
to verify the selected endpoint's local file provider. Setting
`FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_FILE=1` forces the client to use
`--clipboard-endpoint qt` and also validates the remote file offer through the
Qt endpoint's QClipboard file URL publication path; this is an environment gate
for the Qt fallback because QClipboard needs an available desktop clipboard.
The clipboard drag smoke uses `--clipboard-send-drag-drop` to prove a real PC
process can send remote drag coordinates for the current clipboard offer and
the peer dry-run Windows endpoint receives start/move/drop without transferring
content in coordinate messages.
For manual validation, `tools/clipboard_windows_validation.ps1 -Scenario
DragPreflight` drives the same two-process path and can opt the receiving
endpoint into `--windows-clipboard-native-drag-preflight`; the native path uses
`--clipboard-send-drag-start-only` and asserts the remote
LockObject/FileRange/UnlockObject request-response path behind a lazy
FileContents read. Add `-DryRun` when you only want to validate
runtime/network/module closure.
`--print-session-diagnostics` emits the matching line-oriented
`SessionRuntimeDiagnosticsSnapshot` view so scripts and future PC UI/service
owners can consume session, link/channel, mounted/running module, and blocked
channel state without parsing stderr. For display sessions it also carries
`session.diagnostics.display_capture`, `session.diagnostics.display_codec`,
and `session.diagnostics.display_health` rows so UI/service code does not need
to parse codec-plan stdout. The display health row is generated through the
same runtime presentation helper intended for the production UI/service layer.

`tools/clipboard_windows_validation.ps1` is the manual Windows clipboard
validation entry point. It uses the existing PC agent/client/profile-plan
executables and can run a local native text roundtrip from an interactive
desktop:

```text
powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText
```

For two Windows desktops on the same network, run the same script with
`-Mode Agent -AgentAddress <agent-lan-ip>` on the source desktop and
`-Mode Client -AgentAddress <agent-lan-ip>` on the receiving desktop. Add
`-DryRun` to validate only the runtime/network/module path without touching the
OS clipboard. `-Scenario File` generates a small recursive file payload by
default and validates multi-file, chunked FDCL
LockObject/FileRange/UnlockObject reads without touching the OS clipboard; use
`-FileReadChunkBytes` to force smaller FileRange request windows,
`-SaveFilesDir` to materialize the remote offer into a local directory, or use
`-FilePath` plus matching
receiving-side `-FileRequirement` entries when validating caller-supplied text
files. `-Scenario Image` generates a small PNG by default, or uses `-ImagePath`,
and validates canonical `image/png` copy through `--clipboard-seed-image-png`
and `--require-clipboard-image-png`. `-Scenario Html` and `-Scenario Rtf`
generate matching payload files by default, or use `-HtmlPath` / `-RtfPath`,
and validate `text/html` or `text/rtf` copy through the formatted seed/require
CLI. Without `-DryRun`, rich text and image scenarios use the native Windows
clipboard path and therefore need an unlocked interactive desktop; the wrapper
preflights `OpenClipboard` for non-dry-run text, HTML, RTF, and image scenarios
before launching agent/client processes, and reports the native error code if
the current process/session cannot access the OS clipboard. `-Scenario DragPreflight`
validates the remote file drag publication preflight and the
LockObject/FileRange/UnlockObject stream path behind it. `-Scenario DragLoop`
is an opt-in interactive desktop gate that enables `DoDragDrop`; the receiver
must move the generated native drag over a local folder or drop target, then
press and release the mouse before `-DragLoopTimeoutMs`, and the wrapper
asserts native drop plus lazy file stream
counters. DragLoop is intentionally not registered as CTest because it blocks
on user input.
`fusiondesk_clipboard_windows_validation_text_dryrun_smoke`,
`fusiondesk_clipboard_windows_validation_image_dryrun_smoke`,
`fusiondesk_clipboard_windows_validation_rtf_dryrun_smoke`,
`fusiondesk_clipboard_windows_validation_file_smoke`, and
`fusiondesk_clipboard_windows_validation_drag_preflight_smoke` keep the
non-interactive text, rich text, image, file, and native drag preflight wrapper
scenarios under CTest on Windows builds where PowerShell is available.

The PC shell can now use JSON only for the bootstrap control channel, then use
FDPP over that control channel to negotiate additional channels through
`QtPeerProfileRuntimeService`. Full UI lifecycle orchestration and tunnel/P2P
selection are still pending.

After profile modules are mounted, the PC shell can also use FDMI over the
control channel to exchange module declarations before module start. The
resulting remote module versions are passed into `SessionMainline` as
`ModuleStartOptions.peerVersions`; the mainline still does not parse module
payload schemas. Terminal FDMI `Error` or non-Ok `Response` packets fail the
startup path immediately instead of being reported as timeouts. When session
diagnostics are enabled, the accepted remote inventory is printed as
`session.diagnostics.remote_module` rows.

For module integration smoke work, `test.echo` is the smallest real module:
it uses the bootstrap control channel, sends an `Exchange` `Request`, and
requires the peer module to return a correlated `Response` or `Error`.
It exercises module mount, version gates, channel readiness, ingress routing,
and the unified response pattern without depending on display, input,
Qt UI, or OS adapters.

Current two-process display smoke uses this tool to generate no-sessionId
control, small_data, and main_screen startup profiles before starting the PC
agent and client shells.
