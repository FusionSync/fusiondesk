#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_SELECTION_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_SELECTION_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace runtime {
namespace display {

enum class DisplayPlatformFamily : std::uint16_t
{
    Unknown = 0,
    WindowsDesktop = 1,
    LinuxX11 = 2,
    LinuxWayland = 3,
    LinuxEmbedded = 4,
    MacOS = 5,
    AndroidClient = 6,
    AndroidAgent = 7,
    HarmonyOS = 8,
    OpenHarmony = 9,
    RockchipLinux = 10,
    RockchipAndroid = 11
};

enum class DisplayCaptureBackendKind : std::uint16_t
{
    Unknown = 0,
    WindowsGdi = 1,
    WindowsDxgiDesktopDuplication = 2,
    WindowsGraphicsCapture = 3,
    LinuxXDamageXShm = 4,
    LinuxPipeWirePortal = 5,
    LinuxDrmKmsGbm = 6,
    MacOSScreenCaptureKit = 7,
    MacOSQuartz = 8,
    AndroidMediaProjection = 9,
    HarmonySystemCapture = 10,
    SoftwareFallback = 11
};

using DisplayCaptureSourceType =
    modules::display::DisplayCaptureSourceType;

enum class DisplayCaptureMemoryType : std::uint16_t
{
    Unknown = 0,
    CpuBuffer = 1,
    D3DTexture = 2,
    DmaBuf = 3,
    CVPixelBuffer = 4,
    AndroidHardwareBuffer = 5
};

enum class DisplayTargetArchitecture : std::uint16_t
{
    Unknown = 0,
    X86 = 1,
    X86_64 = 2,
    Arm32 = 3,
    Arm64 = 4,
    LoongArch64 = 5,
    Mips64El = 6
};

enum class DisplayTargetSocProfile : std::uint16_t
{
    Unknown = 0,
    Generic = 1,
    Rockchip3568 = 2,
    Rockchip3588 = 3
};

enum class DisplayCaptureDiagnosticKind : std::uint16_t
{
    Unknown = 0,
    AccessDenied = 1,
    PermissionRevoked = 2,
    ProtectedContent = 3,
    SourceNotFound = 4,
    SourceHotplug = 5,
    GeometryOrFormatChanged = 6,
    DeviceLost = 7,
    SessionModeUnsupported = 8,
    FrameTimeout = 9,
    RecoverableAdapterReset = 10,
    UnrecoverableAdapterFailure = 11
};

struct DisplayCaptureBackendCapability
{
    std::string adapterId;
    DisplayPlatformFamily platform = DisplayPlatformFamily::Unknown;
    DisplayCaptureBackendKind backend = DisplayCaptureBackendKind::Unknown;
    std::vector<DisplayCaptureSourceType> sourceTypes;
    std::vector<modules::display::DisplayPixelFormat> pixelFormats;
    std::vector<DisplayCaptureMemoryType> memoryTypes;
    std::vector<DisplayTargetArchitecture> architectures;
    std::vector<DisplayTargetSocProfile> socProfiles;
    std::vector<DisplayCaptureDiagnosticKind> diagnostics;
    bool available = true;
    std::string unavailableReason;
    bool fallback = false;
    bool zeroCopy = false;
    bool requiresUserConsent = false;
    bool supportsServiceSession = true;
    bool agentCaptureSupported = true;
    bool supportsDirtyRegions = false;
    bool supportsCursorCapture = false;
    bool supportsHotplugNotifications = false;
    bool protectedContentAware = false;
    int priority = 0;
};

struct DisplayCaptureBackendSelectionRequest
{
    DisplayPlatformFamily platform = DisplayPlatformFamily::Unknown;
    DisplayCaptureSourceType sourceType = DisplayCaptureSourceType::Monitor;
    std::vector<DisplayCaptureMemoryType> acceptedMemoryTypes = {
        DisplayCaptureMemoryType::CpuBuffer};
    std::vector<modules::display::DisplayPixelFormat> acceptedPixelFormats = {
        modules::display::DisplayPixelFormat::Bgra32};
    DisplayTargetArchitecture architecture = DisplayTargetArchitecture::Unknown;
    DisplayTargetSocProfile socProfile = DisplayTargetSocProfile::Unknown;
    std::string requestedAdapterId;
    bool policyAllowsCapture = true;
    bool permissionGranted = true;
    bool serviceSession = false;
    bool requireAgentCapture = true;
    bool allowFallback = true;
    bool preferZeroCopy = true;
    std::vector<DisplayCaptureBackendCapability> candidates;
};

struct DisplayCaptureBackendRejection
{
    std::string adapterId;
    DisplayCaptureBackendKind backend = DisplayCaptureBackendKind::Unknown;
    std::string reason;
};

struct DisplayCaptureBackendSelectionResult
{
    bool ok = false;
    bool hasSelection = false;
    DisplayCaptureBackendCapability selected;
    bool fallbackSelected = false;
    int score = 0;
    std::vector<DisplayCaptureBackendRejection> rejected;
    std::vector<std::string> messages;
};

const char* displayPlatformFamilyName(DisplayPlatformFamily platform);
const char* displayCaptureBackendKindName(DisplayCaptureBackendKind backend);
const char* displayCaptureSourceTypeName(DisplayCaptureSourceType sourceType);
const char* displayCaptureMemoryTypeName(DisplayCaptureMemoryType memoryType);
const char* displayTargetArchitectureName(DisplayTargetArchitecture architecture);
const char* displayTargetSocProfileName(DisplayTargetSocProfile socProfile);
DisplayCaptureSourceType parseDisplayCaptureSourceType(const std::string& value);
DisplayPlatformFamily parseDisplayPlatformFamily(const std::string& value);
DisplayTargetArchitecture parseDisplayTargetArchitecture(const std::string& value);
DisplayTargetSocProfile parseDisplayTargetSocProfile(const std::string& value);

std::vector<DisplayCaptureBackendCapability>
defaultDisplayCaptureBackendCapabilities(DisplayPlatformFamily platform);

DisplayCaptureBackendSelectionResult selectDisplayCaptureBackend(
    const DisplayCaptureBackendSelectionRequest& request);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_SELECTION_H
