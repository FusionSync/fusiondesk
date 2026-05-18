#include <cassert>
#include <string>
#include <vector>

#include "fusiondesk/runtime/display/display_capture_platform_plan.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCaptureBackendCapability windowsDxgiCapability()
{
    runtime::display::DisplayCaptureBackendCapability capability;
    capability.adapterId = "windows.dxgi.desktop_duplication";
    capability.platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend =
        runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication;
    capability.sourceTypes = {
        runtime::display::DisplayCaptureSourceType::Monitor};
    capability.memoryTypes = {
        runtime::display::DisplayCaptureMemoryType::D3DTexture,
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.zeroCopy = true;
    capability.priority = 90;
    return capability;
}

runtime::display::DisplayCaptureBackendCapability windowsGdiCapability()
{
    runtime::display::DisplayCaptureBackendCapability capability;
    capability.adapterId = "windows.gdi";
    capability.platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend = runtime::display::DisplayCaptureBackendKind::WindowsGdi;
    capability.sourceTypes = {
        runtime::display::DisplayCaptureSourceType::Monitor};
    capability.memoryTypes = {
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.fallback = true;
    capability.priority = 10;
    return capability;
}

void clientRoleIsRenderOnly()
{
    runtime::display::DisplayCapturePlatformPlanRequest request;
    request.role = runtime::display::DisplayCaptureRuntimeRole::Client;
    request.platform = runtime::display::DisplayPlatformFamily::AndroidClient;

    const runtime::display::DisplayCapturePlatformPlan plan =
        runtime::display::planDisplayCapturePlatform(request);

    assert(plan.ok);
    assert(plan.renderOnly);
    assert(!plan.captureRequired);
    assert(plan.capabilitySource ==
           runtime::display::DisplayCaptureCapabilitySource::None);
    assert(std::string(runtime::display::displayCaptureRuntimeRoleName(
               request.role)) == "client");
}

void windowsAgentUsesProbedFactoryCapabilities()
{
    runtime::display::DisplayCapturePlatformPlanRequest request;
    request.role = runtime::display::DisplayCaptureRuntimeRole::Agent;
    request.platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.probedCapabilities = {windowsGdiCapability(), windowsDxgiCapability()};

    const runtime::display::DisplayCapturePlatformPlan plan =
        runtime::display::planDisplayCapturePlatform(request);

    assert(plan.ok);
    assert(plan.captureRequired);
    assert(!plan.renderOnly);
    assert(plan.capabilitySource ==
           runtime::display::DisplayCaptureCapabilitySource::ProbedFactory);
    assert(plan.selection.hasSelection);
    assert(plan.selection.selected.adapterId ==
           "windows.dxgi.desktop_duplication");
    assert(std::string(runtime::display::displayCaptureCapabilitySourceName(
               plan.capabilitySource)) == "probed_factory");
}

void windowsRequestedGdiUsesSameSelectionContract()
{
    runtime::display::DisplayCapturePlatformPlanRequest request;
    request.role = runtime::display::DisplayCaptureRuntimeRole::Agent;
    request.platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.requestedAdapterId = "windows.gdi";
    request.probedCapabilities = {windowsGdiCapability(), windowsDxgiCapability()};

    const runtime::display::DisplayCapturePlatformPlan plan =
        runtime::display::planDisplayCapturePlatform(request);

    assert(plan.ok);
    assert(plan.selection.selected.adapterId == "windows.gdi");
    assert(plan.selection.fallbackSelected);
}

void missingLinuxFactoryDoesNotPretendImplemented()
{
    runtime::display::DisplayCapturePlatformPlanRequest request;
    request.role = runtime::display::DisplayCaptureRuntimeRole::Agent;
    request.platform =
        runtime::display::DisplayPlatformFamily::RockchipLinux;
    request.architecture =
        runtime::display::DisplayTargetArchitecture::Arm64;
    request.socProfile =
        runtime::display::DisplayTargetSocProfile::Rockchip3588;
    request.missingFactoryReason = "rk display adapter target is not linked";

    const runtime::display::DisplayCapturePlatformPlan plan =
        runtime::display::planDisplayCapturePlatform(request);

    assert(!plan.ok);
    assert(plan.captureRequired);
    assert(plan.capabilitySource ==
           runtime::display::DisplayCaptureCapabilitySource::UnavailableDefaultMatrix);
    assert(!plan.selectionRequest.candidates.empty());
    assert(!plan.selection.rejected.empty());

    bool sawMissingFactoryReason = false;
    for (const std::string& message : plan.messages) {
        if (message.find("rk display adapter target is not linked") !=
            std::string::npos) {
            sawMissingFactoryReason = true;
        }
    }
    assert(sawMissingFactoryReason);
}

void defaultMatrixModeDocumentsFutureMacosSelection()
{
    runtime::display::DisplayCapturePlatformPlanRequest request;
    request.role = runtime::display::DisplayCaptureRuntimeRole::Agent;
    request.platform = runtime::display::DisplayPlatformFamily::MacOS;
    request.allowDefaultCapabilityMatrix = true;

    const runtime::display::DisplayCapturePlatformPlan plan =
        runtime::display::planDisplayCapturePlatform(request);

    assert(plan.ok);
    assert(plan.capabilitySource ==
           runtime::display::DisplayCaptureCapabilitySource::DefaultMatrix);
    assert(plan.selection.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::MacOSScreenCaptureKit);
    assert(plan.selection.selected.zeroCopy);
}

void defaultAcceptedMemoryTypesFollowPlatform()
{
    using runtime::display::DisplayCaptureMemoryType;
    using runtime::display::DisplayPlatformFamily;
    using runtime::display::defaultDisplayCaptureAcceptedMemoryTypes;

    const std::vector<DisplayCaptureMemoryType> windows =
        defaultDisplayCaptureAcceptedMemoryTypes(
            DisplayPlatformFamily::WindowsDesktop);
    assert(windows.size() == 2);
    assert(windows.front() == DisplayCaptureMemoryType::D3DTexture);

    const std::vector<DisplayCaptureMemoryType> rk =
        defaultDisplayCaptureAcceptedMemoryTypes(
            DisplayPlatformFamily::RockchipLinux);
    assert(rk.size() == 2);
    assert(rk.front() == DisplayCaptureMemoryType::DmaBuf);

    const std::vector<DisplayCaptureMemoryType> android =
        defaultDisplayCaptureAcceptedMemoryTypes(
            DisplayPlatformFamily::AndroidAgent);
    assert(android.size() == 2);
    assert(android.front() == DisplayCaptureMemoryType::AndroidHardwareBuffer);
}

} // namespace

int main()
{
    clientRoleIsRenderOnly();
    windowsAgentUsesProbedFactoryCapabilities();
    windowsRequestedGdiUsesSameSelectionContract();
    missingLinuxFactoryDoesNotPretendImplemented();
    defaultMatrixModeDocumentsFutureMacosSelection();
    defaultAcceptedMemoryTypesFollowPlatform();
    return 0;
}
