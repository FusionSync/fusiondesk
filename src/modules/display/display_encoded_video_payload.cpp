#include "fusiondesk/modules/display/display_encoded_video_payload.h"

#include <limits>

namespace fusiondesk {
namespace modules {
namespace display {

namespace {

constexpr std::uint32_t EncodedVideoMagic = 0x46445346; // "FDSF"
constexpr std::uint16_t EncodedVideoVersion = 1;
constexpr std::uint16_t EncodedVideoHeaderSize = 64;
constexpr std::uint16_t EncodedVideoFlagKeyFrame = 0x0001;

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

bool readU16(const protocol::ByteBuffer& input,
             std::size_t& offset,
             std::uint16_t& value)
{
    if (offset + 2 > input.size())
        return false;
    value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8) |
        static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

bool readU32(const protocol::ByteBuffer& input,
             std::size_t& offset,
             std::uint32_t& value)
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

bool readU64(const protocol::ByteBuffer& input,
             std::size_t& offset,
             std::uint64_t& value)
{
    if (offset + 8 > input.size())
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + i]);
    offset += 8;
    return true;
}

bool isKnownCodec(DisplayEncodedVideoCodec codec)
{
    return codec == DisplayEncodedVideoCodec::H264 ||
           codec == DisplayEncodedVideoCodec::H265 ||
           codec == DisplayEncodedVideoCodec::Av1;
}

bool isKnownBitstreamFormat(DisplayEncodedVideoBitstreamFormat format)
{
    return format == DisplayEncodedVideoBitstreamFormat::AnnexB ||
           format == DisplayEncodedVideoBitstreamFormat::Avcc;
}

DisplayEncodedVideoPayload normalizedPayload(
    const DisplayEncodedVideoPayload& payload)
{
    DisplayEncodedVideoPayload result = payload;
    if (result.codedWidth == 0)
        result.codedWidth = result.frame.width;
    if (result.codedHeight == 0)
        result.codedHeight = result.frame.height;
    if (result.visibleWidth == 0)
        result.visibleWidth = result.frame.width;
    if (result.visibleHeight == 0)
        result.visibleHeight = result.frame.height;
    return result;
}

bool validateMetadata(const DisplayEncodedVideoPayload& payload,
                      std::string& error)
{
    if (!isKnownCodec(payload.codec)) {
        error = "encoded video codec is unsupported";
        return false;
    }
    if (!isKnownBitstreamFormat(payload.bitstreamFormat)) {
        error = "encoded video bitstream format is unsupported";
        return false;
    }
    if (payload.codedWidth == 0 || payload.codedHeight == 0 ||
        payload.visibleWidth == 0 || payload.visibleHeight == 0) {
        error = "encoded video coded and visible dimensions are required";
        return false;
    }
    if (payload.frame.width == 0 || payload.frame.height == 0 ||
        payload.frame.width != payload.visibleWidth ||
        payload.frame.height != payload.visibleHeight) {
        error = "encoded video frame dimensions must match visible dimensions";
        return false;
    }
    if (payload.frame.strideBytes == 0) {
        error = "encoded video decoded stride is required";
        return false;
    }
    if (payload.frame.pixelFormat == DisplayPixelFormat::Unknown) {
        error = "encoded video decoded pixel format is required";
        return false;
    }
    if (payload.bitstream.empty()) {
        error = "encoded video bitstream is required";
        return false;
    }
    if (payload.sequenceHeader.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        payload.bitstream.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        error = "encoded video payload section is too large";
        return false;
    }
    return true;
}

} // namespace

const char* displayEncodedVideoCodecName(DisplayEncodedVideoCodec codec)
{
    switch (codec) {
    case DisplayEncodedVideoCodec::Unknown:
        return "unknown";
    case DisplayEncodedVideoCodec::H264:
        return "h264";
    case DisplayEncodedVideoCodec::H265:
        return "h265";
    case DisplayEncodedVideoCodec::Av1:
        return "av1";
    }
    return "unknown";
}

const char* displayEncodedVideoBitstreamFormatName(
    DisplayEncodedVideoBitstreamFormat format)
{
    switch (format) {
    case DisplayEncodedVideoBitstreamFormat::Unknown:
        return "unknown";
    case DisplayEncodedVideoBitstreamFormat::AnnexB:
        return "annex_b";
    case DisplayEncodedVideoBitstreamFormat::Avcc:
        return "avcc";
    }
    return "unknown";
}

protocol::ByteBuffer encodeDisplayEncodedVideoPayload(
    const DisplayEncodedVideoPayload& payload)
{
    const DisplayEncodedVideoPayload normalized = normalizedPayload(payload);
    std::string error;
    if (!validateMetadata(normalized, error))
        return {};

    protocol::ByteBuffer output;
    output.reserve(EncodedVideoHeaderSize + normalized.sequenceHeader.size() +
                   normalized.bitstream.size());
    appendU32(output, EncodedVideoMagic);
    appendU16(output, EncodedVideoVersion);
    appendU16(output, EncodedVideoHeaderSize);
    appendU16(output, static_cast<std::uint16_t>(normalized.codec));
    appendU16(output,
              static_cast<std::uint16_t>(normalized.bitstreamFormat));
    appendU16(output, static_cast<std::uint16_t>(normalized.frame.pixelFormat));
    appendU16(output,
              normalized.frame.keyFrame ? EncodedVideoFlagKeyFrame : 0);
    appendU64(output, normalized.frame.frameId);
    appendU64(output, normalized.frame.monotonicTimestampUsec);
    appendU32(output, normalized.codedWidth);
    appendU32(output, normalized.codedHeight);
    appendU32(output, normalized.visibleWidth);
    appendU32(output, normalized.visibleHeight);
    appendU32(output, normalized.frame.strideBytes);
    appendU32(output,
              static_cast<std::uint32_t>(normalized.sequenceHeader.size()));
    appendU32(output, static_cast<std::uint32_t>(normalized.bitstream.size()));
    appendU32(output, 0);
    output.insert(output.end(),
                  normalized.sequenceHeader.begin(),
                  normalized.sequenceHeader.end());
    output.insert(output.end(),
                  normalized.bitstream.begin(),
                  normalized.bitstream.end());
    return output;
}

DisplayEncodedVideoPayloadDecodeResult decodeDisplayEncodedVideoPayload(
    const protocol::ByteBuffer& payload)
{
    DisplayEncodedVideoPayloadDecodeResult result;
    if (payload.size() < EncodedVideoHeaderSize) {
        result.error = "encoded video payload is smaller than header";
        return result;
    }

    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t headerSize = 0;
    std::uint16_t codecValue = 0;
    std::uint16_t bitstreamFormatValue = 0;
    std::uint16_t pixelFormatValue = 0;
    std::uint16_t flags = 0;
    std::uint32_t sequenceHeaderBytes = 0;
    std::uint32_t bitstreamBytes = 0;
    std::uint32_t reserved32 = 0;

    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, version) ||
        !readU16(payload, offset, headerSize) ||
        !readU16(payload, offset, codecValue) ||
        !readU16(payload, offset, bitstreamFormatValue) ||
        !readU16(payload, offset, pixelFormatValue) ||
        !readU16(payload, offset, flags) ||
        !readU64(payload, offset, result.payload.frame.frameId) ||
        !readU64(payload,
                 offset,
                 result.payload.frame.monotonicTimestampUsec) ||
        !readU32(payload, offset, result.payload.codedWidth) ||
        !readU32(payload, offset, result.payload.codedHeight) ||
        !readU32(payload, offset, result.payload.visibleWidth) ||
        !readU32(payload, offset, result.payload.visibleHeight) ||
        !readU32(payload, offset, result.payload.frame.strideBytes) ||
        !readU32(payload, offset, sequenceHeaderBytes) ||
        !readU32(payload, offset, bitstreamBytes) ||
        !readU32(payload, offset, reserved32)) {
        result.error = "encoded video header is truncated";
        return result;
    }

    if (magic != EncodedVideoMagic) {
        result.error = "encoded video magic is invalid";
        return result;
    }
    if (version != EncodedVideoVersion) {
        result.error = "encoded video version is unsupported";
        return result;
    }
    if (headerSize != EncodedVideoHeaderSize || headerSize > payload.size()) {
        result.error = "encoded video header size is invalid";
        return result;
    }
    if (reserved32 != 0) {
        result.error = "encoded video reserved fields must be zero";
        return result;
    }

    const std::uint64_t expectedSize =
        static_cast<std::uint64_t>(headerSize) + sequenceHeaderBytes +
        bitstreamBytes;
    if (expectedSize != payload.size()) {
        result.error = "encoded video payload section sizes are invalid";
        return result;
    }

    result.payload.codec = static_cast<DisplayEncodedVideoCodec>(codecValue);
    result.payload.bitstreamFormat =
        static_cast<DisplayEncodedVideoBitstreamFormat>(bitstreamFormatValue);
    result.payload.frame.pixelFormat =
        static_cast<DisplayPixelFormat>(pixelFormatValue);
    result.payload.frame.keyFrame =
        (flags & EncodedVideoFlagKeyFrame) != 0;
    result.payload.frame.width = result.payload.visibleWidth;
    result.payload.frame.height = result.payload.visibleHeight;

    std::string error;
    DisplayEncodedVideoPayload metadata = result.payload;
    metadata.sequenceHeader.assign(
        payload.begin() + static_cast<std::ptrdiff_t>(headerSize),
        payload.begin() + static_cast<std::ptrdiff_t>(
            headerSize + sequenceHeaderBytes));
    metadata.bitstream.assign(
        payload.begin() + static_cast<std::ptrdiff_t>(
            headerSize + sequenceHeaderBytes),
        payload.end());
    if (!validateMetadata(metadata, error)) {
        result.error = error;
        return result;
    }

    result.payload = std::move(metadata);
    result.payload.frame.payload = result.payload.bitstream;
    result.ok = true;
    return result;
}

} // namespace display
} // namespace modules
} // namespace fusiondesk
