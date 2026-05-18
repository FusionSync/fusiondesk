#ifndef FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_CURSOR_OVERLAY_H
#define FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_CURSOR_OVERLAY_H

#include <cstdint>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

struct WindowsCursorFrameMappingInput
{
    std::int32_t sourceX = 0;
    std::int32_t sourceY = 0;
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    std::uint32_t frameWidth = 0;
    std::uint32_t frameHeight = 0;
    std::int32_t cursorScreenX = 0;
    std::int32_t cursorScreenY = 0;
    std::uint32_t hotspotX = 0;
    std::uint32_t hotspotY = 0;
    std::uint32_t cursorWidth = 0;
    std::uint32_t cursorHeight = 0;
};

struct WindowsCursorFrameRect
{
    bool insideFrame = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

WindowsCursorFrameRect mapWindowsCursorToFrame(
    const WindowsCursorFrameMappingInput& input);

bool overlayWindowsCursorOnBgraFrame(
    modules::display::CapturedFrame& frame,
    const modules::display::DisplaySourceGeometry& sourceGeometry,
    const modules::display::DisplayCaptureOpenOptions& options);

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_CURSOR_OVERLAY_H
