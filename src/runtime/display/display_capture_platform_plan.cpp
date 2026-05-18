#include "fusiondesk/runtime/display/display_capture_platform_plan.h"

#include <utility>

#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

std::string defaultMissingFactoryReason(DisplayPlatformFamily platform)
{
    std::string reason = "no concrete display capture factory is registered for ";
    reason += displayPlatformFamilyName(platform);
    return reason;
}

void appendSelectionMessages(DisplayCapturePlatformPlan& plan)
{
    for (const std::string& message : plan.selection.messages)
        plan.messages.push_back(message);

    for (const DisplayCaptureBackendRejection& rejection :
         plan.selection.rejected) {
        std::string message = "display capture backend rejected: ";
        message += rejection.adapterId.empty()
                       ? displayCaptureBackendKindName(rejection.backend)
                       : rejection.adapterId;
        if (!rejection.reason.empty()) {
            message += ": ";
            message += rejection.reason;
        }
        plan.messages.push_back(std::move(message));
    }
}

} // namespace

const char* displayCaptureRuntimeRoleName(DisplayCaptureRuntimeRole role)
{
    switch (role) {
    case DisplayCaptureRuntimeRole::Unknown:
        return "unknown";
    case DisplayCaptureRuntimeRole::Agent:
        return "agent";
    case DisplayCaptureRuntimeRole::Client:
        return "client";
    }
    return "unknown";
}

const char* displayCaptureCapabilitySourceName(
    DisplayCaptureCapabilitySource source)
{
    switch (source) {
    case DisplayCaptureCapabilitySource::None:
        return "none";
    case DisplayCaptureCapabilitySource::ProbedFactory:
        return "probed_factory";
    case DisplayCaptureCapabilitySource::DefaultMatrix:
        return "default_matrix";
    case DisplayCaptureCapabilitySource::UnavailableDefaultMatrix:
        return "unavailable_default_matrix";
    }
    return "none";
}

std::vector<DisplayCaptureMemoryType>
defaultDisplayCaptureAcceptedMemoryTypes(DisplayPlatformFamily platform)
{
    switch (platform) {
    case DisplayPlatformFamily::WindowsDesktop:
        return {DisplayCaptureMemoryType::D3DTexture,
                DisplayCaptureMemoryType::CpuBuffer};
    case DisplayPlatformFamily::LinuxWayland:
    case DisplayPlatformFamily::LinuxEmbedded:
    case DisplayPlatformFamily::RockchipLinux:
        return {DisplayCaptureMemoryType::DmaBuf,
                DisplayCaptureMemoryType::CpuBuffer};
    case DisplayPlatformFamily::MacOS:
        return {DisplayCaptureMemoryType::CVPixelBuffer,
                DisplayCaptureMemoryType::CpuBuffer};
    case DisplayPlatformFamily::AndroidAgent:
    case DisplayPlatformFamily::RockchipAndroid:
        return {DisplayCaptureMemoryType::AndroidHardwareBuffer,
                DisplayCaptureMemoryType::CpuBuffer};
    case DisplayPlatformFamily::Unknown:
    case DisplayPlatformFamily::LinuxX11:
    case DisplayPlatformFamily::AndroidClient:
    case DisplayPlatformFamily::HarmonyOS:
    case DisplayPlatformFamily::OpenHarmony:
        return {DisplayCaptureMemoryType::CpuBuffer};
    }
    return {DisplayCaptureMemoryType::CpuBuffer};
}

std::vector<modules::display::DisplayPixelFormat>
defaultDisplayCaptureAcceptedPixelFormats()
{
    return {modules::display::DisplayPixelFormat::Bgra32};
}

DisplayCaptureBackendSelectionRequest makeDisplayCaptureSelectionRequest(
    const DisplayCapturePlatformPlanRequest& request)
{
    DisplayCaptureBackendSelectionRequest selectionRequest;
    selectionRequest.platform = request.platform;
    selectionRequest.sourceType = request.sourceType;
    selectionRequest.acceptedMemoryTypes =
        request.acceptedMemoryTypes.empty()
            ? defaultDisplayCaptureAcceptedMemoryTypes(request.platform)
            : request.acceptedMemoryTypes;
    selectionRequest.acceptedPixelFormats =
        request.acceptedPixelFormats.empty()
            ? defaultDisplayCaptureAcceptedPixelFormats()
            : request.acceptedPixelFormats;
    selectionRequest.architecture = request.architecture;
    selectionRequest.socProfile = request.socProfile;
    selectionRequest.requestedAdapterId = request.requestedAdapterId;
    selectionRequest.policyAllowsCapture = request.policyAllowsCapture;
    selectionRequest.permissionGranted = request.permissionGranted;
    selectionRequest.serviceSession = request.serviceSession;
    selectionRequest.requireAgentCapture =
        request.role == DisplayCaptureRuntimeRole::Agent;
    selectionRequest.allowFallback = request.allowFallback;
    selectionRequest.preferZeroCopy = request.preferZeroCopy;
    selectionRequest.candidates = request.probedCapabilities;
    return selectionRequest;
}

DisplayCapturePlatformPlan planDisplayCapturePlatform(
    const DisplayCapturePlatformPlanRequest& request)
{
    DisplayCapturePlatformPlan plan;
    plan.selectionRequest = makeDisplayCaptureSelectionRequest(request);

    if (request.role == DisplayCaptureRuntimeRole::Client) {
        plan.ok = true;
        plan.renderOnly = true;
        plan.messages.push_back(
            "display capture is not required for the client role");
        return plan;
    }

    if (request.role == DisplayCaptureRuntimeRole::Unknown) {
        plan.messages.push_back("display capture platform plan requires a role");
        return plan;
    }

    plan.captureRequired = true;
    if (!request.probedCapabilities.empty()) {
        plan.capabilitySource = DisplayCaptureCapabilitySource::ProbedFactory;
    } else if (request.allowDefaultCapabilityMatrix) {
        plan.selectionRequest.candidates =
            defaultDisplayCaptureBackendCapabilities(request.platform);
        plan.capabilitySource = DisplayCaptureCapabilitySource::DefaultMatrix;
    } else {
        const std::string reason =
            request.missingFactoryReason.empty()
                ? defaultMissingFactoryReason(request.platform)
                : request.missingFactoryReason;
        plan.selectionRequest.candidates =
            unavailableDefaultDisplayCaptureBackendCapabilities(request.platform,
                                                               reason);
        plan.capabilitySource =
            DisplayCaptureCapabilitySource::UnavailableDefaultMatrix;
    }

    plan.selection = selectDisplayCaptureBackend(plan.selectionRequest);
    plan.ok = plan.selection.ok;
    appendSelectionMessages(plan);
    if (!plan.ok && plan.messages.empty())
        plan.messages.push_back("display capture platform plan failed");
    return plan;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
