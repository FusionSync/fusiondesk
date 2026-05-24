# Delivery and Packaging

Windows and Linux are the primary delivery targets for the enterprise rebuild.

macOS remains supported where existing functionality permits, but it is not a release blocker during the first `FusionDesk` rebuild stage.

## CI Priority

Required:

```text
FusionDesk core compile gate
Windows FusionDesk release zip
Linux FusionDesk release zip
Publish GitHub Release from Windows and Linux zip artifacts
```

Optional:

```text
macOS Package
Android APK/AAB
Android Controller AAR
```

## Artifact Policy

Windows:

```text
fusiondesk-windows-x64-<version>.zip
bin/fusiondesk_pc_client.exe
bin/fusiondesk_pc_agent.exe
bin/fusiondesk_pc_profile_plan.exe
manifest.txt
commit id
build number
Qt runtime copied by windeployqt
unsigned engineering release
```

Linux:

```text
fusiondesk-linux-x86_64-<version>.zip
bin/fusiondesk_pc_client
bin/fusiondesk_pc_agent
bin/fusiondesk_pc_profile_plan
manifest.txt
commit id
build number
Qt runtime libraries copied from QT_ROOT_DIR when available
```

Clipboard CLI:

```text
fusiondesk-clip-windows-x64-<version>.zip
bin/fusiondesk_clip.exe
bin/fusiondesk_pc_profile_plan.exe
fusiondesk_clip_agent.cmd
fusiondesk_clip_client.cmd
tools/clipboard_windows_validation.ps1

fusiondesk-clip-linux-x86_64-<version>.zip
bin/fusiondesk_clip
bin/fusiondesk_pc_profile_plan
fusiondesk_clip_agent.sh
fusiondesk_clip_client.sh
```

Do not upload:

```text
legacy Source trees
Target working trees
intermediate qmake Makefiles
temporary packaging folders
old Build/Tools/packaging directories
```

## FusionDesk Build Policy

`FusionDesk` should build as a standalone core library before application packaging.

Initial target:

```text
fusiondesk_core
```

Future targets:

```text
fusiondesk_runtime
fusiondesk_transport_adapters
fusiondesk_codec_adapters
fusiondesk_platform_adapters
fusiondesk_android_client
fusiondesk_android_controller_aar
```

## FusionDesk Release Zip Commands

Windows:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\tools\package_windows_release.ps1 -BuildDir build -Configuration Release -OutputDir artifacts\release -Version dev
.\tools\package_clip_windows_release.ps1 -BuildDir build -Configuration Release -OutputDir artifacts\release -Version dev
```

Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
VERSION=dev bash tools/package_linux_release.sh
VERSION=dev bash tools/package_clip_linux_release.sh
```

The active GitHub Actions workflow is `.github/workflows/FusionDesk-release.yml`.
It builds and tests `FusionDesk` on Windows and Linux, uploads both zip packages as
workflow artifacts, and creates or updates a GitHub Release for tags or manual
workflow runs with `publish_release=true`.

## Android Packaging Policy

Android client package:

```text
APK or AAB
ABI split by arm64-v8a and x86_64 when needed
Qt Android deployment metadata
native symbols archive
version metadata
```

Android controller library package:

```text
AAR
classes.jar
jniLibs/<abi>/*.so
consumer ProGuard/R8 rules
AndroidManifest permissions
native symbols archive
```
