#include "fusiondesk/core/protocol/capability_exchange.h"

#include <utility>

namespace fusiondesk {
namespace protocol {

namespace {

MessageId responseCorrelationId(const PacketEnvelope& request)
{
    return request.correlationId != 0 ? request.correlationId : request.messageId;
}

PacketEnvelope makeBaseResponse(const PacketEnvelope& request, const CapabilityExchangeResponseOptions& options)
{
    PacketEnvelope response;
    response.protocolMajor = request.protocolMajor;
    response.protocolMinor = request.protocolMinor;
    response.sessionId = request.sessionId;
    response.traceId = request.traceId;
    response.messageId = options.messageId;
    response.correlationId = responseCorrelationId(request);
    response.responseTo = request.messageId;
    response.channelId = request.channelId;
    response.channelType = request.channelType;
    response.packetType = PacketType::Exchange;
    response.priority = PacketPriority::Critical;
    response.responseStatus = options.status;
    response.sequence = options.sequence;
    response.monotonicTimestampUsec = options.monotonicTimestampUsec;
    return response;
}

bool isStreamChunk(const PacketEnvelope& packet)
{
    return packet.messageKind == MessageKind::StreamChunk;
}

} // namespace

PacketEnvelope CapabilityExchange::makeRequest(const ProtocolCapabilities& capabilities,
                                               const CapabilityExchangeRequestOptions& options,
                                               CapabilityPayloadOptions payloadOptions)
{
    PacketEnvelope packet;
    packet.protocolMajor = capabilities.protocolMajor;
    packet.protocolMinor = capabilities.protocolMinor;
    packet.sessionId = options.sessionId;
    packet.traceId = options.traceId;
    packet.messageId = options.messageId;
    packet.correlationId = options.correlationId != 0 ? options.correlationId : options.messageId;
    packet.channelId = options.channelId;
    packet.channelType = options.channelType;
    packet.packetType = PacketType::Exchange;
    packet.messageKind = MessageKind::Request;
    packet.priority = options.priority;
    packet.sequence = options.sequence;
    packet.monotonicTimestampUsec = options.monotonicTimestampUsec;
    packet.timeoutMs = options.timeoutMs;
    packet.flags = PacketFlagResponseRequired;
    packet.payload = CapabilityPayloadCodec(payloadOptions).encode(capabilities);
    return packet;
}

PacketEnvelope CapabilityExchange::makeResponse(const PacketEnvelope& request,
                                                const ProtocolCapabilities& capabilities,
                                                const CapabilityExchangeResponseOptions& options,
                                                CapabilityPayloadOptions payloadOptions)
{
    PacketEnvelope response = makeBaseResponse(request, options);
    response.messageKind = options.status == ResponseStatus::Ok ? MessageKind::Response : MessageKind::Error;
    response.payload = options.status == ResponseStatus::Ok
                           ? CapabilityPayloadCodec(payloadOptions).encode(capabilities)
                           : ByteBuffer{};
    return response;
}

PacketEnvelope CapabilityExchange::makeError(const PacketEnvelope& request,
                                             const CapabilityExchangeResponseOptions& options)
{
    CapabilityExchangeResponseOptions errorOptions = options;
    if (errorOptions.status == ResponseStatus::Ok)
        errorOptions.status = ResponseStatus::ProtocolError;

    PacketEnvelope response = makeBaseResponse(request, errorOptions);
    response.messageKind = MessageKind::Error;
    return response;
}

CapabilityDecodeResult CapabilityExchange::decodePayload(const PacketEnvelope& packet,
                                                         CapabilityPayloadOptions payloadOptions)
{
    return CapabilityPayloadCodec(payloadOptions).decode(packet.payload);
}

ProtocolValidationOptions CapabilityExchange::validationOptions(const NegotiatedCapabilities& capabilities)
{
    ProtocolValidationOptions options;
    options.expectedMajor = capabilities.protocolMajor;
    options.minimumMinor = capabilities.protocolMinor;
    options.maxPayloadBytes = capabilities.limits.maxPayloadBytes;
    return options;
}

PacketCodecOptions CapabilityExchange::codecOptions(const NegotiatedCapabilities& capabilities)
{
    PacketCodecOptions options;
    options.maxPayloadBytes = capabilities.limits.maxPayloadBytes;
    return options;
}

NegotiatedPacketLimitResult CapabilityExchange::validatePayloadLimits(const PacketEnvelope& packet,
                                                                      const NegotiatedCapabilities& capabilities)
{
    if (packet.payload.size() > capabilities.limits.maxPayloadBytes) {
        return NegotiatedPacketLimitResult::denied(ResponseStatus::TooLarge,
                                                  "payload exceeds negotiated max payload size");
    }

    if (isStreamChunk(packet) && packet.payload.size() > capabilities.limits.maxStreamChunkBytes) {
        return NegotiatedPacketLimitResult::denied(ResponseStatus::TooLarge,
                                                  "stream chunk exceeds negotiated max stream chunk size");
    }

    return NegotiatedPacketLimitResult::ok();
}

} // namespace protocol
} // namespace fusiondesk
