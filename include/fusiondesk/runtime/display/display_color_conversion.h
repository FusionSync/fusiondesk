#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_COLOR_CONVERSION_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_COLOR_CONVERSION_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace runtime {
namespace display {

struct Nv12Frame
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t yStrideBytes = 0;
    std::uint32_t uvStrideBytes = 0;
    protocol::ByteBuffer bytes;

    std::size_t yPlaneSize() const;
    std::size_t uvPlaneOffset() const;
    std::size_t uvPlaneSize() const;
};

struct BgraToNv12Result
{
    bool ok = false;
    Nv12Frame frame;
    std::string error;
};

struct Nv12ToBgraResult
{
    bool ok = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    protocol::ByteBuffer bytes;
    std::string error;
};

BgraToNv12Result convertBgraToNv12(
    const modules::display::CapturedFrame& frame);

Nv12ToBgraResult convertNv12ToBgra(const Nv12Frame& frame);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_COLOR_CONVERSION_H
