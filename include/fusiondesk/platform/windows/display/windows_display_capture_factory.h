#ifndef FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_DISPLAY_CAPTURE_FACTORY_H
#define FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_DISPLAY_CAPTURE_FACTORY_H

#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

class WindowsDisplayCaptureBackendFactory final
    : public fusiondesk::runtime::display::IDisplayCaptureBackendFactory
{
public:
    WindowsDisplayCaptureBackendFactory();

    std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override;

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;

    std::shared_ptr<modules::display::IDisplaySourceCatalog>
    createSourceCatalog(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;

private:
    fusiondesk::runtime::display::DisplayCaptureBackendFactoryRegistry registry_;
};

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_DISPLAY_CAPTURE_FACTORY_H
