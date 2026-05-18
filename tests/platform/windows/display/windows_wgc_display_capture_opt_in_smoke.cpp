#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#endif

#include "fusiondesk/platform/windows/display/windows_display_capture_factory.h"
#include "fusiondesk/platform/windows/display/windows_graphics_capture.h"
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

using namespace fusiondesk;

namespace {

const char* kValidationEnvironment = "FUSIONDESK_VALIDATE_WGC_CAPTURE";
const char* kWindowValidationEnvironment =
    "FUSIONDESK_VALIDATE_WGC_WINDOW_CAPTURE";
const char* kWgcEnableEnvironment = "FUSIONDESK_ENABLE_WGC_CAPTURE";

bool enabledEnvironment(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
        return false;

    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "on") == 0 ||
           std::strcmp(value, "ON") == 0;
}

void printMessages(const std::vector<std::string>& messages)
{
    for (const std::string& message : messages)
        std::cerr << message << '\n';
}

bool captureFirstFrame(modules::display::IDisplayCapture& capture,
                       const modules::display::DisplayCaptureOpenOptions& options,
                       modules::display::CapturedFrame& frame)
{
    if (!capture.open(options))
        return false;

    for (int attempt = 0; attempt < 20; ++attempt) {
        frame = capture.captureNextFrame(attempt == 0);
        if (frame.width > 0 && frame.height > 0 &&
            frame.strideBytes > 0 && !frame.pixels.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    capture.close();
    return frame.width > 0 && frame.height > 0 &&
           frame.strideBytes > 0 && !frame.pixels.empty();
}

} // namespace

int main()
{
    if (!enabledEnvironment(kValidationEnvironment)) {
        std::cout << "skipped: set " << kValidationEnvironment
                  << "=1 to validate Windows Graphics Capture against a real "
                     "desktop session\n";
        return 0;
    }

#if defined(_WIN32)
    _putenv_s(kWgcEnableEnvironment, "1");
#endif

    platform::windows::display::WindowsDisplayCaptureBackendFactory factory;

    runtime::display::DisplayCaptureBackendSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.sourceType = runtime::display::DisplayCaptureSourceType::Monitor;
    request.acceptedMemoryTypes = {
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    request.acceptedPixelFormats = {
        modules::display::DisplayPixelFormat::Bgra32};
    request.preferZeroCopy = true;
    request.requestedAdapterId = "windows.graphics_capture";

    const runtime::display::DisplayCaptureBackendCreateResult created =
        runtime::display::createSelectedDisplayCapture(factory, request);
    if (!created.ok || created.capture == nullptr) {
        std::cerr << "WGC capture backend could not be selected or created\n";
        printMessages(created.messages);
        return 2;
    }

    if (created.selection.selected.backend !=
        runtime::display::DisplayCaptureBackendKind::WindowsGraphicsCapture) {
        std::cerr << "expected Windows Graphics Capture, selected "
                  << runtime::display::displayCaptureBackendKindName(
                         created.selection.selected.backend)
                  << '\n';
        for (const runtime::display::DisplayCaptureBackendRejection& rejection :
             created.selection.rejected) {
            std::cerr << "rejected "
                      << (rejection.adapterId.empty()
                              ? runtime::display::displayCaptureBackendKindName(
                                    rejection.backend)
                              : rejection.adapterId)
                      << ": " << rejection.reason << '\n';
        }
        return 3;
    }

    if (created.capture->backendId() != "windows.graphics_capture") {
        std::cerr << "WGC capture reported unexpected backend id: "
                  << created.capture->backendId() << '\n';
        return 4;
    }

    modules::display::DisplayCaptureOpenOptions options;
    options.sourceId = 0;
    options.sourceType = modules::display::DisplayCaptureSourceType::Monitor;
    options.targetWidth = 320;
    options.targetHeight = 180;
    options.scaleMode = modules::display::DisplayScaleMode::Fit;
    if (!created.capture->open(options)) {
        const modules::display::DisplayCaptureStatus status =
            created.capture->lastStatus();
        std::cerr << "WGC capture open failed: " << status.message
                  << " native=" << status.nativeCode << '\n';
        return 5;
    }

    modules::display::CapturedFrame frame;
    const bool monitorCaptured =
        captureFirstFrame(*created.capture, options, frame);

    if (!monitorCaptured) {
        const modules::display::DisplayCaptureStatus status =
            created.capture->lastStatus();
        std::cerr << "WGC capture did not return a non-empty frame: "
                  << status.message << " native=" << status.nativeCode << '\n';
        return 6;
    }

    if (created.capture->lastStatus().code !=
        modules::display::DisplayCaptureStatusCode::Ok) {
        std::cerr << "WGC capture returned a frame but did not report OK status\n";
        return 7;
    }

    if (frame.pixelFormat != modules::display::DisplayPixelFormat::Bgra32) {
        std::cerr << "WGC capture returned an unexpected pixel format\n";
        return 8;
    }

    if (frame.strideBytes != frame.width * 4) {
        std::cerr << "WGC capture returned an unexpected stride\n";
        return 9;
    }

    if (frame.width > options.targetWidth || frame.height > options.targetHeight) {
        std::cerr << "WGC capture ignored the requested fit target, captured "
                  << frame.width << "x" << frame.height << '\n';
        return 10;
    }

    std::cout << "WGC captured " << frame.width << "x" << frame.height
              << " frame, bytes=" << frame.pixels.size() << '\n';

    if (enabledEnvironment(kWindowValidationEnvironment)) {
        platform::windows::display::WindowsGraphicsCaptureSourceCatalog catalog;
        const modules::display::DisplayTopologySnapshot topology =
            catalog.snapshot();
        const modules::display::DisplaySourceInfo* windowSource = nullptr;
        for (const modules::display::DisplaySourceInfo& source :
             topology.sources) {
            if (source.sourceType ==
                modules::display::DisplayCaptureSourceType::Window) {
                windowSource = &source;
                break;
            }
        }

        if (windowSource == nullptr) {
            std::cout << "WGC window capture skipped: no visible window source\n";
            return 0;
        }

        runtime::display::DisplayCaptureBackendSelectionRequest windowRequest =
            request;
        windowRequest.sourceType =
            runtime::display::DisplayCaptureSourceType::Window;
        const runtime::display::DisplayCaptureBackendCreateResult
            windowCreated =
                runtime::display::createSelectedDisplayCapture(factory,
                                                               windowRequest);
        if (!windowCreated.ok || windowCreated.capture == nullptr) {
            std::cerr << "WGC window capture backend could not be selected\n";
            printMessages(windowCreated.messages);
            return 11;
        }

        modules::display::DisplayCaptureOpenOptions windowOptions = options;
        windowOptions.sourceType =
            modules::display::DisplayCaptureSourceType::Window;
        windowOptions.sourceId = windowSource->sourceId;
        windowOptions.nativeSourceHandle = windowSource->nativeSourceHandle;
        modules::display::CapturedFrame windowFrame;
        if (!captureFirstFrame(*windowCreated.capture,
                               windowOptions,
                               windowFrame)) {
            const modules::display::DisplayCaptureStatus status =
                windowCreated.capture->lastStatus();
            std::cerr << "WGC window capture did not return a non-empty frame: "
                      << status.message << " native=" << status.nativeCode
                      << '\n';
            return 12;
        }

        std::cout << "WGC window captured " << windowFrame.width << "x"
                  << windowFrame.height
                  << " frame, bytes=" << windowFrame.pixels.size()
                  << '\n';
    }

    return 0;
}
