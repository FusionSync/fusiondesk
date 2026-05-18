#include <cassert>

#include "fusiondesk/core/protocol/capability_exchange.h"

using namespace fusiondesk;

namespace {

protocol::ProtocolCapabilities makeCapabilities()
{
    protocol::ProtocolCapabilities capabilities;
    capabilities.features.bits = protocol::feature::Display | protocol::feature::Mouse;
    capabilities.channelTypes = {protocol::ChannelType::Control, protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::Exchange, protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::StreamChunk};
    capabilities.limits.maxPayloadBytes = 128;
    capabilities.limits.maxStreamChunkBytes = 16;
    return capabilities;
}

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::ProtocolCapabilities capabilities = makeCapabilities();
    protocol::NegotiatedCapabilities negotiated;
    negotiated.protocolMajor = capabilities.protocolMajor;
    negotiated.protocolMinor = capabilities.protocolMinor;
    negotiated.features = capabilities.features;
    negotiated.channelTypes = capabilities.channelTypes;
    negotiated.packetTypes = capabilities.packetTypes;
    negotiated.messageKinds = capabilities.messageKinds;
    negotiated.limits = capabilities.limits;
    return negotiated;
}

protocol::CapabilityExchangeRequestOptions makeRequestOptions()
{
    protocol::CapabilityExchangeRequestOptions options;
    options.sessionId = 7;
    options.traceId = 9;
    options.messageId = 11;
    options.sequence = 13;
    options.monotonicTimestampUsec = 15;
    options.timeoutMs = 5000;
    return options;
}

void requestCarriesCapabilityPayload()
{
    protocol::PacketEnvelope request =
        protocol::CapabilityExchange::makeRequest(makeCapabilities(), makeRequestOptions());

    assert(request.packetType == protocol::PacketType::Exchange);
    assert(request.messageKind == protocol::MessageKind::Request);
    assert(request.priority == protocol::PacketPriority::Critical);
    assert(request.flags == protocol::PacketFlagResponseRequired);
    assert(request.messageId == 11);
    assert(request.correlationId == 11);
    assert(request.timeoutMs == 5000);

    protocol::CapabilityDecodeResult decoded = protocol::CapabilityExchange::decodePayload(request);
    assert(decoded.ok());
    assert(decoded.capabilities.features.bits == makeCapabilities().features.bits);

    protocol::ProtocolValidationResult validation =
        protocol::ProtocolValidator(protocol::CapabilityExchange::validationOptions(makeNegotiated())).validate(request);
    assert(validation.valid);
}

void responsePreservesCorrelation()
{
    protocol::PacketEnvelope request =
        protocol::CapabilityExchange::makeRequest(makeCapabilities(), makeRequestOptions());

    protocol::CapabilityExchangeResponseOptions options;
    options.messageId = 21;
    options.sequence = 22;
    options.monotonicTimestampUsec = 23;
    protocol::PacketEnvelope response =
        protocol::CapabilityExchange::makeResponse(request, makeCapabilities(), options);

    assert(response.packetType == protocol::PacketType::Exchange);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);
    assert(response.messageId == 21);
    assert(response.correlationId == request.correlationId);
    assert(response.responseTo == request.messageId);
    assert(protocol::CapabilityExchange::decodePayload(response).ok());

    protocol::ProtocolValidationResult validation =
        protocol::ProtocolValidator(protocol::CapabilityExchange::validationOptions(makeNegotiated())).validate(response);
    assert(validation.valid);
}

void errorResponseHasNoCapabilityPayload()
{
    protocol::PacketEnvelope request =
        protocol::CapabilityExchange::makeRequest(makeCapabilities(), makeRequestOptions());

    protocol::CapabilityExchangeResponseOptions options;
    options.messageId = 31;
    options.status = protocol::ResponseStatus::Unsupported;
    protocol::PacketEnvelope error = protocol::CapabilityExchange::makeError(request, options);

    assert(error.messageKind == protocol::MessageKind::Error);
    assert(error.responseStatus == protocol::ResponseStatus::Unsupported);
    assert(error.responseTo == request.messageId);
    assert(error.payload.empty());
}

void negotiatedOptionsDriveValidatorAndCodec()
{
    protocol::NegotiatedCapabilities negotiated = makeNegotiated();
    negotiated.limits.maxPayloadBytes = 1;

    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain);
    packet.channelType = protocol::ChannelType::Control;
    packet.packetType = protocol::PacketType::Exchange;
    packet.messageKind = protocol::MessageKind::Event;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    packet.payload = {1, 2};

    protocol::ProtocolValidationResult validation =
        protocol::ProtocolValidator(protocol::CapabilityExchange::validationOptions(negotiated)).validate(packet);
    assert(!validation.valid);
    assert(validation.status == protocol::ResponseStatus::TooLarge);

    protocol::PacketCodec codec(protocol::CapabilityExchange::codecOptions(negotiated));
    protocol::PacketDecodeResult decoded = codec.decode(codec.encode(packet));
    assert(decoded.status == protocol::PacketDecodeStatus::PayloadTooLarge);
}

void negotiatedStreamChunkLimitIsChecked()
{
    protocol::NegotiatedCapabilities negotiated = makeNegotiated();
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::StreamChunk;
    packet.correlationId = 100;
    packet.payload.assign(17, 1);

    protocol::NegotiatedPacketLimitResult result =
        protocol::CapabilityExchange::validatePayloadLimits(packet, negotiated);

    assert(!result.allowed);
    assert(result.status == protocol::ResponseStatus::TooLarge);
}

} // namespace

int main()
{
    requestCarriesCapabilityPayload();
    responsePreservesCorrelation();
    errorResponseHasNoCapabilityPayload();
    negotiatedOptionsDriveValidatorAndCodec();
    negotiatedStreamChunkLimitIsChecked();
    return 0;
}
