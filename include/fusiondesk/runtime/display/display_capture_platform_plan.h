#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_PLATFORM_PLAN_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_PLATFORM_PLAN_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/display/display_capture_backend_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

enum class DisplayCaptureRuntimeRole
{
    Unknown = 0,
    Agent = 1,
    Client = 2
};

enum class DisplayCaptureCapabilitySource
{
    None = 0,
    ProbedFactory = 1,
    DefaultMatrix = 2,
    UnavailableDefaultMatrix = 3
};

struct DisplayCapturePlatformPlanRequest
{
    DisplayCaptureRuntimeRole role = DisplayCaptureRuntimeRole::Agent;
    DisplayPlatformFamily platform = DisplayPlatformFamily::Unknown;
    DisplayCaptureSourceType sourceType = DisplayCaptureSourceType::Monitor;
    DisplayTargetArchitecture architecture = DisplayTargetArchitecture::Unknown;
    DisplayTargetSocProfile socProfile = DisplayTargetSocProfile::Unknown;
    std::string requestedAdapterId;
    bool policyAllowsCapture = true;
    bool permissionGranted = true;
    bool serviceSession = false;
    bool allowFallback = true;
    bool preferZeroCopy = true;
    bool allowDefaultCapabilityMatrix = false;
    std::string missingFactoryReason;
    std::vector<DisplayCaptureMemoryType> acceptedMemoryTypes;
    std::vector<modules::display::DisplayPixelFormat> acceptedPixelFormats;
    std::vector<DisplayCaptureBackendCapability> probedCapabilities;
};

struct DisplayCapturePlatformPlan
{
    bool ok = false;
    bool captureRequired = false;
    bool renderOnly = false;
    DisplayCaptureCapabilitySource capabilitySource =
        DisplayCaptureCapabilitySource::None;
    DisplayCaptureBackendSelectionRequest selectionRequest;
    DisplayCaptureBackendSelectionResult selection;
    std::vector<std::string> messages;
};

const char* displayCaptureRuntimeRoleName(DisplayCaptureRuntimeRole role);
const char* displayCaptureCapabilitySourceName(
    DisplayCaptureCapabilitySource source);

std::vector<DisplayCaptureMemoryType>
defaultDisplayCaptureAcceptedMemoryTypes(DisplayPlatformFamily platform);

std::vector<modules::display::DisplayPixelFormat>
defaultDisplayCaptureAcceptedPixelFormats();

DisplayCaptureBackendSelectionRequest makeDisplayCaptureSelectionRequest(
    const DisplayCapturePlatformPlanRequest& request);

DisplayCapturePlatformPlan planDisplayCapturePlatform(
    const DisplayCapturePlatformPlanRequest& request);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_PLATFORM_PLAN_H
