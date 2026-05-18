#ifndef FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_GRAPHICS_CAPTURE_H
#define FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_GRAPHICS_CAPTURE_H

#include <cstdint>
#include <memory>
#include <string>

#include "fusiondesk/modules/display/display_interfaces.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

class WindowsGraphicsCaptureSourceCatalog final
    : public modules::display::IDisplaySourceCatalog
{
public:
    modules::display::DisplayTopologySnapshot snapshot() const override;
};

class WindowsGraphicsCapture final : public modules::display::IDisplayCapture
{
public:
    WindowsGraphicsCapture();
    ~WindowsGraphicsCapture() override;

    bool open(const modules::display::DisplayCaptureOpenOptions& options) override;
    void close() override;
    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override;

    bool isOpen() const;
    int captureErrors() const override;
    std::string backendId() const override;
    modules::display::DisplayCaptureStatus lastStatus() const override;

private:
    struct Impl;

    void recordOk();
    void recordCaptureError(modules::display::DisplayCaptureStatusCode code,
                            int nativeCode,
                            const char* message,
                            bool recoverable);
    void recordFrameTimeout();

    modules::display::DisplayCaptureOpenOptions options_;
    std::unique_ptr<Impl> impl_;
    modules::display::DisplayCaptureStatus lastStatus_;
    modules::display::DisplaySourceGeometry sourceGeometry_;
    std::uint64_t nextFrameId_ = 1;
    bool opened_ = false;
    int captureErrors_ = 0;
};

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_GRAPHICS_CAPTURE_H
