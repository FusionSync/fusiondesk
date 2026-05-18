#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/runtime/display/display_capture_backend_failover.h"

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
        result.unavailableReason = "adapter unavailable";
    result.fallback = fallback;
    result.priority = priority;
    return result;
}

class FakeDisplayCapture final : public modules::display::IDisplayCapture
{
public:
    explicit FakeDisplayCapture(std::string backendId)
        : backendId_(std::move(backendId))
    {
    }

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

    std::string backendId() const override
    {
        return backendId_;
    }

private:
    std::string backendId_;
};

class FakeCaptureFactory final : public runtime::display::IDisplayCaptureBackendFactory
{
public:
    explicit FakeCaptureFactory(
        std::vector<runtime::display::DisplayCaptureBackendCapability> capabilities)
        : capabilities_(std::move(capabilities))
    {
    }

    std::vector<runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override
    {
        return capabilities_;
    }

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const runtime::display::DisplayCaptureBackendCapability& selected) const override
    {
        if (!selected.available)
            return nullptr;
        return std::make_shared<FakeDisplayCapture>(selected.adapterId);
    }

private:
    std::vector<runtime::display::DisplayCaptureBackendCapability> capabilities_;
};

runtime::display::DisplayCaptureBackendSelectionRequest windowsMonitorRequest(
    std::vector<runtime::display::DisplayCaptureBackendCapability> candidates)
{
    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.candidates = std::move(candidates);
    return request;
}

void dxgiFailureSelectsGdiFallbackWhenWgcUnavailable()
{
    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest = windowsMonitorRequest({
        capability("windows.dxgi.desktop_duplication",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   true,
                   false,
                   90),
        capability("windows.graphics_capture",
                   runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture,
                   false,
                   false,
                   85),
        capability("windows.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    });
    request.failedAdapterId = "windows.dxgi.desktop_duplication";

    const runtime::display::DisplayCaptureBackendFailoverResult result =
        runtime::display::selectDisplayCaptureBackendFailover(request);
    assert(result.ok);
    assert(result.selection.selected.adapterId == "windows.gdi");
    assert(result.selection.fallbackSelected);
    assert(result.selection.rejected.size() == 1);
    assert(result.selection.rejected.front().adapterId ==
           "windows.graphics_capture");
}

void dxgiFailureSelectsWgcBeforeGdiWhenWgcIsAvailable()
{
    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest = windowsMonitorRequest({
        capability("windows.dxgi.desktop_duplication",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   true,
                   false,
                   90),
        capability("windows.graphics_capture",
                   runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture,
                   true,
                   false,
                   85),
        capability("windows.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    });
    request.failedAdapterId = "windows.dxgi.desktop_duplication";

    const runtime::display::DisplayCaptureBackendFailoverResult result =
        runtime::display::selectDisplayCaptureBackendFailover(request);
    assert(result.ok);
    assert(result.selection.selected.adapterId == "windows.graphics_capture");
    assert(!result.selection.fallbackSelected);
}

void explicitRequestedFailedAdapterBlocksFailoverByDefault()
{
    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest = windowsMonitorRequest({
        capability("windows.dxgi.desktop_duplication",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   true,
                   false,
                   90),
        capability("windows.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    });
    request.selectionRequest.requestedAdapterId =
        "windows.dxgi.desktop_duplication";
    request.failedAdapterId = "windows.dxgi.desktop_duplication";

    const runtime::display::DisplayCaptureBackendFailoverResult result =
        runtime::display::selectDisplayCaptureBackendFailover(request);
    assert(!result.ok);
    assert(result.blockedByRequestedAdapter);
}

void explicitRequestedFailedBackendKindBlocksFailoverByDefault()
{
    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest = windowsMonitorRequest({
        capability("windows.dxgi.desktop_duplication",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   true,
                   false,
                   90),
        capability("windows.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    });
    request.selectionRequest.requestedAdapterId =
        "windows.dxgi.desktop_duplication";
    request.failedBackend =
        runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication;

    const runtime::display::DisplayCaptureBackendFailoverResult result =
        runtime::display::selectDisplayCaptureBackendFailover(request);
    assert(!result.ok);
    assert(result.blockedByRequestedAdapter);
}

void callerCanOverrideRequestedAdapterForEmergencyFailover()
{
    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest = windowsMonitorRequest({
        capability("windows.dxgi.desktop_duplication",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   true,
                   false,
                   90),
        capability("windows.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    });
    request.selectionRequest.requestedAdapterId =
        "windows.dxgi.desktop_duplication";
    request.failedAdapterId = "windows.dxgi.desktop_duplication";
    request.honorRequestedAdapter = false;

    const runtime::display::DisplayCaptureBackendFailoverResult result =
        runtime::display::selectDisplayCaptureBackendFailover(request);
    assert(result.ok);
    assert(result.selection.selected.adapterId == "windows.gdi");
}

void failoverCreateUsesFactoryCapabilitiesAndCreatesSelectedBackend()
{
    std::vector<runtime::display::DisplayCaptureBackendCapability> capabilities = {
        capability("test.dxgi",
                   runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication,
                   true,
                   false,
                   90),
        capability("test.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    };
    FakeCaptureFactory factory(capabilities);

    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest.platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.selectionRequest.sourceType =
        runtime::display::DisplayCaptureSourceType::Monitor;
    request.failedAdapterId = "test.dxgi";

    const runtime::display::DisplayCaptureBackendCreateResult result =
        runtime::display::createFailoverDisplayCapture(factory, request);
    assert(result.ok);
    assert(result.capture != nullptr);
    assert(result.selection.selected.adapterId == "test.gdi");
    assert(result.capture->backendId() == "test.gdi");
}

void missingFailedIdentityIsRejected()
{
    runtime::display::DisplayCaptureBackendFailoverRequest request;
    request.selectionRequest = windowsMonitorRequest({
        capability("windows.gdi",
                   runtime::display::DisplayCaptureBackendKind::WindowsGdi,
                   true,
                   true,
                   10),
    });

    const runtime::display::DisplayCaptureBackendFailoverResult result =
        runtime::display::selectDisplayCaptureBackendFailover(request);
    assert(!result.ok);
    assert(!result.messages.empty());
}

} // namespace

int main()
{
    dxgiFailureSelectsGdiFallbackWhenWgcUnavailable();
    dxgiFailureSelectsWgcBeforeGdiWhenWgcIsAvailable();
    explicitRequestedFailedAdapterBlocksFailoverByDefault();
    explicitRequestedFailedBackendKindBlocksFailoverByDefault();
    callerCanOverrideRequestedAdapterForEmergencyFailover();
    failoverCreateUsesFactoryCapabilitiesAndCreatesSelectedBackend();
    missingFailedIdentityIsRejected();
    return 0;
}
