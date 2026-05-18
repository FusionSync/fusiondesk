#ifndef FUSIONDESK_PROTOCOL_PACKET_ENVELOPE_H
#define FUSIONDESK_PROTOCOL_PACKET_ENVELOPE_H

#include <cstdint>

#include "fusiondesk/core/protocol/protocol_types.h"

namespace fusiondesk {
namespace protocol {

struct PacketEnvelope
{
    std::uint16_t protocolMajor = CurrentProtocolMajor;
    std::uint16_t protocolMinor = CurrentProtocolMinor;
    SessionId sessionId = 0;
    TraceId traceId = 0;
    MessageId messageId = 0;
    MessageId correlationId = 0;
    MessageId responseTo = 0;
    ChannelId channelId = 0;
    ChannelType channelType = ChannelType::Standard;
    PacketType packetType = PacketType::Heartbeat;
    MessageKind messageKind = MessageKind::Event;
    PacketPriority priority = PacketPriority::Normal;
    ResponseStatus responseStatus = ResponseStatus::Ok;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    std::uint32_t timeoutMs = 0;
    PacketFlags flags = PacketFlagNone;
    ByteBuffer payload;
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_PACKET_ENVELOPE_H
