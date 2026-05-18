#ifndef FUSIONDESK_PROTOCOL_CAPABILITY_EXCHANGE_H
#define FUSIONDESK_PROTOCOL_CAPABILITY_EXCHANGE_H

#include <string>

#include "fusiondesk/core/protocol/capability_negotiation.h"
#include "fusiondesk/core/protocol/packet_codec.h"
#include "fusiondesk/core/protocol/protocol_validator.h"

namespace fusiondesk {
namespace protocol {

struct CapabilityExchangeRequestOptions
{
    SessionId sessionId = 0;
    TraceId traceId = 0;
    MessageId messageId = 0;
    MessageId correlationId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    std::uint32_t timeoutMs = 5000;
    ChannelId channelId = static_cast<ChannelId>(ChannelIdValue::UserAuthMain);
    ChannelType channelType = ChannelType::Control;
    PacketPriority priority = PacketPriority::Critical;
};

struct CapabilityExchangeResponseOptions
{
    MessageId messageId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    ResponseStatus status = ResponseStatus::Ok;
};

struct NegotiatedPacketLimitResult
{
    bool allowed = false;
    ResponseStatus status = ResponseStatus::TooLarge;
    std::string message;

    static NegotiatedPacketLimitResult ok()
    {
        return {true, ResponseStatus::Ok, {}};
    }

    static NegotiatedPacketLimitResult denied(ResponseStatus status, std::string message)
    {
        return {false, status, message};
    }
};

class CapabilityExchange
{
public:
    static PacketEnvelope makeRequest(const ProtocolCapabilities& capabilities,
                                      const CapabilityExchangeRequestOptions& options,
                                      CapabilityPayloadOptions payloadOptions = {});

    static PacketEnvelope makeResponse(const PacketEnvelope& request,
                                       const ProtocolCapabilities& capabilities,
                                       const CapabilityExchangeResponseOptions& options,
                                       CapabilityPayloadOptions payloadOptions = {});

    static PacketEnvelope makeError(const PacketEnvelope& request,
                                    const CapabilityExchangeResponseOptions& options);

    static CapabilityDecodeResult decodePayload(const PacketEnvelope& packet,
                                                CapabilityPayloadOptions payloadOptions = {});

    static ProtocolValidationOptions validationOptions(const NegotiatedCapabilities& capabilities);
    static PacketCodecOptions codecOptions(const NegotiatedCapabilities& capabilities);
    static NegotiatedPacketLimitResult validatePayloadLimits(const PacketEnvelope& packet,
                                                            const NegotiatedCapabilities& capabilities);
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_CAPABILITY_EXCHANGE_H
