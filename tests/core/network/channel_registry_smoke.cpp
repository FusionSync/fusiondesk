#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"

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
        return open_;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sent_.push_back(packet);
        return network::SendResult::sent();
    }

    void close()
    {
        open_ = false;
    }

private:
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
    bool open_ = true;
    std::vector<protocol::PacketEnvelope> sent_;
};

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control, protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::Exchange,
                                protocol::PacketType::Video,
                                protocol::PacketType::PayloadAck};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

network::ChannelSpec makeScreenSpec()
{
    network::ChannelSpec spec;
    spec.key = screenKey();
    spec.name = "main_screen";
    spec.socketClass = network::SocketClass::Realtime;
    spec.defaultPriority = protocol::PacketPriority::Realtime;
    spec.reliability = network::ReliabilityMode::BestEffort;
    spec.ordering = network::OrderingMode::LatestOnly;
    spec.queuePolicy = network::QueuePolicy::KeepLatest;
    spec.allowlist = {protocol::PacketType::Video, protocol::PacketType::PayloadAck};
    spec.ownerModuleId = "display.screen";
    spec.required = true;
    return spec;
}

protocol::PacketEnvelope makeVideoPacket()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = screenKey().channelId;
    packet.channelType = screenKey().channelType;
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    return packet;
}

void registersBindsAndMarksReady()
{
    network::ChannelRegistry registry(makeNegotiated());
    assert(registry.registerSpec(makeScreenSpec()).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);

    network::ChannelReadyInfo ready;
    ready.monotonicTimestampUsec = 1000;
    ready.endpoint = "fake";
    assert(registry.markReady(screenKey(), ready).ok);
    assert(registry.isReady(screenKey()));

    network::ChannelSnapshot snapshot = registry.snapshot(screenKey());
    assert(snapshot.ready);
    assert(snapshot.bound);
    assert(snapshot.state == network::ChannelLifecycleState::Ready);
    assert(snapshot.spec.required);
}

void rejectsPacketBeforeReady()
{
    network::ChannelRegistry registry(makeNegotiated());
    assert(registry.registerSpec(makeScreenSpec()).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);

    network::ChannelRegistryResult result = registry.validatePacket(makeVideoPacket());

    assert(!result.ok);
    assert(result.status == network::ChannelRegistryStatus::NotReady);
    assert(result.responseStatus == protocol::ResponseStatus::ChannelUnavailable);
}

void rejectsUnnegotiatedPacket()
{
    network::ChannelRegistry registry(makeNegotiated());
    assert(registry.registerSpec(makeScreenSpec()).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    protocol::PacketEnvelope packet = makeVideoPacket();
    packet.packetType = protocol::PacketType::Clipboard;

    network::ChannelRegistryResult result = registry.validatePacket(packet);

    assert(!result.ok);
    assert(result.status == network::ChannelRegistryStatus::NotNegotiated);
    assert(result.responseStatus == protocol::ResponseStatus::Unsupported);
}

void rejectsPacketOutsideChannelSpecAllowlist()
{
    network::ChannelRegistry registry(makeNegotiated());
    assert(registry.registerSpec(makeScreenSpec()).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    protocol::PacketEnvelope packet = makeVideoPacket();
    packet.packetType = protocol::PacketType::Exchange;

    network::ChannelRegistryResult result = registry.validatePacket(packet);

    assert(!result.ok);
    assert(result.status == network::ChannelRegistryStatus::PacketTypeNotAllowed);
    assert(result.responseStatus == protocol::ResponseStatus::Unsupported);
}

void tracksDegradedAndPressureState()
{
    network::ChannelRegistry registry(makeNegotiated());
    assert(registry.registerSpec(makeScreenSpec()).ok);
    assert(registry.markDegraded(screenKey(), "socket slow").ok);
    assert(registry.snapshot(screenKey()).state == network::ChannelLifecycleState::Degraded);

    network::ChannelPressure pressure;
    pressure.level = network::PressureLevel::Congested;
    pressure.queuedPackets = 12;
    assert(registry.updatePressure(screenKey(), pressure).ok);
    assert(registry.snapshot(screenKey()).pressure.level == network::PressureLevel::Congested);
    assert(registry.snapshot(screenKey()).pressure.queuedPackets == 12);
}

} // namespace

int main()
{
    registersBindsAndMarksReady();
    rejectsPacketBeforeReady();
    rejectsUnnegotiatedPacket();
    rejectsPacketOutsideChannelSpecAllowlist();
    tracksDegradedAndPressureState();
    return 0;
}
