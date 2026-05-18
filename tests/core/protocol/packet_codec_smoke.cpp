#include <cassert>

#include "fusiondesk/core/protocol/byte_io.h"
#include "fusiondesk/fusiondesk.h"

using namespace fusiondesk;

namespace {

protocol::PacketEnvelope makeRequest()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.traceId = 9;
    packet.messageId = 11;
    packet.correlationId = 11;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::PayloadAck;
    packet.messageKind = protocol::MessageKind::Request;
    packet.priority = protocol::PacketPriority::Interactive;
    packet.sequence = 33;
    packet.monotonicTimestampUsec = 44;
    packet.timeoutMs = 1000;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.payload = {1, 2, 3, 4};
    return packet;
}

protocol::PacketEnvelope makeVideoEvent()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.traceId = 9;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    packet.priority = protocol::PacketPriority::Realtime;
    packet.sequence = 34;
    packet.monotonicTimestampUsec = 45;
    packet.flags = protocol::PacketFlagNoResponseRequired | protocol::PacketFlagKeyFrame;
    packet.payload = {9, 8, 7, 6};
    return packet;
}

void roundTripsRequest()
{
    protocol::PacketCodec codec;
    protocol::PacketEnvelope packet = makeRequest();
    protocol::ByteBuffer bytes = codec.encode(packet);
    protocol::PacketDecodeResult decoded = codec.decode(bytes);

    assert(decoded.ok());
    assert(decoded.packet.protocolMajor == protocol::CurrentProtocolMajor);
    assert(decoded.packet.protocolMinor == protocol::CurrentProtocolMinor);
    assert(decoded.packet.sessionId == packet.sessionId);
    assert(decoded.packet.traceId == packet.traceId);
    assert(decoded.packet.messageId == packet.messageId);
    assert(decoded.packet.correlationId == packet.correlationId);
    assert(decoded.packet.responseTo == packet.responseTo);
    assert(decoded.packet.channelId == packet.channelId);
    assert(decoded.packet.channelType == packet.channelType);
    assert(decoded.packet.packetType == packet.packetType);
    assert(decoded.packet.messageKind == packet.messageKind);
    assert(decoded.packet.priority == packet.priority);
    assert(decoded.packet.sequence == packet.sequence);
    assert(decoded.packet.monotonicTimestampUsec == packet.monotonicTimestampUsec);
    assert(decoded.packet.timeoutMs == packet.timeoutMs);
    assert(decoded.packet.flags == packet.flags);
    assert(decoded.packet.payload == packet.payload);
}

void roundTripsRealtimeVideoEvent()
{
    protocol::PacketCodec codec;
    protocol::PacketEnvelope packet = makeVideoEvent();
    protocol::PacketDecodeResult decoded = codec.decode(codec.encode(packet));

    assert(decoded.ok());
    assert(decoded.packet.sessionId == packet.sessionId);
    assert(decoded.packet.traceId == packet.traceId);
    assert(decoded.packet.packetType == protocol::PacketType::Video);
    assert(decoded.packet.messageKind == protocol::MessageKind::Event);
    assert((decoded.packet.flags & protocol::PacketFlagNoResponseRequired) != 0);
    assert((decoded.packet.flags & protocol::PacketFlagKeyFrame) != 0);
    assert(decoded.packet.messageId == 0);
    assert(decoded.packet.correlationId == 0);
    assert(decoded.packet.timeoutMs == 0);
}

void rejectsBadMagic()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes = codec.encode(makeRequest());
    bytes[0] = 0;

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::InvalidMagic);
}

void rejectsInvalidHeaderLength()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes = codec.encode(makeRequest());
    bytes[5] = 99;

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::InvalidHeaderLength);
}

void rejectsUnsupportedVersion()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes = codec.encode(makeRequest());
    bytes[6] = 0;
    bytes[7] = 2;

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::UnsupportedVersion);
}

void rejectsIncompletePacket()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes(8, 0);

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::Incomplete);
}

void rejectsLengthMismatch()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes = codec.encode(makeRequest());
    bytes.pop_back();

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::LengthMismatch);
}

void rejectsHeaderCrcMismatch()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes = codec.encode(makeRequest());
    bytes[28] ^= 0xffU;

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::HeaderChecksumMismatch);
}

void rejectsPayloadCrcMismatch()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer bytes = codec.encode(makeRequest());
    bytes.back() ^= 0xffU;

    protocol::PacketDecodeResult decoded = codec.decode(bytes);
    assert(decoded.status == protocol::PacketDecodeStatus::PayloadChecksumMismatch);
}

void rejectsPayloadTooLarge()
{
    protocol::PacketCodecOptions options;
    options.maxPayloadBytes = 1;
    protocol::PacketCodec codec(options);

    protocol::PacketDecodeResult decoded = codec.decode(codec.encode(makeRequest()));
    assert(decoded.status == protocol::PacketDecodeStatus::PayloadTooLarge);
}

void rejectsValidationFailure()
{
    protocol::PacketCodec codec;
    protocol::PacketEnvelope packet = makeRequest();
    packet.timeoutMs = 0;

    protocol::PacketDecodeResult decoded = codec.decode(codec.encode(packet));
    assert(decoded.status == protocol::PacketDecodeStatus::ValidationFailed);
}

void inspectsFrameSizeForStreamDecoding()
{
    protocol::PacketCodec codec;
    protocol::ByteBuffer first = codec.encode(makeRequest());
    protocol::ByteBuffer second = first;
    protocol::ByteBuffer stream = first;
    stream.insert(stream.end(), second.begin(), second.end());

    protocol::PacketFrameInspection firstFrame = codec.inspectFrame(stream);
    assert(firstFrame.ok());
    assert(firstFrame.complete);
    assert(firstFrame.frameSize == first.size());

    protocol::ByteBuffer partial(first.begin(), first.begin() + 16);
    protocol::PacketFrameInspection partialFrame = codec.inspectFrame(partial);
    assert(partialFrame.status == protocol::PacketDecodeStatus::Incomplete);
    assert(!partialFrame.complete);
}

void standardProtocolFacadeExposesLifecycleAndByteIo()
{
    protocol::RequestEnvelopeOptions requestOptions;
    requestOptions.sessionId = 90;
    requestOptions.traceId = 91;
    requestOptions.messageId = 92;
    requestOptions.channelId =
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain);
    requestOptions.channelType = protocol::ChannelType::Control;
    requestOptions.packetType = protocol::PacketType::Control;
    requestOptions.priority = protocol::PacketPriority::Critical;
    requestOptions.timeoutMs = 1000;
    requestOptions.payload = {1, 2, 3};

    const protocol::PacketEnvelope request =
        protocol::makeRequestEnvelope(requestOptions);
    assert(request.messageKind == protocol::MessageKind::Request);
    assert(request.correlationId == request.messageId);
    assert(protocol::hasPacketFlag(request.flags, protocol::PacketFlagResponseRequired));

    protocol::ResponseEnvelopeOptions responseOptions;
    responseOptions.status = protocol::ResponseStatus::Ok;
    responseOptions.payload = {4, 5, 6};
    const protocol::PacketEnvelope response =
        protocol::makeResponseEnvelope(request, responseOptions);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseTo == request.messageId);
    assert(response.correlationId == request.correlationId);

    protocol::ByteWriter writer;
    writer.u16(0x1234U);
    writer.u32(0x56789abcU);
    writer.u64(0x1122334455667788ULL);

    protocol::ByteReader reader(writer.bytes());
    std::uint16_t value16 = 0;
    std::uint32_t value32 = 0;
    std::uint64_t value64 = 0;
    assert(reader.u16(value16));
    assert(reader.u32(value32));
    assert(reader.u64(value64));
    assert(value16 == 0x1234U);
    assert(value32 == 0x56789abcU);
    assert(value64 == 0x1122334455667788ULL);
    assert(reader.atEnd());
}

} // namespace

int main()
{
    roundTripsRequest();
    roundTripsRealtimeVideoEvent();
    rejectsBadMagic();
    rejectsInvalidHeaderLength();
    rejectsUnsupportedVersion();
    rejectsIncompletePacket();
    rejectsLengthMismatch();
    rejectsHeaderCrcMismatch();
    rejectsPayloadCrcMismatch();
    rejectsPayloadTooLarge();
    rejectsValidationFailure();
    inspectsFrameSizeForStreamDecoding();
    standardProtocolFacadeExposesLifecycleAndByteIo();
    return 0;
}
