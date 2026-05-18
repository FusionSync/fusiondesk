#include <cassert>
#include <string>
#include <vector>

#include "fusiondesk/runtime/display/display_capture_backend_selection.h"

using namespace fusiondesk;

namespace {

void windowsMonitorPrefersDxgi()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.hasSelection);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication);
    assert(!selected.fallbackSelected);
    assert(std::string(runtime::display::displayCaptureBackendKindName(
               selected.selected.backend)) == "windows.dxgi.desktop_duplication");
}

void windowsWindowSelectsGraphicsCapture()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Window;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture);
    assert(!selected.selected.supportsServiceSession);
}

void requestedWindowsGdiOverridesDxgiPriority()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.requestedAdapterId = "windows.gdi";

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.adapterId == "windows.gdi");
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGdi);
    assert(selected.fallbackSelected);

    bool sawRequestedReject = false;
    for (const runtime::display::DisplayCaptureBackendRejection& rejection :
         selected.rejected) {
        if (rejection.reason == "capture backend does not match requested adapter id")
            sawRequestedReject = true;
    }
    assert(sawRequestedReject);
}

void requestedMissingBackendFails()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.requestedAdapterId = "windows.missing";

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(!selected.ok);
    assert(!selected.rejected.empty());
    for (const runtime::display::DisplayCaptureBackendRejection& rejection :
         selected.rejected) {
        assert(rejection.reason ==
               "capture backend does not match requested adapter id");
    }
}

void waylandRequiresPermission()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::LinuxWayland;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.acceptedMemoryTypes = {runtime::display::DisplayCaptureMemoryType::DmaBuf};
    request.permissionGranted = false;

    runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);
    assert(!selected.ok);
    assert(!selected.rejected.empty());

    request.permissionGranted = true;
    selected = runtime::display::selectDisplayCaptureBackend(request);
    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::LinuxPipeWirePortal);
    assert(selected.selected.zeroCopy);
}

void rockchipLinuxUsesDrmAdapter()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::RockchipLinux;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.acceptedMemoryTypes = {runtime::display::DisplayCaptureMemoryType::DmaBuf};
    request.architecture = runtime::display::DisplayTargetArchitecture::Arm64;
    request.socProfile = runtime::display::DisplayTargetSocProfile::Rockchip3588;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::LinuxDrmKmsGbm);
    assert(selected.selected.adapterId == "linux.rockchip.drm_kms_gbm");
    assert(selected.selected.zeroCopy);
    assert(std::string(runtime::display::displayTargetArchitectureName(
               request.architecture)) == "arm64");
    assert(std::string(runtime::display::displayTargetSocProfileName(
               request.socProfile)) == "rk3588");
}

void rockchipRejectsUnsupportedArchitecture()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::RockchipLinux;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.acceptedMemoryTypes = {runtime::display::DisplayCaptureMemoryType::DmaBuf};
    request.architecture = runtime::display::DisplayTargetArchitecture::LoongArch64;
    request.socProfile = runtime::display::DisplayTargetSocProfile::Rockchip3568;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(!selected.ok);
    bool sawArchitectureReject = false;
    for (const runtime::display::DisplayCaptureBackendRejection& rejection :
         selected.rejected) {
        if (rejection.reason ==
            "capture backend does not support requested architecture")
            sawArchitectureReject = true;
    }
    assert(sawArchitectureReject);
    assert(std::string(runtime::display::displayTargetArchitectureName(
               request.architecture)) == "loongarch64");
}

void genericLinuxX11AllowsLoongArch()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::LinuxX11;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.architecture = runtime::display::DisplayTargetArchitecture::LoongArch64;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::LinuxXDamageXShm);
}

void linuxEmbeddedAllowsExtendedArchitectures()
{
    const std::vector<runtime::display::DisplayTargetArchitecture> architectures = {
        runtime::display::DisplayTargetArchitecture::X86,
        runtime::display::DisplayTargetArchitecture::X86_64,
        runtime::display::DisplayTargetArchitecture::Arm64,
        runtime::display::DisplayTargetArchitecture::LoongArch64,
        runtime::display::DisplayTargetArchitecture::Mips64El};

    for (runtime::display::DisplayTargetArchitecture architecture : architectures) {
        runtime::display::DisplayCaptureBackendSelectionRequest request;
        request.platform = runtime::display::DisplayPlatformFamily::LinuxEmbedded;
        request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
        request.acceptedMemoryTypes = {
            runtime::display::DisplayCaptureMemoryType::DmaBuf,
            runtime::display::DisplayCaptureMemoryType::CpuBuffer};
        request.architecture = architecture;

        const runtime::display::DisplayCaptureBackendSelectionResult selected =
            runtime::display::selectDisplayCaptureBackend(request);

        assert(selected.ok);
        assert(selected.selected.backend ==
               runtime::display::DisplayCaptureBackendKind::LinuxDrmKmsGbm);
    }
}

void macosPrefersScreenCaptureKit()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::MacOS;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.acceptedMemoryTypes = {
        runtime::display::DisplayCaptureMemoryType::CVPixelBuffer,
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};

    runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::MacOSScreenCaptureKit);
    assert(selected.selected.requiresUserConsent);
    assert(selected.selected.zeroCopy);

    request.permissionGranted = false;
    selected = runtime::display::selectDisplayCaptureBackend(request);
    assert(!selected.ok);
}

void androidAgentUsesMediaProjection()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::AndroidAgent;
    request.sourceType = runtime::display::DisplayCaptureSourceType::MobileProjection;
    request.acceptedMemoryTypes = {
        runtime::display::DisplayCaptureMemoryType::AndroidHardwareBuffer,
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    request.architecture = runtime::display::DisplayTargetArchitecture::Arm64;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::AndroidMediaProjection);
    assert(selected.selected.adapterId == "android.media_projection");
    assert(selected.selected.requiresUserConsent);
}

void rockchipAndroidUsesTaggedMediaProjection()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::RockchipAndroid;
    request.sourceType = runtime::display::DisplayCaptureSourceType::MobileProjection;
    request.acceptedMemoryTypes = {
        runtime::display::DisplayCaptureMemoryType::AndroidHardwareBuffer,
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    request.architecture = runtime::display::DisplayTargetArchitecture::Arm64;
    request.socProfile = runtime::display::DisplayTargetSocProfile::Rockchip3568;

    runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::AndroidMediaProjection);
    assert(selected.selected.adapterId == "android.rockchip.media_projection");
    assert(selected.selected.zeroCopy);

    request.socProfile = runtime::display::DisplayTargetSocProfile::Rockchip3588;
    selected = runtime::display::selectDisplayCaptureBackend(request);
    assert(selected.ok);
    assert(selected.selected.adapterId == "android.rockchip.media_projection");
}

void harmonyPlatformsExposeSystemCapture()
{
    const std::vector<runtime::display::DisplayPlatformFamily> platforms = {
        runtime::display::DisplayPlatformFamily::HarmonyOS,
        runtime::display::DisplayPlatformFamily::OpenHarmony};

    for (runtime::display::DisplayPlatformFamily platform : platforms) {
        runtime::display::DisplayCaptureBackendSelectionRequest request;
        request.platform = platform;
        request.sourceType =
            runtime::display::DisplayCaptureSourceType::MobileProjection;
        request.architecture = runtime::display::DisplayTargetArchitecture::Arm64;

        runtime::display::DisplayCaptureBackendSelectionResult selected =
            runtime::display::selectDisplayCaptureBackend(request);

        assert(selected.ok);
        assert(selected.selected.backend ==
               runtime::display::DisplayCaptureBackendKind::HarmonySystemCapture);
        assert(selected.selected.requiresUserConsent);

        request.permissionGranted = false;
        selected = runtime::display::selectDisplayCaptureBackend(request);
        assert(!selected.ok);
    }
}

void targetArchitectureParsing()
{
    using runtime::display::DisplayTargetArchitecture;
    using runtime::display::parseDisplayTargetArchitecture;

    assert(parseDisplayTargetArchitecture("auto") == DisplayTargetArchitecture::Unknown);
    assert(parseDisplayTargetArchitecture("x86") == DisplayTargetArchitecture::X86);
    assert(parseDisplayTargetArchitecture("amd64") == DisplayTargetArchitecture::X86_64);
    assert(parseDisplayTargetArchitecture("armv7") == DisplayTargetArchitecture::Arm32);
    assert(parseDisplayTargetArchitecture("aarch64") == DisplayTargetArchitecture::Arm64);
    assert(parseDisplayTargetArchitecture("loongarch64") ==
           DisplayTargetArchitecture::LoongArch64);
    assert(parseDisplayTargetArchitecture("mips64el") ==
           DisplayTargetArchitecture::Mips64El);
    assert(parseDisplayTargetArchitecture("not-real") ==
           DisplayTargetArchitecture::Unknown);
}

void targetPlatformParsing()
{
    using runtime::display::DisplayPlatformFamily;
    using runtime::display::parseDisplayPlatformFamily;

    assert(parseDisplayPlatformFamily("auto") == DisplayPlatformFamily::Unknown);
    assert(parseDisplayPlatformFamily("windows") ==
           DisplayPlatformFamily::WindowsDesktop);
    assert(parseDisplayPlatformFamily("linux-x11") ==
           DisplayPlatformFamily::LinuxX11);
    assert(parseDisplayPlatformFamily("wayland") ==
           DisplayPlatformFamily::LinuxWayland);
    assert(parseDisplayPlatformFamily("drm-kms") ==
           DisplayPlatformFamily::LinuxEmbedded);
    assert(parseDisplayPlatformFamily("darwin") == DisplayPlatformFamily::MacOS);
    assert(parseDisplayPlatformFamily("android-controller") ==
           DisplayPlatformFamily::AndroidClient);
    assert(parseDisplayPlatformFamily("android-agent") ==
           DisplayPlatformFamily::AndroidAgent);
    assert(parseDisplayPlatformFamily("hmos") == DisplayPlatformFamily::HarmonyOS);
    assert(parseDisplayPlatformFamily("ohos") == DisplayPlatformFamily::OpenHarmony);
    assert(parseDisplayPlatformFamily("rk3588-linux") ==
           DisplayPlatformFamily::RockchipLinux);
    assert(parseDisplayPlatformFamily("rk3568-android") ==
           DisplayPlatformFamily::RockchipAndroid);
    assert(parseDisplayPlatformFamily("not-real") ==
           DisplayPlatformFamily::Unknown);
}

void sourceTypeParsing()
{
    using runtime::display::DisplayCaptureSourceType;
    using runtime::display::parseDisplayCaptureSourceType;

    assert(parseDisplayCaptureSourceType("auto") ==
           DisplayCaptureSourceType::Unknown);
    assert(parseDisplayCaptureSourceType("monitor") ==
           DisplayCaptureSourceType::Monitor);
    assert(parseDisplayCaptureSourceType("desktop") ==
           DisplayCaptureSourceType::Monitor);
    assert(parseDisplayCaptureSourceType("window-capture") ==
           DisplayCaptureSourceType::Window);
    assert(parseDisplayCaptureSourceType("virtual-display") ==
           DisplayCaptureSourceType::VirtualDisplay);
    assert(parseDisplayCaptureSourceType("media-projection") ==
           DisplayCaptureSourceType::MobileProjection);
    assert(parseDisplayCaptureSourceType("not-real") ==
           DisplayCaptureSourceType::Unknown);
}

void targetSocProfileParsing()
{
    using runtime::display::DisplayTargetSocProfile;
    using runtime::display::parseDisplayTargetSocProfile;

    assert(parseDisplayTargetSocProfile("auto") == DisplayTargetSocProfile::Unknown);
    assert(parseDisplayTargetSocProfile("generic") == DisplayTargetSocProfile::Generic);
    assert(parseDisplayTargetSocProfile("rk3568") ==
           DisplayTargetSocProfile::Rockchip3568);
    assert(parseDisplayTargetSocProfile("rockchip-3588") ==
           DisplayTargetSocProfile::Rockchip3588);
    assert(parseDisplayTargetSocProfile("not-real") ==
           DisplayTargetSocProfile::Unknown);
}

void androidClientIsRenderOnlyForNow()
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::AndroidClient;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(!selected.ok);
    assert(!selected.rejected.empty());
}

void rejectsUnsupportedMemoryType()
{
    runtime::display::DisplayCaptureBackendCapability candidate;
    candidate.adapterId = "test.d3d.only";
    candidate.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    candidate.backend =
        runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication;
    candidate.sourceTypes = {runtime::display::DisplayCaptureSourceType::Monitor};
    candidate.memoryTypes = {runtime::display::DisplayCaptureMemoryType::D3DTexture};
    candidate.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    candidate.priority = 99;

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.acceptedMemoryTypes = {runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    request.candidates = {candidate};

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(!selected.ok);
    assert(selected.rejected.size() == 1);
    assert(selected.rejected.front().reason ==
           "capture backend cannot produce an accepted memory type");
}

void unavailableHighPriorityBackendFallsBackWithReason()
{
    runtime::display::DisplayCaptureBackendCapability dxgi;
    dxgi.adapterId = "test.dxgi";
    dxgi.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    dxgi.backend =
        runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication;
    dxgi.sourceTypes = {runtime::display::DisplayCaptureSourceType::Monitor};
    dxgi.memoryTypes = {runtime::display::DisplayCaptureMemoryType::D3DTexture,
                        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    dxgi.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    dxgi.available = false;
    dxgi.unavailableReason = "probe failed";
    dxgi.priority = 99;

    runtime::display::DisplayCaptureBackendCapability gdi;
    gdi.adapterId = "test.gdi";
    gdi.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    gdi.backend = runtime::display::DisplayCaptureBackendKind::WindowsGdi;
    gdi.sourceTypes = {runtime::display::DisplayCaptureSourceType::Monitor};
    gdi.memoryTypes = {runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    gdi.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    gdi.fallback = true;
    gdi.priority = 1;

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.candidates = {dxgi, gdi};

    const runtime::display::DisplayCaptureBackendSelectionResult selected =
        runtime::display::selectDisplayCaptureBackend(request);

    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGdi);
    assert(selected.fallbackSelected);
    assert(selected.rejected.size() == 1);
    assert(selected.rejected.front().reason ==
           "capture backend is unavailable: probe failed");
}

} // namespace

int main()
{
    windowsMonitorPrefersDxgi();
    windowsWindowSelectsGraphicsCapture();
    requestedWindowsGdiOverridesDxgiPriority();
    requestedMissingBackendFails();
    waylandRequiresPermission();
    rockchipLinuxUsesDrmAdapter();
    rockchipRejectsUnsupportedArchitecture();
    genericLinuxX11AllowsLoongArch();
    linuxEmbeddedAllowsExtendedArchitectures();
    macosPrefersScreenCaptureKit();
    androidAgentUsesMediaProjection();
    rockchipAndroidUsesTaggedMediaProjection();
    harmonyPlatformsExposeSystemCapture();
    targetPlatformParsing();
    sourceTypeParsing();
    targetArchitectureParsing();
    targetSocProfileParsing();
    androidClientIsRenderOnlyForNow();
    rejectsUnsupportedMemoryType();
    unavailableHighPriorityBackendFallsBackWithReason();
    return 0;
}
