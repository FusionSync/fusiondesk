#include "fusiondesk/runtime/display/display_capture_options.h"

namespace fusiondesk {
namespace runtime {
namespace display {

modules::display::DisplayCaptureOpenOptions withDefaultRawFrameCaptureTarget(
    modules::display::DisplayCaptureOpenOptions options)
{
    if (options.scaleMode == modules::display::DisplayScaleMode::Source)
        return options;

    if (options.targetWidth == 0 && options.targetHeight == 0) {
        options.targetWidth = DefaultRawFrameCaptureTargetWidth;
        options.targetHeight = DefaultRawFrameCaptureTargetHeight;
    }

    return options;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
