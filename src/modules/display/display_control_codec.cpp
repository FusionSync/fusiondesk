#include "fusiondesk/modules/display/display_control_codec.h"

namespace fusiondesk {
namespace modules {
namespace display {

namespace {

constexpr std::uint32_t DisplayControlMagic = 0x46445343; // "FDSC"
constexpr std::uint16_t DisplayControlVersion = 1;
constexpr std::uint16_t DisplayControlPayloadSize = 20;

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

bool supportedOperation(DisplayControlOperation operation)
{
    return operation == DisplayControlOperation::RequestKeyframe ||
           operation == DisplayControlOperation::KeyframeScheduled;
}

bool supportedReason(DisplayKeyframeReason reason)
{
    return reason == DisplayKeyframeReason::Unknown ||
           reason == DisplayKeyframeReason::FirstFrameTimeout ||
           reason == DisplayKeyframeReason::DecoderReset ||
           reason == DisplayKeyframeReason::FrameGap ||
           reason == DisplayKeyframeReason::Reconnect ||
           reason == DisplayKeyframeReason::Manual;
}

} // namespace

protocol::ByteBuffer encodeDisplayControlPayload(const DisplayControlPayload& payload)
{
    protocol::ByteBuffer output;
    output.reserve(DisplayControlPayloadSize);
    appendU32(output, DisplayControlMagic);
    appendU16(output, DisplayControlVersion);
    appendU16(output, static_cast<std::uint16_t>(payload.operation));
    appendU16(output, static_cast<std::uint16_t>(payload.reason));
    appendU16(output, 0);
    appendU64(output, payload.frameId);
    return output;
}

DisplayControlDecodeResult decodeDisplayControlPayload(const protocol::ByteBuffer& payload)
{
    DisplayControlDecodeResult result;
    if (payload.size() != DisplayControlPayloadSize) {
        result.error = "display control payload size is invalid";
        return result;
    }

    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t operation = 0;
    std::uint16_t reason = 0;
    std::uint16_t reserved = 0;
    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, version) ||
        !readU16(payload, offset, operation) ||
        !readU16(payload, offset, reason) ||
        !readU16(payload, offset, reserved) ||
        !readU64(payload, offset, result.payload.frameId)) {
        result.error = "display control payload is truncated";
        return result;
    }

    if (magic != DisplayControlMagic) {
        result.error = "display control magic is invalid";
        return result;
    }
    if (version != DisplayControlVersion) {
        result.error = "display control version is unsupported";
        return result;
    }
    if (reserved != 0) {
        result.error = "display control reserved field is not zero";
        return result;
    }

    result.payload.operation = static_cast<DisplayControlOperation>(operation);
    result.payload.reason = static_cast<DisplayKeyframeReason>(reason);
    if (!supportedOperation(result.payload.operation)) {
        result.error = "display control operation is unsupported";
        return result;
    }
    if (!supportedReason(result.payload.reason)) {
        result.error = "display control keyframe reason is unsupported";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace display
} // namespace modules
} // namespace fusiondesk
