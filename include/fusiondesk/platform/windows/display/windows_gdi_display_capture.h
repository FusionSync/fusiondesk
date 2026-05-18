#ifndef FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_GDI_DISPLAY_CAPTURE_H
#define FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_GDI_DISPLAY_CAPTURE_H

#include <cstdint>
#include <memory>
#include <vector>

#include "fusiondesk/modules/display/display_interfaces.h"
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

class WindowsGdiDisplaySourceCatalog final : public modules::display::IDisplaySourceCatalog
{
public:
    modules::display::DisplayTopologySnapshot snapshot() const override;
};

class WindowsGdiDisplayCapture final : public modules::display::IDisplayCapture
{
public:
    bool open(const modules::display::DisplayCaptureOpenOptions& options) override;
    void close() override;
    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override;

    bool isOpen() const;
    int captureErrors() const override;
    std::string backendId() const override;
    modules::display::DisplayCaptureStatus lastStatus() const override;
    modules::display::DisplayTopologySnapshot topologySnapshot() const;

private:
    void recordOk();
    void recordCaptureError(modules::display::DisplayCaptureStatusCode code,
                            int nativeCode,
                            const char* message,
                            bool recoverable);

    modules::display::DisplayCaptureOpenOptions options_;
    modules::display::DisplayTopologySnapshot topology_;
    modules::display::DisplayCaptureStatus lastStatus_;
    std::uint64_t nextFrameId_ = 1;
    bool opened_ = false;
    int captureErrors_ = 0;
};

class WindowsGdiDisplayCaptureFactory final
    : public fusiondesk::runtime::display::IDisplayCaptureBackendFactory
{
public:
    std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override;

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;

    std::shared_ptr<modules::display::IDisplaySourceCatalog>
    createSourceCatalog(
        const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const override;
};

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_GDI_DISPLAY_CAPTURE_H
