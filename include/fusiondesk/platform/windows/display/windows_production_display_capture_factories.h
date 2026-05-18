#ifndef FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_PRODUCTION_DISPLAY_CAPTURE_FACTORIES_H
#define FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_PRODUCTION_DISPLAY_CAPTURE_FACTORIES_H

#include <string>

#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

struct WindowsGraphicsCaptureProbeResult
{
    bool rolloutEnabled = false;
    bool adapterImplemented = false;
    bool available = false;
    std::string reason;
};

WindowsGraphicsCaptureProbeResult probeWindowsGraphicsCapture();

class WindowsDxgiDesktopDuplicationDisplayCaptureFactory final
    : public fusiondesk::runtime::display::IDisplayCaptureBackendFactory
{
public:
    std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override;

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;

    std::shared_ptr<modules::display::IDisplaySourceCatalog>
    createSourceCatalog(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;
};

class WindowsGraphicsCaptureDisplayCaptureFactory final
    : public fusiondesk::runtime::display::IDisplayCaptureBackendFactory
{
public:
    std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override;

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;

    std::shared_ptr<modules::display::IDisplaySourceCatalog>
    createSourceCatalog(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;
};

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_PRODUCTION_DISPLAY_CAPTURE_FACTORIES_H
