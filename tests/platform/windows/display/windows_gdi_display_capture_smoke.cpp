#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi.h>
#include <windows.h>
#endif

#include "fusiondesk/platform/windows/display/windows_display_capture_factory.h"
#include "fusiondesk/platform/windows/display/windows_cursor_overlay.h"
#include "fusiondesk/platform/windows/display/windows_dxgi_desktop_duplication_capture.h"
#include "fusiondesk/platform/windows/display/windows_gdi_display_capture.h"
#include "fusiondesk/platform/windows/display/windows_graphics_capture.h"
#include "fusiondesk/platform/windows/display/windows_production_display_capture_factories.h"
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

using namespace fusiondesk;

namespace {

bool messagesContain(const std::vector<std::string>& messages,
                     const std::string& needle)
{
    for (const std::string& message : messages) {
        if (message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void validateTopologyShape(
    const modules::display::DisplayTopologySnapshot& topology)
{
    for (const modules::display::DisplaySourceInfo& source :
         topology.sources) {
        assert(source.sourceType !=
               modules::display::DisplayCaptureSourceType::Unknown);
        assert(source.geometry.width > 0);
        assert(source.geometry.height > 0);
        if (source.sourceType ==
            modules::display::DisplayCaptureSourceType::Window) {
            assert(source.nativeSourceHandle != 0);
        }
    }
}

bool capabilityContainsSourceType(
    const runtime::display::DisplayCaptureBackendCapability& capability,
    runtime::display::DisplayCaptureSourceType sourceType)
{
    for (runtime::display::DisplayCaptureSourceType candidate :
         capability.sourceTypes) {
        if (candidate == sourceType)
            return true;
    }
    return false;
}

} // namespace

int main()
{
#if defined(_WIN32)
    _putenv_s("FUSIONDESK_ENABLE_DXGI_CAPTURE", "");
    _putenv_s("FUSIONDESK_ENABLE_WGC_CAPTURE", "");

    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                   static_cast<int>(DXGI_ERROR_ACCESS_LOST)) ==
           modules::display::DisplayCaptureStatusCode::SourceHotplug);
    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusRecoverable(
                   static_cast<int>(DXGI_ERROR_ACCESS_LOST)));

    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                   static_cast<int>(DXGI_ERROR_DEVICE_REMOVED)) ==
           modules::display::DisplayCaptureStatusCode::DeviceLost);
    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusRecoverable(
                   static_cast<int>(DXGI_ERROR_DEVICE_REMOVED)));

    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                   static_cast<int>(E_ACCESSDENIED)) ==
           modules::display::DisplayCaptureStatusCode::AccessDenied);
    assert(!platform::windows::display::
                windowsDxgiDesktopDuplicationStatusRecoverable(
                    static_cast<int>(E_ACCESSDENIED)));

    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                   static_cast<int>(DXGI_ERROR_SESSION_DISCONNECTED)) ==
           modules::display::DisplayCaptureStatusCode::SessionModeUnsupported);
    assert(platform::windows::display::
               windowsDxgiDesktopDuplicationStatusRecoverable(
                   static_cast<int>(DXGI_ERROR_SESSION_DISCONNECTED)));

    platform::windows::display::WindowsCursorFrameMappingInput cursorMapping;
    cursorMapping.sourceX = 100;
    cursorMapping.sourceY = 200;
    cursorMapping.sourceWidth = 2000;
    cursorMapping.sourceHeight = 1000;
    cursorMapping.frameWidth = 1000;
    cursorMapping.frameHeight = 500;
    cursorMapping.cursorScreenX = 600;
    cursorMapping.cursorScreenY = 450;
    cursorMapping.hotspotX = 10;
    cursorMapping.hotspotY = 5;
    cursorMapping.cursorWidth = 32;
    cursorMapping.cursorHeight = 32;
    const platform::windows::display::WindowsCursorFrameRect cursorRect =
        platform::windows::display::mapWindowsCursorToFrame(cursorMapping);
    assert(cursorRect.insideFrame);
    assert(cursorRect.x == 245);
    assert(cursorRect.y == 122);
    assert(cursorRect.width == 16);
    assert(cursorRect.height == 16);

    cursorMapping.cursorScreenX = -5000;
    const platform::windows::display::WindowsCursorFrameRect outsideCursorRect =
        platform::windows::display::mapWindowsCursorToFrame(cursorMapping);
    assert(!outsideCursorRect.insideFrame);

    platform::windows::display::WindowsDxgiDesktopDuplicationDisplayCaptureFactory
        defaultDxgiFactory;
    const std::vector<runtime::display::DisplayCaptureBackendCapability>
        defaultDxgiCapabilities = defaultDxgiFactory.capabilities();
    assert(defaultDxgiCapabilities.size() == 1);
    assert(defaultDxgiCapabilities.front().backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication);
    assert(defaultDxgiCapabilities.front().available ||
           defaultDxgiCapabilities.front().unavailableReason.find("disabled") ==
               std::string::npos);

    platform::windows::display::WindowsGraphicsCaptureProbeResult defaultWgcProbe =
        platform::windows::display::probeWindowsGraphicsCapture();
    assert(!defaultWgcProbe.available);
    assert(!defaultWgcProbe.rolloutEnabled);
    assert(defaultWgcProbe.reason.find("not enabled") != std::string::npos);

    platform::windows::display::WindowsGraphicsCaptureDisplayCaptureFactory
        wgcFactory;
    std::vector<runtime::display::DisplayCaptureBackendCapability>
        wgcCapabilities = wgcFactory.capabilities();
    assert(wgcCapabilities.size() == 1);
    assert(wgcCapabilities.front().backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture);
    assert(capabilityContainsSourceType(
        wgcCapabilities.front(),
        runtime::display::DisplayCaptureSourceType::Monitor));
    assert(capabilityContainsSourceType(
        wgcCapabilities.front(),
        runtime::display::DisplayCaptureSourceType::Window));
    assert(!wgcCapabilities.front().available);
    assert(wgcCapabilities.front().unavailableReason.find("not enabled") !=
           std::string::npos);
    std::shared_ptr<modules::display::IDisplayCapture> defaultWgcCapture =
        wgcFactory.createCapture(wgcCapabilities.front());
    assert(defaultWgcCapture != nullptr);
    assert(defaultWgcCapture->backendId() == "windows.graphics_capture");
    std::shared_ptr<modules::display::IDisplaySourceCatalog> wgcFactoryCatalog =
        wgcFactory.createSourceCatalog(wgcCapabilities.front());
    assert(wgcFactoryCatalog != nullptr);

    _putenv_s("FUSIONDESK_ENABLE_WGC_CAPTURE", "0");
    platform::windows::display::WindowsGraphicsCaptureProbeResult disabledWgcProbe =
        platform::windows::display::probeWindowsGraphicsCapture();
    assert(!disabledWgcProbe.available);
    assert(!disabledWgcProbe.rolloutEnabled);
    assert(disabledWgcProbe.reason.find("disabled") != std::string::npos);

    _putenv_s("FUSIONDESK_ENABLE_WGC_CAPTURE", "1");
    platform::windows::display::WindowsGraphicsCaptureProbeResult enabledWgcProbe =
        platform::windows::display::probeWindowsGraphicsCapture();
    assert(enabledWgcProbe.rolloutEnabled);
    assert(enabledWgcProbe.adapterImplemented);
    assert(!enabledWgcProbe.reason.empty());
    if (enabledWgcProbe.available)
        assert(enabledWgcProbe.reason.find("initialized") != std::string::npos);
    if (enabledWgcProbe.available) {
        platform::windows::display::WindowsDisplayCaptureBackendFactory
            enabledWindowsFactory;
        runtime::display::DisplayCaptureBackendSelectionRequest
            enabledWindowRequest;
        enabledWindowRequest.platform =
            runtime::display::DisplayPlatformFamily::WindowsDesktop;
        enabledWindowRequest.sourceType =
            runtime::display::DisplayCaptureSourceType::Window;
        enabledWindowRequest.requestedAdapterId = "windows.graphics_capture";
        const runtime::display::DisplayCaptureBackendCreateResult
            enabledWindowCreated =
                runtime::display::createSelectedDisplayCapture(
                    enabledWindowsFactory,
                    enabledWindowRequest);
        assert(enabledWindowCreated.ok);
        assert(enabledWindowCreated.capture != nullptr);
        assert(enabledWindowCreated.selection.selected.backend ==
               runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture);
    }

    _putenv_s("FUSIONDESK_ENABLE_DXGI_CAPTURE", "0");
    _putenv_s("FUSIONDESK_ENABLE_WGC_CAPTURE", "0");
#endif

    platform::windows::display::WindowsGdiDisplayCaptureFactory factory;
    const std::vector<runtime::display::DisplayCaptureBackendCapability> capabilities =
        factory.capabilities();
    assert(capabilities.size() == 1);
    assert(capabilities.front().backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGdi);
    assert(capabilities.front().fallback);

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    const runtime::display::DisplayCaptureBackendCreateResult created =
        runtime::display::createSelectedDisplayCapture(factory, request);
    assert(created.ok);
    assert(created.capture != nullptr);
    assert(created.capture->backendId() == "windows.gdi");
    assert(created.capture->captureErrors() == 0);
    assert(created.selection.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGdi);

    platform::windows::display::WindowsDxgiDesktopDuplicationDisplayCaptureFactory
        dxgiFactory;
    const std::vector<runtime::display::DisplayCaptureBackendCapability>
        dxgiCapabilities = dxgiFactory.capabilities();
    assert(dxgiCapabilities.size() == 1);
    assert(dxgiCapabilities.front().backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication);
    assert(!dxgiCapabilities.front().available);
    assert(dxgiCapabilities.front().zeroCopy);
    std::shared_ptr<modules::display::IDisplayCapture> dxgiCapture =
        dxgiFactory.createCapture(dxgiCapabilities.front());
    assert(dxgiCapture != nullptr);
    std::shared_ptr<modules::display::IDisplaySourceCatalog> dxgiFactoryCatalog =
        dxgiFactory.createSourceCatalog(dxgiCapabilities.front());
    assert(dxgiFactoryCatalog != nullptr);
    auto* concreteDxgi = dynamic_cast<
        platform::windows::display::WindowsDxgiDesktopDuplicationCapture*>(
        dxgiCapture.get());
    assert(concreteDxgi != nullptr);
    assert(concreteDxgi->backendId() == "windows.dxgi.desktop_duplication");
    assert(!concreteDxgi->isOpen());

    platform::windows::display::WindowsDisplayCaptureBackendFactory windowsFactory;
    const std::vector<runtime::display::DisplayCaptureBackendCapability>
        windowsCapabilities = windowsFactory.capabilities();
    bool sawUnavailableDxgi = false;
    bool sawUnavailableWgc = false;
    bool sawAvailableGdi = false;
    for (const runtime::display::DisplayCaptureBackendCapability& capability :
         windowsCapabilities) {
        if (capability.backend ==
            runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication) {
            sawUnavailableDxgi = !capability.available &&
                                 capability.unavailableReason.find("disabled") !=
                                     std::string::npos;
        }
        if (capability.backend ==
            runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture) {
            sawUnavailableWgc = !capability.available &&
                                capability.unavailableReason.find("disabled") !=
                                    std::string::npos;
        }
        if (capability.backend ==
            runtime::display::DisplayCaptureBackendKind::WindowsGdi)
            sawAvailableGdi = capability.available;
    }
    assert(sawUnavailableDxgi);
    assert(sawUnavailableWgc);
    assert(sawAvailableGdi);

    const runtime::display::DisplayCaptureBackendCreateResult windowsCreated =
        runtime::display::createSelectedDisplayCapture(windowsFactory, request);
    assert(windowsCreated.ok);
    assert(windowsCreated.capture != nullptr);
    assert(windowsCreated.selection.selected.backend ==
           runtime::display::DisplayCaptureBackendKind::WindowsGdi);
    assert(windowsCreated.selection.rejected.size() >= 2);
    std::shared_ptr<modules::display::IDisplaySourceCatalog>
        selectedCatalog =
            windowsFactory.createSourceCatalog(windowsCreated.selection.selected);
    assert(selectedCatalog != nullptr);
    validateTopologyShape(selectedCatalog->snapshot());

    runtime::display::DisplayCaptureBackendSelectionRequest forcedGdiRequest =
        request;
    forcedGdiRequest.requestedAdapterId = "windows.gdi";
    const runtime::display::DisplayCaptureBackendCreateResult forcedGdiCreated =
        runtime::display::createSelectedDisplayCapture(windowsFactory,
                                                       forcedGdiRequest);
    assert(forcedGdiCreated.ok);
    assert(forcedGdiCreated.capture != nullptr);
    assert(forcedGdiCreated.selection.selected.adapterId == "windows.gdi");
    assert(forcedGdiCreated.selection.fallbackSelected);

    runtime::display::DisplayCaptureBackendSelectionRequest forcedDxgiRequest =
        request;
    forcedDxgiRequest.requestedAdapterId = "windows.dxgi.desktop_duplication";
    const runtime::display::DisplayCaptureBackendCreateResult forcedDxgiCreated =
        runtime::display::createSelectedDisplayCapture(windowsFactory,
                                                       forcedDxgiRequest);
    assert(!forcedDxgiCreated.ok);
    assert(messagesContain(forcedDxgiCreated.messages, "windows.dxgi"));
    assert(messagesContain(forcedDxgiCreated.messages, "disabled"));

    runtime::display::DisplayCaptureBackendSelectionRequest forcedWgcRequest =
        request;
    forcedWgcRequest.requestedAdapterId = "windows.graphics_capture";
    const runtime::display::DisplayCaptureBackendCreateResult forcedWgcCreated =
        runtime::display::createSelectedDisplayCapture(windowsFactory,
                                                       forcedWgcRequest);
    assert(!forcedWgcCreated.ok);
    assert(messagesContain(forcedWgcCreated.messages, "windows.graphics_capture"));
    assert(messagesContain(forcedWgcCreated.messages, "disabled"));

    runtime::display::DisplayCaptureBackendSelectionRequest windowRequest = request;
    windowRequest.sourceType = runtime::display::DisplayCaptureSourceType::Window;
    const runtime::display::DisplayCaptureBackendCreateResult windowCreated =
        runtime::display::createSelectedDisplayCapture(windowsFactory, windowRequest);
    assert(!windowCreated.ok);
    bool sawWindowUnavailableReason = false;
    for (const std::string& message : windowCreated.messages) {
        if (message.find("windows.graphics_capture") != std::string::npos &&
            message.find("disabled") != std::string::npos) {
            sawWindowUnavailableReason = true;
        }
    }
    assert(sawWindowUnavailableReason);

    platform::windows::display::WindowsGdiDisplaySourceCatalog catalog;
    const modules::display::DisplayTopologySnapshot topology = catalog.snapshot();
    validateTopologyShape(topology);

    platform::windows::display::WindowsDxgiDesktopDuplicationSourceCatalog
        dxgiCatalog;
    const modules::display::DisplayTopologySnapshot dxgiTopology =
        dxgiCatalog.snapshot();
    validateTopologyShape(dxgiTopology);

    platform::windows::display::WindowsGraphicsCaptureSourceCatalog
        wgcCatalog;
    const modules::display::DisplayTopologySnapshot wgcTopology =
        wgcCatalog.snapshot();
    validateTopologyShape(wgcTopology);

    platform::windows::display::WindowsGdiDisplayCapture capture;
    modules::display::DisplayCaptureOpenOptions options;
    assert(options.includeCursor);
    options.targetWidth = 1;
    options.targetHeight = 1;
    options.scaleMode = modules::display::DisplayScaleMode::Fit;
    assert(capture.open(options));
    assert(capture.isOpen());
    assert(capture.backendId() == "windows.gdi");
    assert(capture.captureErrors() == 0);
    assert(capture.lastStatus().code ==
           modules::display::DisplayCaptureStatusCode::Ok);
    capture.close();
    assert(!capture.isOpen());

    modules::display::DisplayCaptureOpenOptions windowOptions = options;
    windowOptions.sourceType =
        modules::display::DisplayCaptureSourceType::Window;
    assert(!capture.open(windowOptions));
    assert(!capture.isOpen());
    assert(capture.lastStatus().code ==
           modules::display::DisplayCaptureStatusCode::Unsupported);

    modules::display::DisplayCaptureOpenOptions invalidOptions = options;
    invalidOptions.sourceId = static_cast<std::uint32_t>(
        topology.sources.size());
    assert(!capture.open(invalidOptions));
    assert(!capture.isOpen());
    assert(capture.lastStatus().code ==
           modules::display::DisplayCaptureStatusCode::SourceNotFound);
    assert(capture.captureErrors() > 0);
    assert(capture.lastStatus().message.find("source id") !=
           std::string::npos);

    platform::windows::display::WindowsDxgiDesktopDuplicationCapture
        invalidDxgiCapture;
    modules::display::DisplayCaptureOpenOptions invalidDxgiOptions;
    invalidDxgiOptions.sourceId = 0xFFFFFFFFu;
    assert(!invalidDxgiCapture.open(invalidDxgiOptions));
    assert(!invalidDxgiCapture.isOpen());
    assert(invalidDxgiCapture.lastStatus().code ==
           modules::display::DisplayCaptureStatusCode::SourceNotFound);
    assert(invalidDxgiCapture.captureErrors() > 0);

    modules::display::DisplayCaptureOpenOptions windowDxgiOptions;
    windowDxgiOptions.sourceType =
        modules::display::DisplayCaptureSourceType::Window;
    assert(!invalidDxgiCapture.open(windowDxgiOptions));
    assert(!invalidDxgiCapture.isOpen());
    assert(invalidDxgiCapture.lastStatus().code ==
           modules::display::DisplayCaptureStatusCode::Unsupported);
    return 0;
}
