#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCaptureBackendCapability capability(
    std::string adapterId,
    runtime::display::DisplayCaptureBackendKind backend,
    bool available,
    bool fallback,
    int priority)
{
    runtime::display::DisplayCaptureBackendCapability result;
    result.adapterId = std::move(adapterId);
    result.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    result.backend = backend;
    result.sourceTypes = {runtime::display::DisplayCaptureSourceType::Monitor};
    result.memoryTypes = {runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    result.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    result.available = available;
    if (!available)
        result.unavailableReason = "probe unavailable";
    result.fallback = fallback;
    result.priority = priority;
    return result;
}

class FakeDisplayCapture final : public modules::display::IDisplayCapture
{
public:
    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        modules::display::CapturedFrame frame;
        frame.keyFrame = keyFrame;
        frame.width = 1;
        frame.height = 1;
        frame.strideBytes = 4;
        frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
        frame.pixels = {0, 0, 0, 255};
        return frame;
    }
};

class FakeSourceCatalog final : public modules::display::IDisplaySourceCatalog
{
public:
    modules::display::DisplayTopologySnapshot snapshot() const override
    {
        modules::display::DisplayTopologySnapshot snapshot;
        snapshot.generation = 42;
        modules::display::DisplaySourceInfo source;
        source.sourceId = 7;
        source.sourceType = modules::display::DisplayCaptureSourceType::Monitor;
        source.geometry.width = 640;
        source.geometry.height = 480;
        source.name = "fake-monitor";
        snapshot.sources.push_back(source);
        return snapshot;
    }
};

class FakeGdiFactory final : public runtime::display::IDisplayCaptureBackendFactory
{
public:
    std::vector<runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override
    {
        return {capability("test.gdi",
                           runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                           true,
                           true,
                           1)};
    }

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const runtime::display::DisplayCaptureBackendCapability& selected) const override
    {
        if (selected.adapterId != "test.gdi")
            return nullptr;
        return std::make_shared<FakeDisplayCapture>();
    }

    std::shared_ptr<modules::display::IDisplaySourceCatalog> createSourceCatalog(
        const runtime::display::DisplayCaptureBackendCapability& selected) const override
    {
        if (selected.adapterId != "test.gdi")
            return nullptr;
        return std::make_shared<FakeSourceCatalog>();
    }
};

void registrySelectsAvailableFactoryAfterUnavailableStaticCapability()
{
    runtime::display::DisplayCaptureBackendFactoryRegistry registry;
    registry.addFactory(nullptr);
    assert(registry.factoryCount() == 0);

    std::vector<runtime::display::DisplayCaptureBackendCapability> staticCaps = {
        capability("test.dxgi",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   false,
                   false,
                   99)};
    registry.addFactory(std::make_shared<runtime::display::StaticDisplayCaptureBackendFactory>(
        staticCaps));
    registry.addFactory(std::make_shared<FakeGdiFactory>());
    assert(registry.factoryCount() == 2);

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;

    const runtime::display::DisplayCaptureBackendCreateResult created =
        runtime::display::createSelectedDisplayCapture(registry, request);

    assert(created.ok);
    assert(created.capture != nullptr);
    assert(created.selection.selected.adapterId == "test.gdi");
    assert(created.selection.fallbackSelected);
    assert(created.selection.rejected.size() == 1);
    assert(created.selection.rejected.front().reason ==
           "capture backend is unavailable: probe unavailable");

    const std::shared_ptr<modules::display::IDisplaySourceCatalog> catalog =
        registry.createSourceCatalog(created.selection.selected);
    assert(catalog != nullptr);
    const modules::display::DisplayTopologySnapshot topology =
        catalog->snapshot();
    assert(topology.generation == 42);
    assert(topology.sources.size() == 1);
    assert(topology.sources.front().sourceId == 7);

    const runtime::display::DisplayCaptureSourceCatalogResult catalogResult =
        runtime::display::queryDisplayCaptureSourceCatalog(registry, request);
    assert(catalogResult.ok);
    assert(catalogResult.hasCatalog);
    assert(catalogResult.selection.selected.adapterId == "test.gdi");
    assert(catalogResult.topology.generation == 42);
    assert(catalogResult.topology.sources.size() == 1);
}

void staticUnavailableFactoryReportsRejectedMessages()
{
    runtime::display::DisplayCaptureBackendFactoryRegistry registry;
    std::vector<runtime::display::DisplayCaptureBackendCapability> staticCaps = {
        capability("test.dxgi",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   false,
                   false,
                   99)};
    registry.addFactory(std::make_shared<runtime::display::StaticDisplayCaptureBackendFactory>(
        staticCaps));

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;

    const runtime::display::DisplayCaptureBackendCreateResult created =
        runtime::display::createSelectedDisplayCapture(registry, request);

    assert(!created.ok);
    bool sawUnavailableMessage = false;
    for (const std::string& message : created.messages) {
        if (message.find("test.dxgi") != std::string::npos &&
            message.find("probe unavailable") != std::string::npos)
            sawUnavailableMessage = true;
    }
    assert(sawUnavailableMessage);

    const std::shared_ptr<modules::display::IDisplaySourceCatalog> catalog =
        registry.createSourceCatalog(staticCaps.front());
    assert(catalog == nullptr);
}

void selectedBackendWithoutCatalogReportsProviderFailure()
{
    runtime::display::DisplayCaptureBackendFactoryRegistry registry;
    std::vector<runtime::display::DisplayCaptureBackendCapability> staticCaps = {
        capability("test.static",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   1)};
    registry.addFactory(std::make_shared<runtime::display::StaticDisplayCaptureBackendFactory>(
        staticCaps));

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;

    const runtime::display::DisplayCaptureSourceCatalogResult catalogResult =
        runtime::display::queryDisplayCaptureSourceCatalog(registry, request);
    assert(!catalogResult.ok);
    assert(catalogResult.selection.hasSelection);
    assert(catalogResult.selection.selected.adapterId == "test.static");
    assert(!catalogResult.hasCatalog);
    bool sawCatalogMessage = false;
    for (const std::string& message : catalogResult.messages) {
        if (message.find("source catalog") != std::string::npos)
            sawCatalogMessage = true;
    }
    assert(sawCatalogMessage);
}

void unavailableDefaultFactoriesCoverProductionPlatformFamilies()
{
    const std::vector<runtime::display::DisplayPlatformFamily> platforms = {
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        runtime::display::DisplayPlatformFamily::LinuxX11,
        runtime::display::DisplayPlatformFamily::LinuxWayland,
        runtime::display::DisplayPlatformFamily::LinuxEmbedded,
        runtime::display::DisplayPlatformFamily::MacOS,
        runtime::display::DisplayPlatformFamily::AndroidClient,
        runtime::display::DisplayPlatformFamily::AndroidAgent,
        runtime::display::DisplayPlatformFamily::HarmonyOS,
        runtime::display::DisplayPlatformFamily::OpenHarmony,
        runtime::display::DisplayPlatformFamily::RockchipLinux,
        runtime::display::DisplayPlatformFamily::RockchipAndroid};

    for (runtime::display::DisplayPlatformFamily platform : platforms) {
        const std::string reason = std::string("adapter pending for ") +
                                   runtime::display::displayPlatformFamilyName(platform);
        const std::vector<runtime::display::DisplayCaptureBackendCapability> caps =
            runtime::display::unavailableDefaultDisplayCaptureBackendCapabilities(
                platform,
                reason);
        assert(!caps.empty());
        for (const runtime::display::DisplayCaptureBackendCapability& cap : caps) {
            assert(!cap.available);
            assert(cap.unavailableReason == reason);
        }

        const std::shared_ptr<runtime::display::IDisplayCaptureBackendFactory> factory =
            runtime::display::createUnavailableDefaultDisplayCaptureBackendFactory(
                platform,
                reason);
        runtime::display::DisplayCaptureBackendSelectionRequest request;
        request.platform = platform;
        request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;

        const runtime::display::DisplayCaptureBackendCreateResult created =
            runtime::display::createSelectedDisplayCapture(*factory, request);
        assert(!created.ok);

        bool sawReason = false;
        for (const std::string& message : created.messages) {
            if (message.find(reason) != std::string::npos)
                sawReason = true;
        }
        assert(sawReason);
    }
}

} // namespace

int main()
{
    registrySelectsAvailableFactoryAfterUnavailableStaticCapability();
    staticUnavailableFactoryReportsRejectedMessages();
    selectedBackendWithoutCatalogReportsProviderFailure();
    unavailableDefaultFactoriesCoverProductionPlatformFamilies();
    return 0;
}
