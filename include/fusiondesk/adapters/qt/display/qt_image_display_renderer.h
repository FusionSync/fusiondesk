#ifndef FUSIONDESK_ADAPTERS_QT_DISPLAY_QT_IMAGE_DISPLAY_RENDERER_H
#define FUSIONDESK_ADAPTERS_QT_DISPLAY_QT_IMAGE_DISPLAY_RENDERER_H

#include <cstdint>
#include <functional>

#include "fusiondesk/modules/display/display_interfaces.h"

class QImage;

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {

class QtImageDisplayRenderer final : public modules::display::IDisplayRenderer
{
public:
    using FrameRenderedHandler = std::function<void(const QImage&)>;

    void setFrameRenderedHandler(FrameRenderedHandler handler);

    bool attachSurface(const modules::display::DisplayRenderSurface& surface) override;
    void detachSurface() override;
    bool render(const modules::display::DecodedFrame& frame) override;

    bool surfaceAttached() const;
    int renderedFrames() const;
    std::uint32_t lastWidth() const;
    std::uint32_t lastHeight() const;

private:
    modules::display::DisplayRenderSurface surface_;
    FrameRenderedHandler handler_;
    bool surfaceAttached_ = false;
    int renderedFrames_ = 0;
    std::uint32_t lastWidth_ = 0;
    std::uint32_t lastHeight_ = 0;
};

} // namespace display
} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_DISPLAY_QT_IMAGE_DISPLAY_RENDERER_H
