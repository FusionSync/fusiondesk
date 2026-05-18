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
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

using namespace fusiondesk;

namespace {

const char* kValidationEnvironment = "FUSIONDESK_VALIDATE_DXGI_CAPTURE";
const char* kDxgiEnableEnvironment = "FUSIONDESK_ENABLE_DXGI_CAPTURE";

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

void printMessages(
    const std::vector<std::string>& messages)
{
    for (const std::string& message : messages)
        std::cerr << message << '\n';
}

} // namespace

int main()
{
    if (!enabledEnvironment(kValidationEnvironment)) {
        std::cout << "skipped: set " << kValidationEnvironment
                  << "=1 to validate DXGI Desktop Duplication against a real "
                     "desktop session\n";
        return 0;
    }

#if defined(_WIN32)
    _putenv_s(kDxgiEnableEnvironment, "1");
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

    const runtime::display::DisplayCaptureBackendCreateResult created =
        runtime::display::createSelectedDisplayCapture(factory, request);
    if (!created.ok || created.capture == nullptr) {
        std::cerr << "DXGI capture backend could not be selected or created\n";
        printMessages(created.messages);
        return 2;
    }

    if (created.selection.selected.backend !=
        runtime::display::DisplayCaptureBackendKind::WindowsDxgiDesktopDuplication) {
        std::cerr << "expected DXGI Desktop Duplication, selected "
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

    if (created.capture->backendId() != "windows.dxgi.desktop_duplication") {
        std::cerr << "DXGI capture reported unexpected backend id: "
                  << created.capture->backendId() << '\n';
        return 4;
    }

    modules::display::DisplayCaptureOpenOptions options;
    options.sourceId = 0;
    options.targetWidth = 320;
    options.targetHeight = 180;
    options.scaleMode = modules::display::DisplayScaleMode::Fit;
    if (!created.capture->open(options)) {
        const modules::display::DisplayCaptureStatus status =
            created.capture->lastStatus();
        std::cerr << "DXGI capture open failed: " << status.message
                  << " native=" << status.nativeCode << '\n';
        return 5;
    }

    modules::display::CapturedFrame frame;
    for (int attempt = 0; attempt < 20; ++attempt) {
        frame = created.capture->captureNextFrame(attempt == 0);
        if (frame.width > 0 && frame.height > 0 && frame.strideBytes > 0 &&
            !frame.pixels.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    created.capture->close();

    if (frame.width == 0 || frame.height == 0 || frame.strideBytes == 0 ||
        frame.pixels.empty()) {
        const modules::display::DisplayCaptureStatus status =
            created.capture->lastStatus();
        std::cerr << "DXGI capture did not return a non-empty frame: "
                  << status.message << " native=" << status.nativeCode << '\n';
        return 6;
    }

    if (created.capture->lastStatus().code !=
        modules::display::DisplayCaptureStatusCode::Ok) {
        std::cerr << "DXGI capture returned a frame but did not report OK status\n";
        return 7;
    }

    if (frame.pixelFormat != modules::display::DisplayPixelFormat::Bgra32) {
        std::cerr << "DXGI capture returned an unexpected pixel format\n";
        return 8;
    }

    if (frame.strideBytes != frame.width * 4) {
        std::cerr << "DXGI capture returned an unexpected stride\n";
        return 9;
    }

    if (frame.width > options.targetWidth || frame.height > options.targetHeight) {
        std::cerr << "DXGI capture ignored the requested fit target, captured "
                  << frame.width << "x" << frame.height << '\n';
        return 10;
    }

    std::cout << "DXGI captured " << frame.width << "x" << frame.height
              << " frame, bytes=" << frame.pixels.size() << '\n';
    return 0;
}
