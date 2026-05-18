#ifndef FUSIONDESK_PROTOCOL_MESSAGE_LIFECYCLE_H
#define FUSIONDESK_PROTOCOL_MESSAGE_LIFECYCLE_H

#include <limits>

#include "fusiondesk/core/protocol/packet_envelope.h"

namespace fusiondesk {
namespace protocol {

inline bool hasPacketFlag(PacketFlags flags, PacketFlags flag)
{
    return (flags & flag) != 0;
}

inline PacketFlags withPacketFlag(PacketFlags flags, PacketFlags flag)
{
    return flags | flag;
}

inline PacketFlags withoutPacketFlag(PacketFlags flags, PacketFlags flag)
{
    return flags & ~flag;
}

inline MessageId responseCorrelationId(const PacketEnvelope& request)
{
    return request.correlationId != 0 ? request.correlationId : request.messageId;
}

inline MessageKind responseMessageKindForStatus(ResponseStatus status)
{
    switch (status) {
    case ResponseStatus::Ok:
        return MessageKind::Response;
    case ResponseStatus::Accepted:
        return MessageKind::Ack;
    case ResponseStatus::Progress:
        return MessageKind::Progress;
    case ResponseStatus::InvalidArgument:
    case ResponseStatus::Unauthorized:
    case ResponseStatus::DeniedByPolicy:
    case ResponseStatus::Unsupported:
    case ResponseStatus::NotFound:
    case ResponseStatus::Conflict:
    case ResponseStatus::Busy:
    case ResponseStatus::Timeout:
    case ResponseStatus::Cancelled:
    case ResponseStatus::TooLarge:
    case ResponseStatus::BackPressure:
    case ResponseStatus::ChannelUnavailable:
    case ResponseStatus::Failed:
    case ResponseStatus::InternalError:
    case ResponseStatus::ProtocolError:
        return MessageKind::Error;
    }
    return MessageKind::Error;
}

struct RequestEnvelopeOptions
{
    SessionId sessionId = 0;
    TraceId traceId = 0;
    MessageId messageId = 0;
    MessageId correlationId = 0;
    ChannelId channelId = 0;
    ChannelType channelType = ChannelType::Standard;
    PacketType packetType = PacketType::Control;
    PacketPriority priority = PacketPriority::Normal;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    std::uint32_t timeoutMs = 0;
    PacketFlags flags = PacketFlagResponseRequired;
    ByteBuffer payload;
};

struct ResponseEnvelopeOptions
{
    MessageId messageId = 0;
    ResponseStatus status = ResponseStatus::Ok;
    PacketPriority priority = PacketPriority::Normal;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    PacketFlags flags = PacketFlagNone;
    ByteBuffer payload;
};

inline PacketEnvelope makeRequestEnvelope(const RequestEnvelopeOptions& options)
{
    PacketEnvelope packet;
    packet.sessionId = options.sessionId;
    packet.traceId = options.traceId;
    packet.messageId = options.messageId;
    packet.correlationId =
        options.correlationId != 0 ? options.correlationId : options.messageId;
    packet.channelId = options.channelId;
    packet.channelType = options.channelType;
    packet.packetType = options.packetType;
    packet.messageKind = MessageKind::Request;
    packet.priority = options.priority;
    packet.sequence = options.sequence;
    packet.monotonicTimestampUsec = options.monotonicTimestampUsec;
    packet.timeoutMs = options.timeoutMs;
    packet.flags = options.flags;
    packet.payload = options.payload;
    return packet;
}

inline MessageId defaultResponseMessageId(const PacketEnvelope& request)
{
    if (request.messageId != 0 &&
        request.messageId != std::numeric_limits<MessageId>::max()) {
        return request.messageId + 1U;
    }
    return 1;
}

inline PacketEnvelope makeResponseEnvelope(const PacketEnvelope& request,
                                           const ResponseEnvelopeOptions& options = {})
{
    PacketEnvelope packet;
    packet.sessionId = request.sessionId;
    packet.traceId = request.traceId;
    packet.messageId = options.messageId != 0 ? options.messageId
                                              : defaultResponseMessageId(request);
    packet.correlationId = responseCorrelationId(request);
    packet.responseTo = request.messageId;
    packet.channelId = request.channelId;
    packet.channelType = request.channelType;
    packet.packetType = request.packetType;
    packet.messageKind = responseMessageKindForStatus(options.status);
    packet.priority = options.priority;
    packet.responseStatus = options.status;
    packet.sequence = options.sequence;
    packet.monotonicTimestampUsec = options.monotonicTimestampUsec;
    packet.flags = options.flags;
    packet.payload = options.payload;
    return packet;
}

inline PacketEnvelope makeAckEnvelope(const PacketEnvelope& request,
                                      ResponseEnvelopeOptions options = {})
{
    options.status = ResponseStatus::Accepted;
    return makeResponseEnvelope(request, options);
}

inline PacketEnvelope makeErrorEnvelope(const PacketEnvelope& request,
                                        ResponseStatus status,
                                        ResponseEnvelopeOptions options = {})
{
    options.status = status == ResponseStatus::Ok ? ResponseStatus::Failed : status;
    return makeResponseEnvelope(request, options);
}

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_MESSAGE_LIFECYCLE_H
