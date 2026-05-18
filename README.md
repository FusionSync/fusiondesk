# FusionDesk

`FusionDesk` is the clean architecture root for the enterprise remote desktop platform rebuild.

The legacy `Source/` tree has been removed from this repository. `FusionDesk` defines the new architecture contracts first and implements new modules against those contracts; old-product behavior may be consulted only from an external archive or legacy repository snapshot.

```text
core/protocol
core/session
core/network
core/module
core/policy
runtime
modules
platform
adapters
bindings
apps
```

The first goal is not to copy display, audio, clipboard, filesystem, printer, camera, input, gamepad, or peripheral code. The first goal is to give new implementations one stable runtime loop while using old code only to understand behavior and protocol compatibility.

## Build Presets

`CMakePresets.json` defines the host Windows build and cross-target entry
points for Linux, Android, OpenHarmony/HarmonyOS, and Rockchip board builds.

```powershell
cmake --list-presets=all .
cmake --preset windows-host-release
cmake --build --preset windows-host-release
ctest --test-dir build/windows-host-release -C Release --output-on-failure
```

Cross presets intentionally require environment variables for compilers,
sysroots, SDKs, or NDKs. See `cmake/toolchains/README.md`.

## Manual DXGI Capture Validation

Windows GDI remains the capture fallback. The Windows aggregate factory probes
DXGI Desktop Duplication by default and falls back to GDI when the probe fails;
set `FUSIONDESK_ENABLE_DXGI_CAPTURE=0` to disable DXGI during staged rollout
or diagnostics.

```powershell
cmake --build build --config Release --target fusiondesk_windows_dxgi_display_capture_opt_in_tests
$env:FUSIONDESK_VALIDATE_DXGI_CAPTURE="1"
.\build\Release\fusiondesk_windows_dxgi_display_capture_opt_in_tests.exe
Remove-Item Env:\FUSIONDESK_VALIDATE_DXGI_CAPTURE
```

Default CTest runs skip real DXGI capture validation unless the environment
variable above is set. The manual gate requests a bounded 320x180 fit frame
and fails if DXGI returns a frame outside that target.

PC agent capture backend selection can be controlled for diagnostics:

```powershell
.\build\Release\fusiondesk_pc_agent.exe --smoke --mount-display --display-capture-backend gdi --print-session-diagnostics
.\build\Release\fusiondesk_pc_agent.exe --smoke --mount-display --display-capture-backend dxgi --print-session-diagnostics
$env:FUSIONDESK_ENABLE_DXGI_CAPTURE="0"
.\build\Release\fusiondesk_pc_agent.exe --smoke --mount-display --print-session-diagnostics
Remove-Item Env:\FUSIONDESK_ENABLE_DXGI_CAPTURE
```

Add `--print-display-capture-plan` to print stable line-oriented backend
planning diagnostics, including capability source, candidates, selected
adapter, and rejection reasons. Add `--print-display-sources` to print the
selected backend source catalog through the backend factory contract for
monitor/window selection diagnostics. The source catalog row includes
`ok=`, requested source type, requested adapter, provider state, rejection
count, requested source id/native handle, selected-source match state, source
candidate count, and message count. Failure cases also print
`display.source.catalog.rejection`, `display.source.catalog.message`, and
`display.source.selection.message` rows, while successful rows mark the matched
`display.source` with `selected=1`. Product UI and service readers can use the
same source-picker preflight before opening a capture adapter.

Windows Graphics Capture now has a rollout-gated monitor/window capture
adapter. It is disabled by default while DXGI remains the normal
production-first monitor path; enable it explicitly for field validation:

```powershell
$env:FUSIONDESK_ENABLE_WGC_CAPTURE="0"  # reports disabled
$env:FUSIONDESK_ENABLE_WGC_CAPTURE="1"  # enables WGC monitor/window probing
Remove-Item Env:\FUSIONDESK_ENABLE_WGC_CAPTURE
```

The manual real-frame gate is opt-in, matching the DXGI validation style:

```powershell
$env:FUSIONDESK_VALIDATE_WGC_CAPTURE="1"
ctest --test-dir build -C Release -R fusiondesk_windows_wgc_display_capture_opt_in_tests --output-on-failure
Remove-Item Env:\FUSIONDESK_VALIDATE_WGC_CAPTURE
```

Add `FUSIONDESK_VALIDATE_WGC_WINDOW_CAPTURE=1` to the same manual test to
capture the first visible WGC window source by native handle. PC shell source
catalog diagnostics print `type=` and `nativeHandle=` so future UI and scripts
can distinguish monitor and window sources.

For the current raw-frame MVP, PC agent `--start-display` starts the agent
display frame pump. The runtime/display `withDefaultRawFrameCaptureTarget`
helper applies a bounded 1280x720 fit capture target when no target is supplied.
Use `--display-target-width`, `--display-target-height`, or
`--display-scale-mode source` when validating a specific source size. Invalid
Windows `--display-source-id` values fail with `SourceNotFound` instead of
falling back to the primary monitor.
Windows GDI and DXGI BGRA captures compose the current Win32 cursor by default
through `DisplayCaptureOpenOptions::includeCursor`; pass `--display-no-cursor`
to disable that composition for diagnostics or cursor sideband experiments. A
lower-latency cursor sideband remains future work.
In auto backend mode, `--start-display` also passes the Windows aggregate
capture factory into `DisplayRuntimeService`, so device-loss style capture
failures can switch to the next compatible backend and send a fresh keyframe.
Explicit `--display-capture-backend` values pin the requested adapter by
default for controlled rollout diagnostics.
The runtime also applies the first recovery guardrails: failed recovery attempts
enter a short cooldown, and repeated same-backend recoverable failures can be
promoted to backend failover instead of reopening the same adapter forever.
PC client `--start-display` starts the same runtime owner in client monitoring
mode. Use `--display-first-frame-timeout-ms` to tune first-frame recovery and
`--print-display-runtime-diagnostics` to inspect the runtime snapshot during
CLI smoke runs. Display runtime rows include service-observed captured pixel
bytes, encoded payload bytes, sent payload bytes, last frame byte sizes, and
effective bitrate Kbps, so the raw-frame MVP and future codec adapters can be
compared through the same diagnostics surface. When Qt Widgets is available,
PC client `--show-display-window`
opens the first QWidget-backed display surface and binds it to the Qt image
renderer without changing display module contracts.

`runtime/display` now also has a pure codec selection contract for the next
production step. The default matrix keeps `raw_bgra` available as the current
MVP fallback and represents H.264/H.265/AV1 software, hardware, zero-copy, and
Rockchip MPP paths as explicit capability slots until real codec adapters are
linked. This does not change the current CLI startup path yet; it defines the
selection surface that future FFmpeg, MediaFoundation, VAAPI, VideoToolbox,
MediaCodec, Harmony, or Rockchip adapters must satisfy.
The same runtime area now also has a pure two-peer negotiation helper that
intersects the agent encoder request with the client decoder request, tries
common codec preferences in order, allows raw fallback only through the normal
selection policy, and refuses exact-backend mismatches instead of silently
falling back. FDPP can now carry opaque peer-profile extensions, and
`runtime/display` defines the `display.codec.v1` extension payload for
serializing encoder/decoder selection requests plus codec capability lists.
The PC shell can use `--display-codec-negotiate-fdpp` on both peers to carry
the client decoder inventory in the FDPP request, let the agent negotiate
against its local encoder inventory, return the selected inventory, and pin
both roles before display dependencies are created. It can still opt into a
local Windows-Windows diagnostic view with `--display-codec-negotiate-local`.
Both paths print `display.codec.negotiation` rows when
`--print-display-codec-plan` is enabled.
PC agent/client startup now creates the current raw encoder/decoder through
the same codec backend factory surface, and `--print-display-codec-plan`
prints the selected codec, fallback/hardware/zero-copy flags, candidates, and
rejection reasons. Use `--display-codec`, `--display-codec-backend`,
`--display-codec-no-hardware`, `--display-codec-no-software`,
`--display-codec-prefer-software`, or `--display-codec-no-zerocopy` to exercise
the selection policy before real codec adapters are linked.
On Windows, the PC codec registry now publishes a rollout-gated
MediaFoundation H.264 candidate before raw fallback. It is disabled by default;
set `FUSIONDESK_ENABLE_MF_CODEC=1` only to run capability probe diagnostics.
The candidate still reports unavailable unless the validation-only selector
gate is set. `runtime/display` provides `convertBgraToNv12` and `convertNv12ToBgra`
as pure CPU helpers with stable NV12 plane metadata for the validation path and
future codec adapters. The same gate also enables the adapter preflight helper
used by tests to validate BGRA-to-NV12 conversion, verify MFT creation, and
check decoder output type acceptance without making the adapter selectable.
The factory can direct-create validation-only MediaFoundation H.264 encoder
and decoder objects when `FUSIONDESK_ENABLE_MF_CODEC=1`, but selector-based
startup still falls back to raw unless the additional validation-only
`FUSIONDESK_SELECT_MF_H264=1` gate is set. That gate lets PC diagnostics
exercise `windows.media_foundation.h264` without changing the default raw
fallback or claiming full production negotiation.
Set `FUSIONDESK_VALIDATE_MF_H264_ENCODE=1` when running
`fusiondesk_windows_media_foundation_display_codec_tests` to require a real
H.264 encode, FDSF wrapping, direct factory decode, repeated keyframe recovery,
and BGRA decoded output on the current Windows host.
Set `FUSIONDESK_VALIDATE_PC_H264_DISPLAY=1` when running
`fusiondesk_pc_two_peer_start_display_smoke` to validate the Windows-Windows
H.264 first-frame and reconnect fresh-frame path through real PC agent/client
processes.
The display module also owns the FDSF compressed-frame payload envelope for
future H.264/H.265/AV1 adapters. FDSF carries codec id, Annex B/AVCC bitstream
format, coded and visible dimensions, decoded pixel format, frame id, keyframe
flag, timestamp, codec config bytes, and compressed bitstream bytes inside the
VIDEO payload, so the mainline still treats display frames as opaque module
payloads.

Backend selection can also receive architecture and SoC hints for board or
cross-target diagnostics:

```powershell
.\build\Release\fusiondesk_pc_agent.exe --smoke --mount-display --display-target-platform windows-desktop --display-target-arch x86_64 --display-target-soc generic
.\build\Release\fusiondesk_pc_agent.exe --smoke --mount-display --display-source-type monitor --display-target-platform rockchip-linux --display-target-arch aarch64 --display-target-soc rk3588 --print-display-capture-plan
```

Accepted platform aliases include `windows`, `linux-x11`, `wayland`,
`linux-embedded`, `macos`/`darwin`, `android-client`,
`android-agent`, `harmonyos`, `openharmony`, `rk3568-linux`,
`rk3588-linux`, `rk3568-android`, and `rk3588-android`.
Accepted display source aliases include `monitor`/`desktop`, `window`,
`virtual-display`, and `mobile-projection`/`media-projection`.
Accepted architecture aliases include `x86`, `x86_64`/`amd64`,
`arm32`/`armv7`, `arm64`/`aarch64`, `loongarch64`, and `mips64el`.
Accepted SoC aliases include `generic`, `rk3568`, and `rk3588`.

`runtime/display` also has a pure capture platform plan contract. It separates
real probed factory capabilities from the diagnostic-only default matrix, so a
future Linux/macOS/Android/Harmony target can report intended backend choices
without pretending that an unlinked platform adapter is implemented.
