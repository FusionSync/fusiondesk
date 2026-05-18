#include "fusiondesk/runtime/display/display_color_conversion.h"

#include <algorithm>
#include <limits>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

std::uint8_t clampByte(int value)
{
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

std::uint8_t yFromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return clampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
}

std::uint8_t uFromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return clampByte((-38 * r - 74 * g + 112 * b + 128) / 256 + 128);
}

std::uint8_t vFromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return clampByte((112 * r - 94 * g - 18 * b + 128) / 256 + 128);
}

bool validateBgraFrame(const modules::display::CapturedFrame& frame,
                       std::string& error)
{
    if (frame.pixelFormat != modules::display::DisplayPixelFormat::Bgra32) {
        error = "BGRA to NV12 conversion requires Bgra32 input";
        return false;
    }
    if (frame.width == 0 || frame.height == 0) {
        error = "BGRA to NV12 conversion requires non-zero dimensions";
        return false;
    }
    if ((frame.width % 2) != 0 || (frame.height % 2) != 0) {
        error = "BGRA to NV12 conversion requires even dimensions";
        return false;
    }
    const std::uint64_t minimumStride =
        static_cast<std::uint64_t>(frame.width) * 4ULL;
    if (frame.strideBytes < minimumStride) {
        error = "BGRA to NV12 conversion stride is smaller than width * 4";
        return false;
    }
    const std::uint64_t requiredBytes =
        static_cast<std::uint64_t>(frame.strideBytes) * frame.height;
    if (requiredBytes >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        requiredBytes > frame.pixels.size()) {
        error = "BGRA to NV12 conversion input pixels are truncated";
        return false;
    }
    return true;
}

void readBgra(const modules::display::CapturedFrame& frame,
              std::uint32_t x,
              std::uint32_t y,
              std::uint8_t& r,
              std::uint8_t& g,
              std::uint8_t& b)
{
    const std::size_t offset =
        static_cast<std::size_t>(y) * frame.strideBytes +
        static_cast<std::size_t>(x) * 4U;
    b = frame.pixels[offset];
    g = frame.pixels[offset + 1];
    r = frame.pixels[offset + 2];
}

bool validateNv12Frame(const Nv12Frame& frame, std::string& error)
{
    if (frame.width == 0 || frame.height == 0) {
        error = "NV12 to BGRA conversion requires non-zero dimensions";
        return false;
    }
    if ((frame.width % 2) != 0 || (frame.height % 2) != 0) {
        error = "NV12 to BGRA conversion requires even dimensions";
        return false;
    }
    if (frame.yStrideBytes < frame.width ||
        frame.uvStrideBytes < frame.width) {
        error = "NV12 to BGRA conversion stride is smaller than width";
        return false;
    }
    const std::uint64_t requiredBytes =
        static_cast<std::uint64_t>(frame.yStrideBytes) * frame.height +
        static_cast<std::uint64_t>(frame.uvStrideBytes) * (frame.height / 2U);
    if (requiredBytes >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        requiredBytes > frame.bytes.size()) {
        error = "NV12 to BGRA conversion input pixels are truncated";
        return false;
    }
    return true;
}

void writeBgraFromYuv(std::uint8_t yValue,
                      std::uint8_t uValue,
                      std::uint8_t vValue,
                      std::uint8_t* target)
{
    const int c = static_cast<int>(yValue) - 16;
    const int d = static_cast<int>(uValue) - 128;
    const int e = static_cast<int>(vValue) - 128;
    const std::uint8_t r = clampByte((298 * c + 409 * e + 128) >> 8);
    const std::uint8_t g =
        clampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
    const std::uint8_t b = clampByte((298 * c + 516 * d + 128) >> 8);
    target[0] = b;
    target[1] = g;
    target[2] = r;
    target[3] = 255;
}

} // namespace

std::size_t Nv12Frame::yPlaneSize() const
{
    return static_cast<std::size_t>(yStrideBytes) * height;
}

std::size_t Nv12Frame::uvPlaneOffset() const
{
    return yPlaneSize();
}

std::size_t Nv12Frame::uvPlaneSize() const
{
    return static_cast<std::size_t>(uvStrideBytes) * (height / 2U);
}

BgraToNv12Result convertBgraToNv12(
    const modules::display::CapturedFrame& frame)
{
    BgraToNv12Result result;
    if (!validateBgraFrame(frame, result.error))
        return result;

    result.frame.width = frame.width;
    result.frame.height = frame.height;
    result.frame.yStrideBytes = frame.width;
    result.frame.uvStrideBytes = frame.width;
    result.frame.bytes.assign(result.frame.yPlaneSize() +
                                  result.frame.uvPlaneSize(),
                              0);

    for (std::uint32_t y = 0; y < frame.height; ++y) {
        std::uint8_t* targetRow =
            result.frame.bytes.data() +
            static_cast<std::size_t>(result.frame.yStrideBytes) * y;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            readBgra(frame, x, y, r, g, b);
            targetRow[x] = yFromRgb(r, g, b);
        }
    }

    std::uint8_t* uvPlane =
        result.frame.bytes.data() + result.frame.uvPlaneOffset();
    for (std::uint32_t y = 0; y < frame.height; y += 2) {
        std::uint8_t* uvRow =
            uvPlane + static_cast<std::size_t>(result.frame.uvStrideBytes) *
                          (y / 2U);
        for (std::uint32_t x = 0; x < frame.width; x += 2) {
            int uSum = 0;
            int vSum = 0;
            for (std::uint32_t dy = 0; dy < 2; ++dy) {
                for (std::uint32_t dx = 0; dx < 2; ++dx) {
                    std::uint8_t r = 0;
                    std::uint8_t g = 0;
                    std::uint8_t b = 0;
                    readBgra(frame, x + dx, y + dy, r, g, b);
                    uSum += uFromRgb(r, g, b);
                    vSum += vFromRgb(r, g, b);
                }
            }
            uvRow[x] = static_cast<std::uint8_t>((uSum + 2) / 4);
            uvRow[x + 1] = static_cast<std::uint8_t>((vSum + 2) / 4);
        }
    }

    result.ok = true;
    return result;
}

Nv12ToBgraResult convertNv12ToBgra(const Nv12Frame& frame)
{
    Nv12ToBgraResult result;
    if (!validateNv12Frame(frame, result.error))
        return result;

    result.width = frame.width;
    result.height = frame.height;
    result.strideBytes = frame.width * 4U;
    result.bytes.assign(
        static_cast<std::size_t>(result.strideBytes) * frame.height,
        0);

    const std::uint8_t* yPlane = frame.bytes.data();
    const std::uint8_t* uvPlane = frame.bytes.data() + frame.uvPlaneOffset();
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const std::uint8_t* yRow =
            yPlane + static_cast<std::size_t>(frame.yStrideBytes) * y;
        const std::uint8_t* uvRow =
            uvPlane + static_cast<std::size_t>(frame.uvStrideBytes) *
                          (y / 2U);
        std::uint8_t* targetRow =
            result.bytes.data() +
            static_cast<std::size_t>(result.strideBytes) * y;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::uint32_t uvOffset = x & ~1U;
            writeBgraFromYuv(yRow[x],
                             uvRow[uvOffset],
                             uvRow[uvOffset + 1],
                             targetRow + static_cast<std::size_t>(x) * 4U);
        }
    }

    result.ok = true;
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
