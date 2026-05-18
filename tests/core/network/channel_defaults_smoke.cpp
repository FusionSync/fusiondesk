#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"

using namespace fusiondesk;

namespace {

class FakeChannel : public network::IChannel
{
public:
    FakeChannel(protocol::ChannelId id, protocol::ChannelType type)
        : id_(id)
        , type_(type)
    {
    }

    protocol::ChannelId id() const override
    {
        return id_;
    }

    protocol::ChannelType type() const override
    {
        return type_;
    }

    bool isOpen() const override
    {
        return true;
    }

    network::SendResult send(const protocol::PacketEnvelope&) override
    {
        return network::SendResult::sent();
    }

private:
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
};

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video,
                                protocol::PacketType::Filesystem,
                                protocol::PacketType::Printer};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

protocol::PacketEnvelope packet(protocol::PacketType packetType)
{
    protocol::PacketEnvelope envelope;
    envelope.sessionId = 7;
    envelope.packetType = packetType;
    envelope.messageKind = protocol::MessageKind::Event;
    envelope.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    envelope.channelType = protocol::ChannelType::Video;
    return envelope;
}

protocol::PacketEnvelope packet(protocol::PacketType packetType,
                                protocol::PacketPriority priority)
{
    protocol::PacketEnvelope envelope = packet(packetType);
    envelope.priority = priority;
    return envelope;
}

bool containsPacket(const std::vector<protocol::PacketType>& packets,
                    protocol::PacketType packetType)
{
    for (protocol::PacketType packet : packets) {
        if (packet == packetType)
            return true;
    }
    return false;
}

void defaultMvpChannelsRegisterAndBecomeReady()
{
    network::ChannelRegistry registry(makeNegotiated());
    std::vector<network::ChannelSpec> specs = network::defaultMvpChannelSpecs();

    assert(specs.size() == 3);
    for (const network::ChannelSpec& spec : specs) {
        assert(registry.registerSpec(spec).ok);
        assert(registry.bind(spec.key, std::make_shared<FakeChannel>(spec.key.channelId, spec.key.channelType)).ok);
        assert(registry.markReady(spec.key, {}).ok);
        assert(registry.isReady(spec.key));
    }

    assert(registry.snapshots().size() == 3);
}

void mapsKnownPacketsToExpectedPriority()
{
    assert(network::defaultSendOptions(packet(protocol::PacketType::Heartbeat)).priority ==
           protocol::PacketPriority::Critical);
    assert(network::defaultSendOptions(packet(protocol::PacketType::Video)).priority ==
           protocol::PacketPriority::Realtime);
    assert(network::defaultSendOptions(packet(protocol::PacketType::PayloadAck)).priority ==
           protocol::PacketPriority::Interactive);
    assert(network::defaultSendOptions(packet(protocol::PacketType::Clipboard)).priority ==
           protocol::PacketPriority::Interactive);
    assert(network::defaultSendOptions(
               packet(protocol::PacketType::Clipboard,
                      protocol::PacketPriority::Bulk)).priority ==
           protocol::PacketPriority::Bulk);
    assert(network::defaultSendOptions(packet(protocol::PacketType::Filesystem)).priority ==
           protocol::PacketPriority::Bulk);
    assert(network::defaultSendOptions(packet(protocol::PacketType::Printer)).priority ==
           protocol::PacketPriority::Bulk);
}

void smallDataChannelSupportsClipboardPackets()
{
    const std::vector<network::ChannelSpec> specs = network::defaultMvpChannelSpecs();
    bool checked = false;
    for (const network::ChannelSpec& spec : specs) {
        if (spec.name != "small_data")
            continue;

        assert(containsPacket(spec.allowlist, protocol::PacketType::Clipboard));
        checked = true;
    }
    assert(checked);
}

void defaultVideoQueueUsesKeepLatestPolicy()
{
    network::SendOptions options = network::defaultSendOptions(packet(protocol::PacketType::Video));

    assert(options.priority == protocol::PacketPriority::Realtime);
    assert(options.dropPolicy == network::DropPolicy::KeepLatestNonKeyFrame);
    assert(options.maxQueueDepth == 2);
}

void largeDataChannelSupportsRedirectionPackets()
{
    const network::ChannelSpec spec = network::defaultLargeDataChannelSpec();

    assert(spec.name == "large_data");
    assert(spec.key.channelId == static_cast<protocol::ChannelId>(protocol::ChannelIdValue::LargeData));
    assert(spec.key.channelType == protocol::ChannelType::Standard);
    assert(spec.socketClass == network::SocketClass::Bulk);
    assert(spec.allowlist.size() == 5);
    assert(containsPacket(spec.allowlist, protocol::PacketType::Clipboard));
}

} // namespace

int main()
{
    defaultMvpChannelsRegisterAndBecomeReady();
    mapsKnownPacketsToExpectedPriority();
    smallDataChannelSupportsClipboardPackets();
    defaultVideoQueueUsesKeepLatestPolicy();
    largeDataChannelSupportsRedirectionPackets();
    return 0;
}
