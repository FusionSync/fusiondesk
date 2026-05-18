# FusionDesk Toolchain Presets

These files are entry points for `CMakePresets.json`. They deliberately
avoid hard-coded SDK, NDK, Qt, sysroot, or compiler paths.

Host Windows builds can use `windows-host-release` directly. Cross presets
require environment variables before configuring.

Linux cross presets use:

```text
FUSIONDESK_<TARGET>_C
FUSIONDESK_<TARGET>_CXX
FUSIONDESK_<TARGET>_SYSROOT
```

where `<TARGET>` is one of:

```text
LINUX_X86_64
LINUX_AARCH64
LINUX_LOONGARCH64
LINUX_MIPS64EL
LINUX_RK3568
LINUX_RK3588
```

Android presets use `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT`.

OpenHarmony/HarmonyOS uses:

```text
FUSIONDESK_OHOS_TOOLCHAIN_FILE
```

Qt kit paths should enter through `CMAKE_PREFIX_PATH` or the user's
`CMakeUserPresets.json`, not through repository-wide hard-coded paths.
