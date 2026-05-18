#include "fusiondesk/runtime/display/display_capture_backend_selection.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

template <typename T>
bool contains(const std::vector<T>& values, T value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
bool intersects(const std::vector<T>& lhs, const std::vector<T>& rhs)
{
    if (lhs.empty() || rhs.empty())
        return true;

    for (T value : lhs) {
        if (contains(rhs, value))
            return true;
    }
    return false;
}

std::string normalizedIdentifier(std::string value)
{
    for (char& ch : value) {
        const unsigned char raw = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::tolower(raw));
        if (ch == '-' || ch == ' ')
            ch = '_';
    }
    return value;
}

DisplayCaptureBackendCapability capability(
    std::string adapterId,
    DisplayPlatformFamily platform,
    DisplayCaptureBackendKind backend,
    std::vector<DisplayCaptureSourceType> sourceTypes,
    std::vector<DisplayCaptureMemoryType> memoryTypes,
    int priority)
{
    DisplayCaptureBackendCapability result;
    result.adapterId = std::move(adapterId);
    result.platform = platform;
    result.backend = backend;
    result.sourceTypes = std::move(sourceTypes);
    result.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    result.memoryTypes = std::move(memoryTypes);
    result.priority = priority;
    return result;
}

void reject(DisplayCaptureBackendSelectionResult& result,
            const DisplayCaptureBackendCapability& candidate,
            std::string reason)
{
    DisplayCaptureBackendRejection rejection;
    rejection.adapterId = candidate.adapterId;
    rejection.backend = candidate.backend;
    rejection.reason = std::move(reason);
    result.rejected.push_back(std::move(rejection));
}

void fail(DisplayCaptureBackendSelectionResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

int scoreCandidate(const DisplayCaptureBackendCapability& candidate,
                   const DisplayCaptureBackendSelectionRequest& request)
{
    int score = candidate.priority;
    if (request.preferZeroCopy && candidate.zeroCopy)
        score += 20;
    if (!candidate.fallback)
        score += 5;
    if (candidate.supportsHotplugNotifications)
        score += 3;
    if (candidate.supportsDirtyRegions)
        score += 2;
    if (candidate.supportsCursorCapture)
        score += 1;
    return score;
}

bool candidateUsable(const DisplayCaptureBackendCapability& candidate,
                     const DisplayCaptureBackendSelectionRequest& request,
                     DisplayCaptureBackendSelectionResult& result)
{
    if (!request.requestedAdapterId.empty() &&
        candidate.adapterId != request.requestedAdapterId) {
        reject(result, candidate, "capture backend does not match requested adapter id");
        return false;
    }
    if (!candidate.available) {
        std::string reason = "capture backend is unavailable";
        if (!candidate.unavailableReason.empty())
            reason += ": " + candidate.unavailableReason;
        reject(result, candidate, std::move(reason));
        return false;
    }
    if (candidate.platform != request.platform) {
        reject(result, candidate, "capture backend platform does not match request");
        return false;
    }
    if (request.requireAgentCapture && !candidate.agentCaptureSupported) {
        reject(result, candidate, "capture backend does not support agent capture");
        return false;
    }
    if (candidate.fallback && !request.allowFallback) {
        reject(result, candidate, "capture fallback backends are disabled");
        return false;
    }
    if (!contains(candidate.sourceTypes, request.sourceType)) {
        reject(result, candidate, "capture backend does not support requested source type");
        return false;
    }
    if (candidate.requiresUserConsent && !request.permissionGranted) {
        reject(result, candidate, "capture backend requires a granted user permission");
        return false;
    }
    if (request.serviceSession && !candidate.supportsServiceSession) {
        reject(result, candidate, "capture backend does not support service session capture");
        return false;
    }
    if (!intersects(candidate.memoryTypes, request.acceptedMemoryTypes)) {
        reject(result, candidate, "capture backend cannot produce an accepted memory type");
        return false;
    }
    if (!intersects(candidate.pixelFormats, request.acceptedPixelFormats)) {
        reject(result, candidate, "capture backend cannot produce an accepted pixel format");
        return false;
    }
    if (!candidate.architectures.empty() &&
        request.architecture != DisplayTargetArchitecture::Unknown &&
        !contains(candidate.architectures, request.architecture)) {
        reject(result, candidate, "capture backend does not support requested architecture");
        return false;
    }
    if (!candidate.socProfiles.empty() &&
        request.socProfile != DisplayTargetSocProfile::Unknown &&
        request.socProfile != DisplayTargetSocProfile::Generic &&
        !contains(candidate.socProfiles, request.socProfile)) {
        reject(result, candidate, "capture backend does not support requested SoC profile");
        return false;
    }
    return true;
}

} // namespace

const char* displayPlatformFamilyName(DisplayPlatformFamily platform)
{
    switch (platform) {
    case DisplayPlatformFamily::Unknown:
        return "unknown";
    case DisplayPlatformFamily::WindowsDesktop:
        return "windows_desktop";
    case DisplayPlatformFamily::LinuxX11:
        return "linux_x11";
    case DisplayPlatformFamily::LinuxWayland:
        return "linux_wayland";
    case DisplayPlatformFamily::LinuxEmbedded:
        return "linux_embedded";
    case DisplayPlatformFamily::MacOS:
        return "macos";
    case DisplayPlatformFamily::AndroidClient:
        return "android_client";
    case DisplayPlatformFamily::AndroidAgent:
        return "android_agent";
    case DisplayPlatformFamily::HarmonyOS:
        return "harmonyos";
    case DisplayPlatformFamily::OpenHarmony:
        return "openharmony";
    case DisplayPlatformFamily::RockchipLinux:
        return "rockchip_linux";
    case DisplayPlatformFamily::RockchipAndroid:
        return "rockchip_android";
    }
    return "unknown";
}

const char* displayCaptureBackendKindName(DisplayCaptureBackendKind backend)
{
    switch (backend) {
    case DisplayCaptureBackendKind::Unknown:
        return "unknown";
    case DisplayCaptureBackendKind::WindowsGdi:
        return "windows.gdi";
    case DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication:
        return "windows.dxgi.desktop_duplication";
    case DisplayCaptureBackendKind::WindowsGraphicsCapture:
        return "windows.graphics_capture";
    case DisplayCaptureBackendKind::LinuxXDamageXShm:
        return "linux.xdamage_xshm";
    case DisplayCaptureBackendKind::LinuxPipeWirePortal:
        return "linux.pipewire_portal";
    case DisplayCaptureBackendKind::LinuxDrmKmsGbm:
        return "linux.drm_kms_gbm";
    case DisplayCaptureBackendKind::MacOSScreenCaptureKit:
        return "macos.screen_capture_kit";
    case DisplayCaptureBackendKind::MacOSQuartz:
        return "macos.quartz";
    case DisplayCaptureBackendKind::AndroidMediaProjection:
        return "android.media_projection";
    case DisplayCaptureBackendKind::HarmonySystemCapture:
        return "harmony.system_capture";
    case DisplayCaptureBackendKind::SoftwareFallback:
        return "software.fallback";
    }
    return "unknown";
}

const char* displayCaptureSourceTypeName(DisplayCaptureSourceType sourceType)
{
    switch (sourceType) {
    case DisplayCaptureSourceType::Unknown:
        return "unknown";
    case DisplayCaptureSourceType::Monitor:
        return "monitor";
    case DisplayCaptureSourceType::Window:
        return "window";
    case DisplayCaptureSourceType::VirtualDisplay:
        return "virtual_display";
    case DisplayCaptureSourceType::MobileProjection:
        return "mobile_projection";
    }
    return "unknown";
}

DisplayCaptureSourceType parseDisplayCaptureSourceType(const std::string& value)
{
    const std::string normalized = normalizedIdentifier(value);
    if (normalized.empty() || normalized == "auto" || normalized == "unknown")
        return DisplayCaptureSourceType::Unknown;
    if (normalized == "monitor" || normalized == "screen" ||
        normalized == "display" || normalized == "desktop")
        return DisplayCaptureSourceType::Monitor;
    if (normalized == "window" || normalized == "app" ||
        normalized == "application" || normalized == "window_capture")
        return DisplayCaptureSourceType::Window;
    if (normalized == "virtual" || normalized == "virtual_display" ||
        normalized == "vdisplay")
        return DisplayCaptureSourceType::VirtualDisplay;
    if (normalized == "mobile" || normalized == "mobile_projection" ||
        normalized == "projection" || normalized == "media_projection" ||
        normalized == "android_projection")
        return DisplayCaptureSourceType::MobileProjection;
    return DisplayCaptureSourceType::Unknown;
}

const char* displayCaptureMemoryTypeName(DisplayCaptureMemoryType memoryType)
{
    switch (memoryType) {
    case DisplayCaptureMemoryType::Unknown:
        return "unknown";
    case DisplayCaptureMemoryType::CpuBuffer:
        return "cpu_buffer";
    case DisplayCaptureMemoryType::D3DTexture:
        return "d3d_texture";
    case DisplayCaptureMemoryType::DmaBuf:
        return "dma_buf";
    case DisplayCaptureMemoryType::CVPixelBuffer:
        return "cv_pixel_buffer";
    case DisplayCaptureMemoryType::AndroidHardwareBuffer:
        return "android_hardware_buffer";
    }
    return "unknown";
}

const char* displayTargetArchitectureName(DisplayTargetArchitecture architecture)
{
    switch (architecture) {
    case DisplayTargetArchitecture::Unknown:
        return "unknown";
    case DisplayTargetArchitecture::X86:
        return "x86";
    case DisplayTargetArchitecture::X86_64:
        return "x86_64";
    case DisplayTargetArchitecture::Arm32:
        return "arm32";
    case DisplayTargetArchitecture::Arm64:
        return "arm64";
    case DisplayTargetArchitecture::LoongArch64:
        return "loongarch64";
    case DisplayTargetArchitecture::Mips64El:
        return "mips64el";
    }
    return "unknown";
}

const char* displayTargetSocProfileName(DisplayTargetSocProfile socProfile)
{
    switch (socProfile) {
    case DisplayTargetSocProfile::Unknown:
        return "unknown";
    case DisplayTargetSocProfile::Generic:
        return "generic";
    case DisplayTargetSocProfile::Rockchip3568:
        return "rk3568";
    case DisplayTargetSocProfile::Rockchip3588:
        return "rk3588";
    }
    return "unknown";
}

DisplayPlatformFamily parseDisplayPlatformFamily(const std::string& value)
{
    const std::string normalized = normalizedIdentifier(value);
    if (normalized.empty() || normalized == "auto" || normalized == "unknown")
        return DisplayPlatformFamily::Unknown;
    if (normalized == "windows" || normalized == "win" ||
        normalized == "win32" || normalized == "win64" ||
        normalized == "windows_desktop" || normalized == "windows_pc")
        return DisplayPlatformFamily::WindowsDesktop;
    if (normalized == "linux_x11" || normalized == "x11" ||
        normalized == "linux_desktop_x11")
        return DisplayPlatformFamily::LinuxX11;
    if (normalized == "linux_wayland" || normalized == "wayland" ||
        normalized == "linux_desktop_wayland")
        return DisplayPlatformFamily::LinuxWayland;
    if (normalized == "linux_embedded" || normalized == "embedded_linux" ||
        normalized == "drm_kms" || normalized == "kms" ||
        normalized == "gbm")
        return DisplayPlatformFamily::LinuxEmbedded;
    if (normalized == "macos" || normalized == "mac" ||
        normalized == "darwin" || normalized == "osx")
        return DisplayPlatformFamily::MacOS;
    if (normalized == "android_client" || normalized == "android_controller")
        return DisplayPlatformFamily::AndroidClient;
    if (normalized == "android" || normalized == "android_agent")
        return DisplayPlatformFamily::AndroidAgent;
    if (normalized == "harmonyos" || normalized == "harmony" ||
        normalized == "hmos")
        return DisplayPlatformFamily::HarmonyOS;
    if (normalized == "openharmony" || normalized == "open_harmony" ||
        normalized == "ohos")
        return DisplayPlatformFamily::OpenHarmony;
    if (normalized == "rockchip_linux" || normalized == "rk_linux" ||
        normalized == "rk3568_linux" || normalized == "rk3588_linux")
        return DisplayPlatformFamily::RockchipLinux;
    if (normalized == "rockchip_android" || normalized == "rk_android" ||
        normalized == "rk3568_android" || normalized == "rk3588_android")
        return DisplayPlatformFamily::RockchipAndroid;
    return DisplayPlatformFamily::Unknown;
}

DisplayTargetArchitecture parseDisplayTargetArchitecture(const std::string& value)
{
    const std::string normalized = normalizedIdentifier(value);
    if (normalized.empty() || normalized == "auto" || normalized == "unknown")
        return DisplayTargetArchitecture::Unknown;
    if (normalized == "x86" || normalized == "i386" ||
        normalized == "i686")
        return DisplayTargetArchitecture::X86;
    if (normalized == "x86_64" || normalized == "x64" ||
        normalized == "amd64")
        return DisplayTargetArchitecture::X86_64;
    if (normalized == "arm" || normalized == "arm32" ||
        normalized == "armv7" || normalized == "armeabi_v7a")
        return DisplayTargetArchitecture::Arm32;
    if (normalized == "arm64" || normalized == "aarch64" ||
        normalized == "arm64_v8a")
        return DisplayTargetArchitecture::Arm64;
    if (normalized == "loongarch64" || normalized == "loong64")
        return DisplayTargetArchitecture::LoongArch64;
    if (normalized == "mips64el" || normalized == "mips64le")
        return DisplayTargetArchitecture::Mips64El;
    return DisplayTargetArchitecture::Unknown;
}

DisplayTargetSocProfile parseDisplayTargetSocProfile(const std::string& value)
{
    const std::string normalized = normalizedIdentifier(value);
    if (normalized.empty() || normalized == "auto" || normalized == "unknown")
        return DisplayTargetSocProfile::Unknown;
    if (normalized == "generic")
        return DisplayTargetSocProfile::Generic;
    if (normalized == "rk3568" || normalized == "rockchip3568" ||
        normalized == "rockchip_3568" || normalized == "3568")
        return DisplayTargetSocProfile::Rockchip3568;
    if (normalized == "rk3588" || normalized == "rockchip3588" ||
        normalized == "rockchip_3588" || normalized == "3588")
        return DisplayTargetSocProfile::Rockchip3588;
    return DisplayTargetSocProfile::Unknown;
}

std::vector<DisplayCaptureBackendCapability>
defaultDisplayCaptureBackendCapabilities(DisplayPlatformFamily platform)
{
    std::vector<DisplayCaptureBackendCapability> result;

    if (platform == DisplayPlatformFamily::WindowsDesktop) {
        DisplayCaptureBackendCapability dxgi = capability(
            "windows.dxgi.desktop_duplication",
            platform,
            DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
            {DisplayCaptureSourceType::Monitor},
            {DisplayCaptureMemoryType::D3DTexture, DisplayCaptureMemoryType::CpuBuffer},
            90);
        dxgi.zeroCopy = true;
        dxgi.supportsHotplugNotifications = true;
        dxgi.supportsCursorCapture = true;
        dxgi.protectedContentAware = true;
        dxgi.diagnostics = {DisplayCaptureDiagnosticKind::DeviceLost,
                            DisplayCaptureDiagnosticKind::SourceHotplug,
                            DisplayCaptureDiagnosticKind::GeometryOrFormatChanged,
                            DisplayCaptureDiagnosticKind::ProtectedContent};
        result.push_back(dxgi);

        DisplayCaptureBackendCapability wgc = capability(
            "windows.graphics_capture",
            platform,
            DisplayCaptureBackendKind::WindowsGraphicsCapture,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::Window},
            {DisplayCaptureMemoryType::D3DTexture, DisplayCaptureMemoryType::CpuBuffer},
            85);
        wgc.zeroCopy = true;
        wgc.supportsServiceSession = false;
        wgc.supportsHotplugNotifications = true;
        wgc.protectedContentAware = true;
        wgc.diagnostics = {DisplayCaptureDiagnosticKind::AccessDenied,
                           DisplayCaptureDiagnosticKind::PermissionRevoked,
                           DisplayCaptureDiagnosticKind::ProtectedContent,
                           DisplayCaptureDiagnosticKind::SourceHotplug};
        result.push_back(wgc);

        DisplayCaptureBackendCapability gdi = capability(
            "windows.gdi",
            platform,
            DisplayCaptureBackendKind::WindowsGdi,
            {DisplayCaptureSourceType::Monitor},
            {DisplayCaptureMemoryType::CpuBuffer},
            10);
        gdi.fallback = true;
        result.push_back(gdi);
        return result;
    }

    if (platform == DisplayPlatformFamily::LinuxX11) {
        DisplayCaptureBackendCapability xdamage = capability(
            "linux.xdamage_xshm",
            platform,
            DisplayCaptureBackendKind::LinuxXDamageXShm,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::Window},
            {DisplayCaptureMemoryType::CpuBuffer},
            80);
        xdamage.supportsDirtyRegions = true;
        xdamage.supportsHotplugNotifications = true;
        result.push_back(xdamage);

        DisplayCaptureBackendCapability software = capability(
            "linux.x11.full_frame",
            platform,
            DisplayCaptureBackendKind::SoftwareFallback,
            {DisplayCaptureSourceType::Monitor},
            {DisplayCaptureMemoryType::CpuBuffer},
            5);
        software.fallback = true;
        result.push_back(software);
        return result;
    }

    if (platform == DisplayPlatformFamily::LinuxWayland) {
        DisplayCaptureBackendCapability pipewire = capability(
            "linux.pipewire_portal",
            platform,
            DisplayCaptureBackendKind::LinuxPipeWirePortal,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::Window},
            {DisplayCaptureMemoryType::DmaBuf, DisplayCaptureMemoryType::CpuBuffer},
            80);
        pipewire.zeroCopy = true;
        pipewire.requiresUserConsent = true;
        pipewire.supportsServiceSession = false;
        pipewire.supportsHotplugNotifications = true;
        pipewire.diagnostics = {DisplayCaptureDiagnosticKind::PermissionRevoked,
                                DisplayCaptureDiagnosticKind::SourceNotFound,
                                DisplayCaptureDiagnosticKind::FrameTimeout};
        result.push_back(pipewire);
        return result;
    }

    if (platform == DisplayPlatformFamily::LinuxEmbedded ||
        platform == DisplayPlatformFamily::RockchipLinux) {
        DisplayCaptureBackendCapability drm = capability(
            platform == DisplayPlatformFamily::RockchipLinux
                ? "linux.rockchip.drm_kms_gbm"
                : "linux.drm_kms_gbm",
            platform,
            DisplayCaptureBackendKind::LinuxDrmKmsGbm,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::VirtualDisplay},
            {DisplayCaptureMemoryType::DmaBuf, DisplayCaptureMemoryType::CpuBuffer},
            platform == DisplayPlatformFamily::RockchipLinux ? 85 : 80);
        drm.zeroCopy = true;
        if (platform == DisplayPlatformFamily::RockchipLinux) {
            drm.architectures = {DisplayTargetArchitecture::Arm64};
            drm.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                               DisplayTargetSocProfile::Rockchip3588};
        }
        drm.supportsHotplugNotifications = true;
        drm.diagnostics = {DisplayCaptureDiagnosticKind::AccessDenied,
                           DisplayCaptureDiagnosticKind::DeviceLost,
                           DisplayCaptureDiagnosticKind::GeometryOrFormatChanged};
        result.push_back(drm);

        DisplayCaptureBackendCapability software = capability(
            platform == DisplayPlatformFamily::RockchipLinux
                ? "linux.rockchip.cpu_fallback"
                : "linux.embedded.cpu_fallback",
            platform,
            DisplayCaptureBackendKind::SoftwareFallback,
            {DisplayCaptureSourceType::Monitor},
            {DisplayCaptureMemoryType::CpuBuffer},
            5);
        software.fallback = true;
        if (platform == DisplayPlatformFamily::RockchipLinux) {
            software.architectures = {DisplayTargetArchitecture::Arm64};
            software.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                                    DisplayTargetSocProfile::Rockchip3588};
        }
        result.push_back(software);
        return result;
    }

    if (platform == DisplayPlatformFamily::MacOS) {
        DisplayCaptureBackendCapability screenCaptureKit = capability(
            "macos.screen_capture_kit",
            platform,
            DisplayCaptureBackendKind::MacOSScreenCaptureKit,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::Window},
            {DisplayCaptureMemoryType::CVPixelBuffer, DisplayCaptureMemoryType::CpuBuffer},
            90);
        screenCaptureKit.zeroCopy = true;
        screenCaptureKit.requiresUserConsent = true;
        screenCaptureKit.supportsServiceSession = false;
        screenCaptureKit.protectedContentAware = true;
        result.push_back(screenCaptureKit);

        DisplayCaptureBackendCapability quartz = capability(
            "macos.quartz",
            platform,
            DisplayCaptureBackendKind::MacOSQuartz,
            {DisplayCaptureSourceType::Monitor},
            {DisplayCaptureMemoryType::CpuBuffer},
            25);
        quartz.fallback = true;
        quartz.requiresUserConsent = true;
        quartz.supportsServiceSession = false;
        result.push_back(quartz);
        return result;
    }

    if (platform == DisplayPlatformFamily::AndroidAgent ||
        platform == DisplayPlatformFamily::RockchipAndroid) {
        DisplayCaptureBackendCapability mediaProjection = capability(
            platform == DisplayPlatformFamily::RockchipAndroid
                ? "android.rockchip.media_projection"
                : "android.media_projection",
            platform,
            DisplayCaptureBackendKind::AndroidMediaProjection,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::MobileProjection},
            {DisplayCaptureMemoryType::AndroidHardwareBuffer,
             DisplayCaptureMemoryType::CpuBuffer},
            platform == DisplayPlatformFamily::RockchipAndroid ? 85 : 80);
        mediaProjection.zeroCopy = true;
        if (platform == DisplayPlatformFamily::RockchipAndroid) {
            mediaProjection.architectures = {DisplayTargetArchitecture::Arm64};
            mediaProjection.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                                           DisplayTargetSocProfile::Rockchip3588};
        }
        mediaProjection.requiresUserConsent = true;
        mediaProjection.supportsServiceSession = false;
        mediaProjection.diagnostics = {DisplayCaptureDiagnosticKind::PermissionRevoked,
                                       DisplayCaptureDiagnosticKind::SourceNotFound,
                                       DisplayCaptureDiagnosticKind::FrameTimeout};
        result.push_back(mediaProjection);
        return result;
    }

    if (platform == DisplayPlatformFamily::AndroidClient) {
        DisplayCaptureBackendCapability clientOnly = capability(
            "android.client.render_only",
            platform,
            DisplayCaptureBackendKind::Unknown,
            {},
            {},
            0);
        clientOnly.agentCaptureSupported = false;
        result.push_back(clientOnly);
        return result;
    }

    if (platform == DisplayPlatformFamily::HarmonyOS ||
        platform == DisplayPlatformFamily::OpenHarmony) {
        DisplayCaptureBackendCapability harmony = capability(
            platform == DisplayPlatformFamily::HarmonyOS
                ? "harmonyos.system_capture"
                : "openharmony.system_capture",
            platform,
            DisplayCaptureBackendKind::HarmonySystemCapture,
            {DisplayCaptureSourceType::Monitor, DisplayCaptureSourceType::MobileProjection},
            {DisplayCaptureMemoryType::CpuBuffer},
            50);
        harmony.requiresUserConsent = true;
        harmony.supportsServiceSession = false;
        harmony.diagnostics = {DisplayCaptureDiagnosticKind::AccessDenied,
                               DisplayCaptureDiagnosticKind::PermissionRevoked,
                               DisplayCaptureDiagnosticKind::SessionModeUnsupported};
        result.push_back(harmony);
        return result;
    }

    return result;
}

DisplayCaptureBackendSelectionResult selectDisplayCaptureBackend(
    const DisplayCaptureBackendSelectionRequest& request)
{
    DisplayCaptureBackendSelectionResult result;

    if (request.platform == DisplayPlatformFamily::Unknown)
        fail(result, "display capture backend selection requires a platform");
    if (request.sourceType == DisplayCaptureSourceType::Unknown)
        fail(result, "display capture backend selection requires a source type");
    if (!request.policyAllowsCapture)
        fail(result, "display capture is blocked by policy");

    std::vector<DisplayCaptureBackendCapability> candidates = request.candidates;
    if (candidates.empty())
        candidates = defaultDisplayCaptureBackendCapabilities(request.platform);
    if (candidates.empty())
        fail(result, "display capture backend selection has no candidates");
    if (!result.messages.empty())
        return result;

    bool foundUsable = false;
    int bestScore = 0;
    DisplayCaptureBackendCapability bestCandidate;
    for (const DisplayCaptureBackendCapability& candidate : candidates) {
        if (!candidateUsable(candidate, request, result))
            continue;

        const int score = scoreCandidate(candidate, request);
        if (!foundUsable || score > bestScore) {
            foundUsable = true;
            bestScore = score;
            bestCandidate = candidate;
        }
    }

    if (!foundUsable) {
        fail(result, "display capture backend selection found no usable backend");
        return result;
    }

    result.ok = true;
    result.hasSelection = true;
    result.selected = bestCandidate;
    result.fallbackSelected = bestCandidate.fallback;
    result.score = bestScore;
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
