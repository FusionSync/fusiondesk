#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_GEOMETRY_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_GEOMETRY_H

#include <cstdint>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace runtime {
namespace display {

struct DisplayCaptureOutputSize
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

DisplayCaptureOutputSize resolveDisplayCaptureOutputSize(
    std::uint32_t sourceWidth,
    std::uint32_t sourceHeight,
    const modules::display::DisplayCaptureOpenOptions& options);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_GEOMETRY_H
