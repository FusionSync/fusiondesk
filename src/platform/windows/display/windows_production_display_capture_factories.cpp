#include "fusiondesk/platform/windows/display/windows_production_display_capture_factories.h"

#include "fusiondesk/platform/windows/display/windows_dxgi_desktop_duplication_capture.h"
#include "fusiondesk/platform/windows/display/windows_graphics_capture.h"

#include <cstdlib>
#include <memory>
#include <string>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

namespace {

const char* kDxgiEnableEnvironment = "FUSIONDESK_ENABLE_DXGI_CAPTURE";
const char* kWgcEnableEnvironment = "FUSIONDESK_ENABLE_WGC_CAPTURE";

enum class EnvironmentFlag
{
    Unset,
    Enabled,
    Disabled
};

EnvironmentFlag readEnvironmentFlag(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
        return EnvironmentFlag::Unset;

    const std::string text(value);
    if (text.empty())
        return EnvironmentFlag::Unset;
    if (text == "1" || text == "true" || text == "TRUE" || text == "on" ||
        text == "ON" || text == "yes" || text == "YES")
        return EnvironmentFlag::Enabled;
    if (text == "0" || text == "false" || text == "FALSE" ||
        text == "off" || text == "OFF" || text == "no" || text == "NO")
        return EnvironmentFlag::Disabled;

    return EnvironmentFlag::Disabled;
}

fusiondesk::runtime::display::DisplayCaptureBackendCapability
dxgiCapability()
{
    fusiondesk::runtime::display::DisplayCaptureBackendCapability capability;
    capability.adapterId = "windows.dxgi.desktop_duplication";
    capability.platform = fusiondesk::runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend =
        fusiondesk::runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication;
    capability.sourceTypes = {
        fusiondesk::runtime::display::DisplayCaptureSourceType::Monitor};
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.memoryTypes = {
        fusiondesk::runtime::display::DisplayCaptureMemoryType::D3DTexture,
        fusiondesk::runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    capability.diagnostics = {
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::DeviceLost,
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::SourceHotplug,
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::GeometryOrFormatChanged,
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::ProtectedContent};
    const EnvironmentFlag dxgiEnvironment =
        readEnvironmentFlag(kDxgiEnableEnvironment);
    capability.available = false;
    if (dxgiEnvironment == EnvironmentFlag::Disabled) {
        capability.unavailableReason =
            "DXGI Desktop Duplication adapter is disabled by "
            "FUSIONDESK_ENABLE_DXGI_CAPTURE=0";
    } else {
        const WindowsDxgiDesktopDuplicationProbeResult probe =
            probeWindowsDxgiDesktopDuplication();
        capability.available = probe.available;
        capability.unavailableReason = probe.available ? std::string() : probe.reason;
    }
    capability.zeroCopy = true;
    capability.supportsHotplugNotifications = true;
    capability.supportsCursorCapture = true;
    capability.protectedContentAware = true;
    capability.priority = 90;
    return capability;
}

fusiondesk::runtime::display::DisplayCaptureBackendCapability
wgcCapability()
{
    fusiondesk::runtime::display::DisplayCaptureBackendCapability capability;
    capability.adapterId = "windows.graphics_capture";
    capability.platform = fusiondesk::runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend =
        fusiondesk::runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture;
    capability.sourceTypes = {
        fusiondesk::runtime::display::DisplayCaptureSourceType::Monitor,
        fusiondesk::runtime::display::DisplayCaptureSourceType::Window};
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.memoryTypes = {
        fusiondesk::runtime::display::DisplayCaptureMemoryType::D3DTexture,
        fusiondesk::runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    capability.diagnostics = {
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::AccessDenied,
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::PermissionRevoked,
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::ProtectedContent,
        fusiondesk::runtime::display::DisplayCaptureDiagnosticKind::SourceHotplug};
    const WindowsGraphicsCaptureProbeResult probe =
        probeWindowsGraphicsCapture();
    capability.available = probe.available;
    capability.unavailableReason = probe.available ? std::string() : probe.reason;
    capability.zeroCopy = true;
    capability.supportsServiceSession = false;
    capability.supportsHotplugNotifications = true;
    capability.protectedContentAware = true;
    capability.priority = 85;
    return capability;
}

} // namespace

WindowsGraphicsCaptureProbeResult probeWindowsGraphicsCapture()
{
    WindowsGraphicsCaptureProbeResult result;
    const EnvironmentFlag wgcEnvironment =
        readEnvironmentFlag(kWgcEnableEnvironment);
    result.rolloutEnabled = wgcEnvironment == EnvironmentFlag::Enabled;
    result.adapterImplemented = true;

    if (wgcEnvironment == EnvironmentFlag::Disabled) {
        result.reason =
            "Windows Graphics Capture adapter is disabled by "
            "FUSIONDESK_ENABLE_WGC_CAPTURE=0";
        return result;
    }

    if (wgcEnvironment != EnvironmentFlag::Enabled) {
        result.reason =
            "Windows Graphics Capture adapter rollout is not enabled; set "
            "FUSIONDESK_ENABLE_WGC_CAPTURE=1 to validate the monitor adapter";
        return result;
    }

    WindowsGraphicsCapture capture;
    modules::display::DisplayCaptureOpenOptions options;
    options.targetWidth = 1;
    options.targetHeight = 1;
    options.scaleMode = modules::display::DisplayScaleMode::Fit;
    if (capture.open(options)) {
        capture.close();
        result.available = true;
        result.reason = "Windows Graphics Capture monitor adapter initialized";
        return result;
    }

    const modules::display::DisplayCaptureStatus status = capture.lastStatus();
    result.reason = status.message.empty()
                        ? "Windows Graphics Capture monitor adapter could not initialize"
                        : status.message;
    return result;
}

std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
WindowsDxgiDesktopDuplicationDisplayCaptureFactory::capabilities() const
{
    return {dxgiCapability()};
}

std::shared_ptr<modules::display::IDisplayCapture>
WindowsDxgiDesktopDuplicationDisplayCaptureFactory::createCapture(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    if (selected.backend !=
        fusiondesk::runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication)
        return nullptr;
    if (selected.adapterId != "windows.dxgi.desktop_duplication")
        return nullptr;
    return std::make_shared<WindowsDxgiDesktopDuplicationCapture>();
}

std::shared_ptr<modules::display::IDisplaySourceCatalog>
WindowsDxgiDesktopDuplicationDisplayCaptureFactory::createSourceCatalog(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    if (selected.backend !=
        fusiondesk::runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication)
        return nullptr;
    if (selected.adapterId != "windows.dxgi.desktop_duplication")
        return nullptr;
    return std::make_shared<WindowsDxgiDesktopDuplicationSourceCatalog>();
}

std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
WindowsGraphicsCaptureDisplayCaptureFactory::capabilities() const
{
    return {wgcCapability()};
}

std::shared_ptr<modules::display::IDisplayCapture>
WindowsGraphicsCaptureDisplayCaptureFactory::createCapture(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    if (selected.backend !=
        fusiondesk::runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture)
        return nullptr;
    if (selected.adapterId != "windows.graphics_capture")
        return nullptr;
    return std::make_shared<WindowsGraphicsCapture>();
}

std::shared_ptr<modules::display::IDisplaySourceCatalog>
WindowsGraphicsCaptureDisplayCaptureFactory::createSourceCatalog(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    if (selected.backend !=
        fusiondesk::runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture)
        return nullptr;
    if (selected.adapterId != "windows.graphics_capture")
        return nullptr;
    return std::make_shared<WindowsGraphicsCaptureSourceCatalog>();
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
