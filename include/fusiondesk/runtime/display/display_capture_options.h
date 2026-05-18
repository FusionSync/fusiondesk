#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_OPTIONS_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_OPTIONS_H

#include <cstdint>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace runtime {
namespace display {

constexpr std::uint32_t DefaultRawFrameCaptureTargetWidth = 1280;
constexpr std::uint32_t DefaultRawFrameCaptureTargetHeight = 720;

modules::display::DisplayCaptureOpenOptions withDefaultRawFrameCaptureTarget(
    modules::display::DisplayCaptureOpenOptions options);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_OPTIONS_H
