#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/network/network_manager.h"

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

    const std::vector<protocol::PacketEnvelope>& sent() const
    {
        return sent_;
    }

private:
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
    bool open_ = true;
    std::vector<protocol::PacketEnvelope> sent_;
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
                                protocol::PacketType::Clipboard,
                                protocol::PacketType::Video,
                                protocol::PacketType::Printer};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

network::ChannelKey controlKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                               protocol::ChannelType::Control};
}

network::ChannelKey smallDataKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
                               protocol::ChannelType::Standard};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

protocol::PacketEnvelope makePacket(network::ChannelKey key, protocol::PacketType packetType)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = key.channelId;
    packet.channelType = key.channelType;
    packet.packetType = packetType;
    packet.messageKind = protocol::MessageKind::Event;
    return packet;
}

void registerBindAndFlushVideo()
{
    network::NetworkManager manager(makeNegotiated());
    std::vector<network::ChannelRegistryResult> results = manager.registerDefaultMvpChannels();
    assert(results.size() == 3);
    for (const network::ChannelRegistryResult& result : results)
        assert(result.ok);

    auto screen = std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType);
    assert(manager.bindChannel(screen).ok);
    assert(manager.markReady(screenKey(), {}).ok);

    assert(manager.enqueue(makePacket(screenKey(), protocol::PacketType::Video)).queued());
    assert(manager.flushNext().status == network::SendStatus::Sent);
    assert(screen->sent().size() == 1);
    assert(screen->sent().front().packetType == protocol::PacketType::Video);
}

void rejectsBeforeChannelReady()
{
    network::NetworkManager manager(makeNegotiated());
    manager.registerDefaultMvpChannels();
    manager.bindChannel(std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType));

    network::EnqueueResult result = manager.enqueue(makePacket(screenKey(), protocol::PacketType::Video));

    assert(result.status == network::EnqueueStatus::Rejected);
}

void flushesCriticalBeforeBulk()
{
    network::NetworkManager manager(makeNegotiated());
    manager.registerDefaultMvpChannels();

    auto control = std::make_shared<FakeChannel>(controlKey().channelId, controlKey().channelType);
    auto smallData = std::make_shared<FakeChannel>(smallDataKey().channelId, smallDataKey().channelType);
    assert(manager.bindChannel(control).ok);
    assert(manager.bindChannel(smallData).ok);
    assert(manager.markReady(controlKey(), {}).ok);
    assert(manager.markReady(smallDataKey(), {}).ok);

    assert(manager.enqueue(makePacket(smallDataKey(), protocol::PacketType::Printer)).queued());
    assert(manager.enqueue(makePacket(controlKey(), protocol::PacketType::Heartbeat)).queued());

    assert(manager.flushNext().status == network::SendStatus::Sent);
    assert(control->sent().size() == 1);
    assert(control->sent().front().packetType == protocol::PacketType::Heartbeat);

    assert(manager.flushNext().status == network::SendStatus::Sent);
    assert(smallData->sent().size() == 1);
    assert(smallData->sent().front().packetType == protocol::PacketType::Printer);
}

void flushPacketSendsRequestedPacket()
{
    network::NetworkManager manager(makeNegotiated());
    manager.registerDefaultMvpChannels();

    auto control = std::make_shared<FakeChannel>(controlKey().channelId, controlKey().channelType);
    auto screen = std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType);
    assert(manager.bindChannel(control).ok);
    assert(manager.bindChannel(screen).ok);
    assert(manager.markReady(controlKey(), {}).ok);
    assert(manager.markReady(screenKey(), {}).ok);

    protocol::PacketEnvelope video = makePacket(screenKey(), protocol::PacketType::Video);
    video.priority = protocol::PacketPriority::Realtime;
    video.sequence = 88;
    protocol::PacketEnvelope heartbeat = makePacket(controlKey(), protocol::PacketType::Heartbeat);
    heartbeat.priority = protocol::PacketPriority::Critical;

    assert(manager.enqueue(heartbeat).queued());
    assert(manager.enqueue(video).queued());
    assert(manager.flushPacket(video).status == network::SendStatus::Sent);
    assert(screen->sent().size() == 1);
    assert(screen->sent().front().sequence == 88);
    assert(control->sent().empty());
    assert(manager.scheduler().queuedCount() == 1);

    assert(manager.flushNext().status == network::SendStatus::Sent);
    assert(control->sent().size() == 1);
    assert(control->sent().front().packetType == protocol::PacketType::Heartbeat);
}

void flushPacketFindsPacketsWithDefaultPriorityOverride()
{
    network::NetworkManager manager(makeNegotiated());
    manager.registerDefaultMvpChannels();

    auto smallData = std::make_shared<FakeChannel>(smallDataKey().channelId,
                                                   smallDataKey().channelType);
    assert(manager.bindChannel(smallData).ok);
    assert(manager.markReady(smallDataKey(), {}).ok);

    protocol::PacketEnvelope clipboard =
        makePacket(smallDataKey(), protocol::PacketType::Clipboard);
    clipboard.priority = protocol::PacketPriority::Normal;
    clipboard.sequence = 99;

    assert(network::defaultSendOptions(clipboard).priority ==
           protocol::PacketPriority::Interactive);
    assert(manager.enqueue(clipboard).queued());
    assert(manager.flushPacket(clipboard).status == network::SendStatus::Sent);
    assert(smallData->sent().size() == 1);
    assert(smallData->sent().front().packetType == protocol::PacketType::Clipboard);
    assert(smallData->sent().front().sequence == 99);
}

void rebindsChannelWithoutDroppingSubscriptionsOrControl()
{
    network::NetworkManager manager(makeNegotiated());
    manager.registerDefaultMvpChannels();

    auto control = std::make_shared<FakeChannel>(controlKey().channelId, controlKey().channelType);
    auto oldScreen = std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType);
    auto newScreen = std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType);
    assert(manager.bindChannel(control).ok);
    assert(manager.bindChannel(oldScreen).ok);
    assert(manager.markReady(controlKey(), {}).ok);
    assert(manager.markReady(screenKey(), {}).ok);

    int incomingVideoCount = 0;
    network::RouteMatch route;
    route.channelId = screenKey().channelId;
    route.channelType = screenKey().channelType;
    route.packetType = protocol::PacketType::Video;
    manager.router().subscribe(route, [&incomingVideoCount](const protocol::PacketEnvelope&) {
        ++incomingVideoCount;
    });

    assert(manager.markDegraded(screenKey(), "screen socket failed").ok);
    assert(manager.registry().snapshot(screenKey()).state == network::ChannelLifecycleState::Degraded);
    assert(manager.registry().isReady(controlKey()));

    assert(manager.rebindChannel(newScreen, {}).ok);
    assert(manager.registry().isReady(screenKey()));

    assert(manager.enqueue(makePacket(screenKey(), protocol::PacketType::Video)).queued());
    assert(manager.flushNext().status == network::SendStatus::Sent);
    assert(oldScreen->sent().empty());
    assert(newScreen->sent().size() == 1);

    manager.router().submitIncoming(makePacket(screenKey(), protocol::PacketType::Video));
    assert(incomingVideoCount == 1);
}

void submitsIncomingUsingSubscriptionSnapshot()
{
    network::NetworkManager manager(makeNegotiated());
    manager.registerDefaultMvpChannels();

    auto screen = std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType);
    assert(manager.bindChannel(screen).ok);
    assert(manager.markReady(screenKey(), {}).ok);

    int firstCalls = 0;
    int secondCalls = 0;
    network::SubscriptionToken firstToken = 0;
    network::SubscriptionToken secondToken = 0;
    network::RouteMatch route;
    route.channelId = screenKey().channelId;
    route.channelType = screenKey().channelType;
    route.packetType = protocol::PacketType::Video;

    firstToken = manager.router().subscribe(route, [&manager, &firstToken, &secondToken, &firstCalls, &secondCalls, &route](const protocol::PacketEnvelope&) {
        ++firstCalls;
        manager.router().unsubscribe(firstToken);
        secondToken = manager.router().subscribe(route, [&secondCalls](const protocol::PacketEnvelope&) {
            ++secondCalls;
        });
    });

    manager.router().submitIncoming(makePacket(screenKey(), protocol::PacketType::Video));
    assert(firstCalls == 1);
    assert(secondCalls == 0);

    manager.router().submitIncoming(makePacket(screenKey(), protocol::PacketType::Video));
    assert(firstCalls == 1);
    assert(secondCalls == 1);
    assert(secondToken != 0);
}

} // namespace

int main()
{
    registerBindAndFlushVideo();
    rejectsBeforeChannelReady();
    flushesCriticalBeforeBulk();
    flushPacketSendsRequestedPacket();
    flushPacketFindsPacketsWithDefaultPriorityOverride();
    rebindsChannelWithoutDroppingSubscriptionsOrControl();
    submitsIncomingUsingSubscriptionSnapshot();
    return 0;
}
