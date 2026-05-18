#include <cassert>

#include "fusiondesk/core/network/channel_allowlist.h"

using namespace fusiondesk;

namespace {

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control, protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::Exchange, protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

protocol::PacketEnvelope makePacket()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    return packet;
}

void allowsNegotiatedPacket()
{
    network::ChannelAllowlistValidator validator(makeNegotiated());
    network::ChannelAllowlistResult result = validator.validate(makePacket());

    assert(result.allowed);
    assert(result.responseStatus == protocol::ResponseStatus::Ok);
}

void rejectsUnnegotiatedChannelType()
{
    network::ChannelAllowlistValidator validator(makeNegotiated());
    protocol::PacketEnvelope packet = makePacket();
    packet.channelType = protocol::ChannelType::Bulk;

    network::ChannelAllowlistResult result = validator.validate(packet);

    assert(!result.allowed);
    assert(result.status == network::ChannelAllowlistStatus::ChannelTypeNotNegotiated);
    assert(result.responseStatus == protocol::ResponseStatus::Unsupported);
}

void rejectsUnnegotiatedPacketType()
{
    network::ChannelAllowlistValidator validator(makeNegotiated());
    protocol::PacketEnvelope packet = makePacket();
    packet.packetType = protocol::PacketType::Clipboard;

    network::ChannelAllowlistResult result = validator.validate(packet);

    assert(!result.allowed);
    assert(result.status == network::ChannelAllowlistStatus::PacketTypeNotNegotiated);
    assert(result.responseStatus == protocol::ResponseStatus::Unsupported);
}

void rejectsUnnegotiatedMessageKind()
{
    network::ChannelAllowlistValidator validator(makeNegotiated());
    protocol::PacketEnvelope packet = makePacket();
    packet.messageKind = protocol::MessageKind::StreamChunk;

    network::ChannelAllowlistResult result = validator.validate(packet);

    assert(!result.allowed);
    assert(result.status == network::ChannelAllowlistStatus::MessageKindNotNegotiated);
    assert(result.responseStatus == protocol::ResponseStatus::Unsupported);
}

} // namespace

int main()
{
    allowsNegotiatedPacket();
    rejectsUnnegotiatedChannelType();
    rejectsUnnegotiatedPacketType();
    rejectsUnnegotiatedMessageKind();
    return 0;
}
