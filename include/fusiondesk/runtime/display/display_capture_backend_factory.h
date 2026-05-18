#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_FACTORY_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_FACTORY_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_interfaces.h"
#include "fusiondesk/runtime/display/display_capture_backend_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

class IDisplayCaptureBackendFactory
{
public:
    virtual ~IDisplayCaptureBackendFactory() = default;

    virtual std::vector<DisplayCaptureBackendCapability> capabilities() const = 0;
    virtual std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const DisplayCaptureBackendCapability& selected) const = 0;
    virtual std::shared_ptr<modules::display::IDisplaySourceCatalog>
    createSourceCatalog(const DisplayCaptureBackendCapability& selected) const
    {
        (void)selected;
        return nullptr;
    }
};

class StaticDisplayCaptureBackendFactory final : public IDisplayCaptureBackendFactory
{
public:
    explicit StaticDisplayCaptureBackendFactory(
        std::vector<DisplayCaptureBackendCapability> capabilities);

    std::vector<DisplayCaptureBackendCapability> capabilities() const override;

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const DisplayCaptureBackendCapability& selected) const override;

private:
    std::vector<DisplayCaptureBackendCapability> capabilities_;
};

class DisplayCaptureBackendFactoryRegistry final : public IDisplayCaptureBackendFactory
{
public:
    void addFactory(std::shared_ptr<IDisplayCaptureBackendFactory> factory);
    std::size_t factoryCount() const;

    std::vector<DisplayCaptureBackendCapability> capabilities() const override;

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const DisplayCaptureBackendCapability& selected) const override;

    std::shared_ptr<modules::display::IDisplaySourceCatalog>
    createSourceCatalog(
        const DisplayCaptureBackendCapability& selected) const override;

private:
    std::vector<std::shared_ptr<IDisplayCaptureBackendFactory>> factories_;
};

struct DisplayCaptureBackendCreateResult
{
    bool ok = false;
    DisplayCaptureBackendSelectionResult selection;
    std::shared_ptr<modules::display::IDisplayCapture> capture;
    std::vector<std::string> messages;
};

struct DisplayCaptureSourceCatalogResult
{
    bool ok = false;
    DisplayCaptureBackendSelectionResult selection;
    bool hasCatalog = false;
    modules::display::DisplayTopologySnapshot topology;
    std::vector<std::string> messages;
};

DisplayCaptureBackendCreateResult createSelectedDisplayCapture(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendSelectionRequest& request);

DisplayCaptureSourceCatalogResult queryDisplayCaptureSourceCatalog(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendSelectionRequest& request);

std::vector<DisplayCaptureBackendCapability>
unavailableDefaultDisplayCaptureBackendCapabilities(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason);

std::shared_ptr<IDisplayCaptureBackendFactory>
createUnavailableDefaultDisplayCaptureBackendFactory(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_FACTORY_H
