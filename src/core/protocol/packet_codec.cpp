#include "fusiondesk/core/protocol/packet_codec.h"

#include <algorithm>
#include <utility>

#include "fusiondesk/core/protocol/byte_io.h"

namespace fusiondesk {
namespace protocol {

namespace {

constexpr std::size_t kOffsetMagic = 0;
constexpr std::size_t kOffsetHeaderLength = 4;
constexpr std::size_t kOffsetProtocolMajor = 6;
constexpr std::size_t kOffsetProtocolMinor = 8;
constexpr std::size_t kOffsetChannelId = 10;
constexpr std::size_t kOffsetChannelType = 12;
constexpr std::size_t kOffsetPacketType = 14;
constexpr std::size_t kOffsetMessageKind = 16;
constexpr std::size_t kOffsetPriority = 18;
constexpr std::size_t kOffsetResponseStatus = 20;
constexpr std::size_t kOffsetFlags = 24;
constexpr std::size_t kOffsetSessionId = 28;
constexpr std::size_t kOffsetTraceId = 36;
constexpr std::size_t kOffsetMessageId = 44;
constexpr std::size_t kOffsetCorrelationId = 52;
constexpr std::size_t kOffsetResponseTo = 60;
constexpr std::size_t kOffsetSequence = 68;
constexpr std::size_t kOffsetTimestamp = 76;
constexpr std::size_t kOffsetTimeout = 84;
constexpr std::size_t kOffsetPayloadLength = 88;
constexpr std::size_t kOffsetHeaderCrc = 92;
constexpr std::size_t kOffsetPayloadCrc = 96;

std::uint8_t readU8(const ByteBuffer& bytes, std::size_t offset)
{
    std::uint8_t value = 0;
    (void)readU8At(bytes, offset, value);
    return value;
}

std::uint16_t readU16(const ByteBuffer& bytes, std::size_t offset)
{
    std::uint16_t value = 0;
    (void)readU16At(bytes, offset, value);
    return value;
}

std::uint32_t readU32(const ByteBuffer& bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    (void)readU32At(bytes, offset, value);
    return value;
}

std::uint64_t readU64(const ByteBuffer& bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    (void)readU64At(bytes, offset, value);
    return value;
}

ByteBuffer headerForChecksum(ByteBuffer bytes)
{
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(kOffsetHeaderCrc),
              bytes.begin() + static_cast<std::ptrdiff_t>(kOffsetHeaderCrc + sizeof(std::uint32_t)),
              0);
    return bytes;
}

PacketDecodeResult fail(PacketDecodeStatus status, std::string message)
{
    PacketDecodeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

} // namespace

PacketCodec::PacketCodec(PacketCodecOptions options)
    : options_(options)
{
}

ByteBuffer PacketCodec::encode(const PacketEnvelope& packet) const
{
    const std::uint32_t payloadLength = static_cast<std::uint32_t>(packet.payload.size());
    ByteBuffer bytes(PacketWireHeaderLength + packet.payload.size(), 0);

    (void)writeU32At(bytes, kOffsetMagic, PacketWireMagic);
    (void)writeU16At(bytes, kOffsetHeaderLength, PacketWireHeaderLength);
    (void)writeU16At(bytes, kOffsetProtocolMajor, packet.protocolMajor);
    (void)writeU16At(bytes, kOffsetProtocolMinor, packet.protocolMinor);
    (void)writeU16At(bytes, kOffsetChannelId, packet.channelId);
    (void)writeU16At(bytes, kOffsetChannelType, static_cast<std::uint16_t>(packet.channelType));
    (void)writeU16At(bytes, kOffsetPacketType, static_cast<std::uint16_t>(packet.packetType));
    (void)writeU16At(bytes, kOffsetMessageKind, static_cast<std::uint16_t>(packet.messageKind));
    (void)writeU8At(bytes, kOffsetPriority, static_cast<std::uint8_t>(packet.priority));
    (void)writeU16At(bytes,
                     kOffsetResponseStatus,
                     static_cast<std::uint16_t>(packet.responseStatus));
    (void)writeU32At(bytes, kOffsetFlags, packet.flags);
    (void)writeU64At(bytes, kOffsetSessionId, packet.sessionId);
    (void)writeU64At(bytes, kOffsetTraceId, packet.traceId);
    (void)writeU64At(bytes, kOffsetMessageId, packet.messageId);
    (void)writeU64At(bytes, kOffsetCorrelationId, packet.correlationId);
    (void)writeU64At(bytes, kOffsetResponseTo, packet.responseTo);
    (void)writeU64At(bytes, kOffsetSequence, packet.sequence);
    (void)writeU64At(bytes, kOffsetTimestamp, packet.monotonicTimestampUsec);
    (void)writeU32At(bytes, kOffsetTimeout, packet.timeoutMs);
    (void)writeU32At(bytes, kOffsetPayloadLength, payloadLength);

    std::copy(packet.payload.begin(), packet.payload.end(), bytes.begin() + PacketWireHeaderLength);
    (void)writeU32At(bytes, kOffsetPayloadCrc, crc32(packet.payload.data(), packet.payload.size()));

    ByteBuffer header = headerForChecksum(ByteBuffer(bytes.begin(), bytes.begin() + PacketWireHeaderLength));
    (void)writeU32At(bytes, kOffsetHeaderCrc, crc32(header.data(), header.size()));
    return bytes;
}

PacketFrameInspection PacketCodec::inspectFrame(const ByteBuffer& bytes) const
{
    PacketFrameInspection result;
    if (bytes.size() < PacketWireHeaderLength) {
        result.status = PacketDecodeStatus::Incomplete;
        result.message = "packet is shorter than header";
        return result;
    }

    if (readU32(bytes, kOffsetMagic) != PacketWireMagic) {
        result.status = PacketDecodeStatus::InvalidMagic;
        result.message = "packet magic mismatch";
        return result;
    }

    const std::uint16_t headerLength = readU16(bytes, kOffsetHeaderLength);
    if (headerLength != PacketWireHeaderLength) {
        result.status = PacketDecodeStatus::InvalidHeaderLength;
        result.message = "unsupported packet header length";
        return result;
    }

    const std::uint16_t protocolMajor = readU16(bytes, kOffsetProtocolMajor);
    if (protocolMajor != CurrentProtocolMajor) {
        result.status = PacketDecodeStatus::UnsupportedVersion;
        result.message = "unsupported protocol major version";
        return result;
    }

    const std::uint32_t payloadLength = readU32(bytes, kOffsetPayloadLength);
    if (payloadLength > options_.maxPayloadBytes) {
        result.status = PacketDecodeStatus::PayloadTooLarge;
        result.message = "payload exceeds codec limit";
        return result;
    }

    result.frameSize = static_cast<std::size_t>(headerLength) + static_cast<std::size_t>(payloadLength);
    result.complete = bytes.size() >= result.frameSize;
    result.status = result.complete ? PacketDecodeStatus::Ok : PacketDecodeStatus::Incomplete;
    if (!result.complete)
        result.message = "packet payload is incomplete";
    return result;
}

PacketDecodeResult PacketCodec::decode(const ByteBuffer& bytes) const
{
    if (bytes.size() < PacketWireHeaderLength)
        return fail(PacketDecodeStatus::Incomplete, "packet is shorter than header");

    if (readU32(bytes, kOffsetMagic) != PacketWireMagic)
        return fail(PacketDecodeStatus::InvalidMagic, "packet magic mismatch");

    const std::uint16_t headerLength = readU16(bytes, kOffsetHeaderLength);
    if (headerLength != PacketWireHeaderLength)
        return fail(PacketDecodeStatus::InvalidHeaderLength, "unsupported packet header length");

    const std::uint16_t protocolMajor = readU16(bytes, kOffsetProtocolMajor);
    if (protocolMajor != CurrentProtocolMajor)
        return fail(PacketDecodeStatus::UnsupportedVersion, "unsupported protocol major version");

    const std::uint32_t payloadLength = readU32(bytes, kOffsetPayloadLength);
    if (payloadLength > options_.maxPayloadBytes)
        return fail(PacketDecodeStatus::PayloadTooLarge, "payload exceeds codec limit");

    if (bytes.size() != static_cast<std::size_t>(headerLength) + static_cast<std::size_t>(payloadLength))
        return fail(PacketDecodeStatus::LengthMismatch, "packet length does not match payload length");

    if (options_.validateHeaderCrc) {
        ByteBuffer header(bytes.begin(), bytes.begin() + headerLength);
        const std::uint32_t expectedHeaderCrc = readU32(header, kOffsetHeaderCrc);
        header = headerForChecksum(std::move(header));
        const std::uint32_t actualHeaderCrc = crc32(header.data(), header.size());
        if (actualHeaderCrc != expectedHeaderCrc)
            return fail(PacketDecodeStatus::HeaderChecksumMismatch, "header crc mismatch");
    }

    ByteBuffer payload(bytes.begin() + headerLength, bytes.end());
    if (options_.validatePayloadCrc) {
        const std::uint32_t expectedPayloadCrc = readU32(bytes, kOffsetPayloadCrc);
        const std::uint32_t actualPayloadCrc = crc32(payload.data(), payload.size());
        if (actualPayloadCrc != expectedPayloadCrc)
            return fail(PacketDecodeStatus::PayloadChecksumMismatch, "payload crc mismatch");
    }

    PacketEnvelope packet;
    packet.protocolMajor = protocolMajor;
    packet.protocolMinor = readU16(bytes, kOffsetProtocolMinor);
    packet.channelId = readU16(bytes, kOffsetChannelId);
    packet.channelType = static_cast<ChannelType>(readU16(bytes, kOffsetChannelType));
    packet.packetType = static_cast<PacketType>(readU16(bytes, kOffsetPacketType));
    packet.messageKind = static_cast<MessageKind>(readU16(bytes, kOffsetMessageKind));
    packet.priority = static_cast<PacketPriority>(readU8(bytes, kOffsetPriority));
    packet.responseStatus = static_cast<ResponseStatus>(readU16(bytes, kOffsetResponseStatus));
    packet.flags = readU32(bytes, kOffsetFlags);
    packet.sessionId = readU64(bytes, kOffsetSessionId);
    packet.traceId = readU64(bytes, kOffsetTraceId);
    packet.messageId = readU64(bytes, kOffsetMessageId);
    packet.correlationId = readU64(bytes, kOffsetCorrelationId);
    packet.responseTo = readU64(bytes, kOffsetResponseTo);
    packet.sequence = readU64(bytes, kOffsetSequence);
    packet.monotonicTimestampUsec = readU64(bytes, kOffsetTimestamp);
    packet.timeoutMs = readU32(bytes, kOffsetTimeout);
    packet.payload = std::move(payload);

    ProtocolValidationOptions validationOptions;
    validationOptions.maxPayloadBytes = options_.maxPayloadBytes;
    ProtocolValidationResult validation = ProtocolValidator(validationOptions).validate(packet);
    if (!validation.valid)
        return fail(PacketDecodeStatus::ValidationFailed, validation.message);

    PacketDecodeResult result;
    result.status = PacketDecodeStatus::Ok;
    result.packet = std::move(packet);
    return result;
}

std::uint32_t PacketCodec::crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

} // namespace protocol
} // namespace fusiondesk
