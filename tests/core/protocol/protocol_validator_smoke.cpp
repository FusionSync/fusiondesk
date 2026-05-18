#include <cassert>

#include "fusiondesk/core/protocol/protocol_validator.h"

using namespace fusiondesk;

namespace {

protocol::PacketEnvelope makeBase()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.traceId = 9;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::PayloadAck;
    packet.priority = protocol::PacketPriority::Interactive;
    return packet;
}

void acceptsValidRequest()
{
    protocol::ProtocolValidator validator;
    protocol::PacketEnvelope packet = makeBase();
    packet.messageId = 1;
    packet.correlationId = 1;
    packet.messageKind = protocol::MessageKind::Request;
    packet.timeoutMs = 1000;

    protocol::ProtocolValidationResult result = validator.validate(packet);
    assert(result.valid);
    assert(result.status == protocol::ResponseStatus::Ok);
}

void rejectsRequestWithoutTimeout()
{
    protocol::ProtocolValidator validator;
    protocol::PacketEnvelope packet = makeBase();
    packet.messageId = 1;
    packet.correlationId = 1;
    packet.messageKind = protocol::MessageKind::Request;
    packet.timeoutMs = 0;

    protocol::ProtocolValidationResult result = validator.validate(packet);
    assert(!result.valid);
    assert(result.status == protocol::ResponseStatus::InvalidArgument);
}

void rejectsResponseWithoutResponseTo()
{
    protocol::ProtocolValidator validator;
    protocol::PacketEnvelope packet = makeBase();
    packet.messageId = 2;
    packet.correlationId = 1;
    packet.responseTo = 0;
    packet.messageKind = protocol::MessageKind::Response;
    packet.responseStatus = protocol::ResponseStatus::Ok;

    protocol::ProtocolValidationResult result = validator.validate(packet);
    assert(!result.valid);
    assert(result.status == protocol::ResponseStatus::InvalidArgument);
}

void acceptsFireAndForgetEvent()
{
    protocol::ProtocolValidator validator;
    protocol::PacketEnvelope packet = makeBase();
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    packet.priority = protocol::PacketPriority::Realtime;

    protocol::ProtocolValidationResult result = validator.validate(packet);
    assert(result.valid);
}

void rejectsOversizedPayload()
{
    protocol::ProtocolValidationOptions options;
    options.maxPayloadBytes = 4;
    protocol::ProtocolValidator validator(options);
    protocol::PacketEnvelope packet = makeBase();
    packet.messageKind = protocol::MessageKind::Event;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    packet.payload = {1, 2, 3, 4, 5};

    protocol::ProtocolValidationResult result = validator.validate(packet);
    assert(!result.valid);
    assert(result.status == protocol::ResponseStatus::TooLarge);
}

} // namespace

int main()
{
    acceptsValidRequest();
    rejectsRequestWithoutTimeout();
    rejectsResponseWithoutResponseTo();
    acceptsFireAndForgetEvent();
    rejectsOversizedPayload();
    return 0;
}

