#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

#include <memory>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

void appendSelectionMessages(const DisplayCaptureBackendSelectionResult& selection,
                             std::vector<std::string>& messages)
{
    messages = selection.messages;
    for (const DisplayCaptureBackendRejection& rejection :
         selection.rejected) {
        std::string message = "display capture backend rejected: ";
        message += rejection.adapterId.empty()
                       ? displayCaptureBackendKindName(rejection.backend)
                       : rejection.adapterId;
        if (!rejection.reason.empty()) {
            message += ": ";
            message += rejection.reason;
        }
        messages.push_back(std::move(message));
    }
}

DisplayCaptureBackendSelectionRequest requestWithFactoryCapabilities(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendSelectionRequest& request)
{
    DisplayCaptureBackendSelectionRequest selectionRequest = request;
    if (selectionRequest.candidates.empty())
        selectionRequest.candidates = factory.capabilities();
    return selectionRequest;
}

} // namespace

StaticDisplayCaptureBackendFactory::StaticDisplayCaptureBackendFactory(
    std::vector<DisplayCaptureBackendCapability> capabilities)
    : capabilities_(std::move(capabilities))
{
}

std::vector<DisplayCaptureBackendCapability>
StaticDisplayCaptureBackendFactory::capabilities() const
{
    return capabilities_;
}

std::shared_ptr<modules::display::IDisplayCapture>
StaticDisplayCaptureBackendFactory::createCapture(
    const DisplayCaptureBackendCapability& selected) const
{
    (void)selected;
    return nullptr;
}

void DisplayCaptureBackendFactoryRegistry::addFactory(
    std::shared_ptr<IDisplayCaptureBackendFactory> factory)
{
    if (factory)
        factories_.push_back(std::move(factory));
}

std::size_t DisplayCaptureBackendFactoryRegistry::factoryCount() const
{
    return factories_.size();
}

std::vector<DisplayCaptureBackendCapability>
DisplayCaptureBackendFactoryRegistry::capabilities() const
{
    std::vector<DisplayCaptureBackendCapability> result;
    for (const std::shared_ptr<IDisplayCaptureBackendFactory>& factory :
         factories_) {
        if (!factory)
            continue;

        std::vector<DisplayCaptureBackendCapability> values =
            factory->capabilities();
        result.insert(result.end(), values.begin(), values.end());
    }
    return result;
}

std::shared_ptr<modules::display::IDisplayCapture>
DisplayCaptureBackendFactoryRegistry::createCapture(
    const DisplayCaptureBackendCapability& selected) const
{
    for (const std::shared_ptr<IDisplayCaptureBackendFactory>& factory :
         factories_) {
        if (!factory)
            continue;

        std::shared_ptr<modules::display::IDisplayCapture> capture =
            factory->createCapture(selected);
        if (capture)
            return capture;
    }
    return nullptr;
}

std::shared_ptr<modules::display::IDisplaySourceCatalog>
DisplayCaptureBackendFactoryRegistry::createSourceCatalog(
    const DisplayCaptureBackendCapability& selected) const
{
    for (const std::shared_ptr<IDisplayCaptureBackendFactory>& factory :
         factories_) {
        if (!factory)
            continue;

        std::shared_ptr<modules::display::IDisplaySourceCatalog> catalog =
            factory->createSourceCatalog(selected);
        if (catalog)
            return catalog;
    }
    return nullptr;
}

DisplayCaptureBackendCreateResult createSelectedDisplayCapture(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendSelectionRequest& request)
{
    DisplayCaptureBackendCreateResult result;

    const DisplayCaptureBackendSelectionRequest selectionRequest =
        requestWithFactoryCapabilities(factory, request);
    result.selection = selectDisplayCaptureBackend(selectionRequest);
    if (!result.selection.ok) {
        appendSelectionMessages(result.selection, result.messages);
        if (result.messages.empty())
            result.messages.push_back("display capture backend selection failed");
        return result;
    }

    result.capture = factory.createCapture(result.selection.selected);
    if (result.capture == nullptr) {
        result.messages.push_back(
            "display capture backend factory could not create selected backend");
        return result;
    }

    result.ok = true;
    return result;
}

DisplayCaptureSourceCatalogResult queryDisplayCaptureSourceCatalog(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendSelectionRequest& request)
{
    DisplayCaptureSourceCatalogResult result;

    const DisplayCaptureBackendSelectionRequest selectionRequest =
        requestWithFactoryCapabilities(factory, request);
    result.selection = selectDisplayCaptureBackend(selectionRequest);
    if (!result.selection.ok) {
        appendSelectionMessages(result.selection, result.messages);
        if (result.messages.empty())
            result.messages.push_back(
                "display capture source catalog selection failed");
        return result;
    }

    std::shared_ptr<modules::display::IDisplaySourceCatalog> catalog =
        factory.createSourceCatalog(result.selection.selected);
    if (catalog == nullptr) {
        result.messages.push_back(
            "display capture backend factory could not create selected source catalog");
        return result;
    }

    result.hasCatalog = true;
    result.topology = catalog->snapshot();
    result.ok = true;
    return result;
}

std::vector<DisplayCaptureBackendCapability>
unavailableDefaultDisplayCaptureBackendCapabilities(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason)
{
    std::vector<DisplayCaptureBackendCapability> capabilities =
        defaultDisplayCaptureBackendCapabilities(platform);
    for (DisplayCaptureBackendCapability& capability : capabilities) {
        capability.available = false;
        capability.unavailableReason = unavailableReason;
    }
    return capabilities;
}

std::shared_ptr<IDisplayCaptureBackendFactory>
createUnavailableDefaultDisplayCaptureBackendFactory(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason)
{
    return std::make_shared<StaticDisplayCaptureBackendFactory>(
        unavailableDefaultDisplayCaptureBackendCapabilities(platform,
                                                           unavailableReason));
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
