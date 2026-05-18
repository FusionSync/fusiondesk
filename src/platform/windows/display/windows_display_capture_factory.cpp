#include "fusiondesk/platform/windows/display/windows_display_capture_factory.h"

#include <memory>

#include "fusiondesk/platform/windows/display/windows_gdi_display_capture.h"
#include "fusiondesk/platform/windows/display/windows_production_display_capture_factories.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

WindowsDisplayCaptureBackendFactory::WindowsDisplayCaptureBackendFactory()
{
    registry_.addFactory(
        std::make_shared<WindowsDxgiDesktopDuplicationDisplayCaptureFactory>());
    registry_.addFactory(
        std::make_shared<WindowsGraphicsCaptureDisplayCaptureFactory>());
    registry_.addFactory(std::make_shared<WindowsGdiDisplayCaptureFactory>());
}

std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
WindowsDisplayCaptureBackendFactory::capabilities() const
{
    return registry_.capabilities();
}

std::shared_ptr<modules::display::IDisplayCapture>
WindowsDisplayCaptureBackendFactory::createCapture(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    return registry_.createCapture(selected);
}

std::shared_ptr<modules::display::IDisplaySourceCatalog>
WindowsDisplayCaptureBackendFactory::createSourceCatalog(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    return registry_.createSourceCatalog(selected);
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
