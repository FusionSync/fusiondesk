#include "fusiondesk/runtime/display/display_capture_geometry.h"

#include <algorithm>

namespace fusiondesk {
namespace runtime {
namespace display {

DisplayCaptureOutputSize resolveDisplayCaptureOutputSize(
    std::uint32_t sourceWidth,
    std::uint32_t sourceHeight,
    const modules::display::DisplayCaptureOpenOptions& options)
{
    if (sourceWidth == 0 || sourceHeight == 0)
        return {};

    DisplayCaptureOutputSize size{sourceWidth, sourceHeight};
    if (options.scaleMode == modules::display::DisplayScaleMode::Source ||
        (options.targetWidth == 0 && options.targetHeight == 0)) {
        return size;
    }

    if (options.scaleMode == modules::display::DisplayScaleMode::Stretch) {
        size.width = options.targetWidth == 0 ? sourceWidth : options.targetWidth;
        size.height = options.targetHeight == 0 ? sourceHeight : options.targetHeight;
        return size;
    }

    if (options.targetWidth == 0 && options.targetHeight > 0) {
        size.height = options.targetHeight;
        size.width = std::max<std::uint32_t>(
            1,
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(sourceWidth) * options.targetHeight /
                sourceHeight));
        return size;
    }

    if (options.targetHeight == 0 && options.targetWidth > 0) {
        size.width = options.targetWidth;
        size.height = std::max<std::uint32_t>(
            1,
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(sourceHeight) * options.targetWidth /
                sourceWidth));
        return size;
    }

    if (options.targetWidth > 0 && options.targetHeight > 0) {
        const std::uint64_t widthLimitedHeight =
            static_cast<std::uint64_t>(sourceHeight) * options.targetWidth /
            sourceWidth;
        if (widthLimitedHeight <= options.targetHeight) {
            size.width = options.targetWidth;
            size.height = std::max<std::uint32_t>(
                1,
                static_cast<std::uint32_t>(widthLimitedHeight));
        } else {
            size.height = options.targetHeight;
            size.width = std::max<std::uint32_t>(
                1,
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(sourceWidth) *
                    options.targetHeight / sourceHeight));
        }
    }

    return size;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
