#include "fusiondesk/modules/display/display_frame_codec.h"

#include <limits>

namespace fusiondesk {
namespace modules {
namespace display {

namespace {

constexpr std::uint32_t RawFrameMagic = 0x46445246; // "FDRF"
constexpr std::uint16_t RawFrameVersion = 1;
constexpr std::uint16_t RawFrameHeaderSize = 40;
constexpr std::uint16_t RawFrameFlagKeyFrame = 0x0001;

std::uint32_t bytesPerPixel(DisplayPixelFormat format)
{
    switch (format) {
    case DisplayPixelFormat::Bgra32:
    case DisplayPixelFormat::Rgba32:
        return 4;
    case DisplayPixelFormat::Rgb24:
        return 3;
    case DisplayPixelFormat::Gray8:
        return 1;
    case DisplayPixelFormat::Unknown:
        return 0;
    }
    return 0;
}

void appendU16(protocol::ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendU32(protocol::ByteBuffer& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendU64(protocol::ByteBuffer& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

bool readU16(const protocol::ByteBuffer& input, std::size_t& offset, std::uint16_t& value)
{
    if (offset + 2 > input.size())
        return false;
    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[offset]) << 8) |
                                       static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

bool readU32(const protocol::ByteBuffer& input, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4 > input.size())
        return false;
    value = (static_cast<std::uint32_t>(input[offset]) << 24) |
            (static_cast<std::uint32_t>(input[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 2]) << 8) |
            static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool readU64(const protocol::ByteBuffer& input, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8 > input.size())
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + i]);
    offset += 8;
    return true;
}

bool validateRawFrameGeometry(std::uint32_t width,
                              std::uint32_t height,
                              std::uint32_t strideBytes,
                              DisplayPixelFormat pixelFormat,
                              std::size_t pixelBytes,
                              std::string& error)
{
    const std::uint32_t bpp = bytesPerPixel(pixelFormat);
    if (width == 0 || height == 0) {
        error = "raw frame width and height are required";
        return false;
    }
    if (bpp == 0) {
        error = "raw frame pixel format is unsupported";
        return false;
    }

    const std::uint64_t minimumStride = static_cast<std::uint64_t>(width) * bpp;
    if (strideBytes < minimumStride) {
        error = "raw frame stride is smaller than width * bytesPerPixel";
        return false;
    }

    const std::uint64_t requiredBytes = static_cast<std::uint64_t>(strideBytes) * height;
    if (requiredBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        requiredBytes > pixelBytes) {
        error = "raw frame payload is shorter than stride * height";
        return false;
    }

    return true;
}

DisplayCodecRuntimeInfo rawFrameCodecInfo()
{
    DisplayCodecRuntimeInfo info;
    info.selected = true;
    info.adapterId = "raw_frame";
    info.codec = "raw_bgra";
    info.backend = "raw_frame";
    info.fallback = true;
    info.hardwareAccelerated = false;
    info.zeroCopy = false;
    info.lowLatency = true;
    info.deltaFrames = false;
    info.selectionMode = "fallback";
    return info;
}

} // namespace

protocol::ByteBuffer encodeRawFramePayload(const CapturedFrame& frame)
{
    std::string error;
    if (!validateRawFrameGeometry(frame.width,
                                  frame.height,
                                  frame.strideBytes,
                                  frame.pixelFormat,
                                  frame.pixels.size(),
                                  error)) {
        return {};
    }

    protocol::ByteBuffer output;
    output.reserve(RawFrameHeaderSize + frame.pixels.size());
    appendU32(output, RawFrameMagic);
    appendU16(output, RawFrameVersion);
    appendU16(output, RawFrameHeaderSize);
    appendU64(output, frame.frameId);
    appendU64(output, frame.monotonicTimestampUsec);
    appendU32(output, frame.width);
    appendU32(output, frame.height);
    appendU32(output, frame.strideBytes);
    appendU16(output, static_cast<std::uint16_t>(frame.pixelFormat));
    appendU16(output, frame.keyFrame ? RawFrameFlagKeyFrame : 0);
    output.insert(output.end(), frame.pixels.begin(), frame.pixels.end());
    return output;
}

RawFrameDecodeResult decodeRawFramePayload(const protocol::ByteBuffer& payload)
{
    RawFrameDecodeResult result;
    if (payload.size() < RawFrameHeaderSize) {
        result.error = "raw frame payload is smaller than header";
        return result;
    }

    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t headerSize = 0;
    std::uint16_t pixelFormatValue = 0;
    std::uint16_t flags = 0;
    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, version) ||
        !readU16(payload, offset, headerSize)) {
        result.error = "raw frame header is truncated";
        return result;
    }

    if (magic != RawFrameMagic) {
        result.error = "raw frame magic is invalid";
        return result;
    }
    if (version != RawFrameVersion) {
        result.error = "raw frame version is unsupported";
        return result;
    }
    if (headerSize != RawFrameHeaderSize || headerSize > payload.size()) {
        result.error = "raw frame header size is invalid";
        return result;
    }

    if (!readU64(payload, offset, result.frame.frameId) ||
        !readU64(payload, offset, result.frame.monotonicTimestampUsec) ||
        !readU32(payload, offset, result.frame.width) ||
        !readU32(payload, offset, result.frame.height) ||
        !readU32(payload, offset, result.frame.strideBytes) ||
        !readU16(payload, offset, pixelFormatValue) ||
        !readU16(payload, offset, flags)) {
        result.error = "raw frame metadata is truncated";
        return result;
    }

    result.frame.pixelFormat = static_cast<DisplayPixelFormat>(pixelFormatValue);
    result.frame.keyFrame = (flags & RawFrameFlagKeyFrame) != 0;
    const std::size_t pixelOffset = headerSize;
    const std::size_t pixelBytes = payload.size() - pixelOffset;
    if (!validateRawFrameGeometry(result.frame.width,
                                  result.frame.height,
                                  result.frame.strideBytes,
                                  result.frame.pixelFormat,
                                  pixelBytes,
                                  result.error)) {
        return result;
    }

    result.frame.pixels.assign(payload.begin() + static_cast<std::ptrdiff_t>(pixelOffset),
                               payload.end());
    result.ok = true;
    return result;
}

EncodedFrame RawFrameEncoder::encode(const CapturedFrame& frame)
{
    EncodedFrame encoded;
    encoded.frameId = frame.frameId;
    encoded.keyFrame = frame.keyFrame;
    encoded.width = frame.width;
    encoded.height = frame.height;
    encoded.strideBytes = frame.strideBytes;
    encoded.pixelFormat = frame.pixelFormat;
    encoded.monotonicTimestampUsec = frame.monotonicTimestampUsec;
    encoded.payload = encodeRawFramePayload(frame);
    return encoded;
}

DecodedFrame RawFrameDecoder::decode(const EncodedFrame& frame)
{
    RawFrameDecodeResult decoded = decodeRawFramePayload(frame.payload);
    if (decoded.ok)
        return decoded.frame;
    return {};
}

DisplayCodecRuntimeInfo RawFrameEncoder::codecRuntimeInfo() const
{
    return rawFrameCodecInfo();
}

DisplayCodecRuntimeInfo RawFrameDecoder::codecRuntimeInfo() const
{
    return rawFrameCodecInfo();
}

} // namespace display
} // namespace modules
} // namespace fusiondesk
