#include <cassert>
#include <optional>

#include "fusiondesk/core/network/priority_scheduler.h"

using namespace fusiondesk;

namespace {

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

network::ChannelKey secondScreenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopSecondScreen),
                               protocol::ChannelType::Video};
}

protocol::PacketEnvelope makePacket(protocol::PacketPriority priority, std::uint64_t sequence)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 7;
    packet.channelId = screenKey().channelId;
    packet.channelType = screenKey().channelType;
    packet.packetType = priority == protocol::PacketPriority::Bulk ? protocol::PacketType::Filesystem
                                                                   : protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    packet.priority = priority;
    packet.sequence = sequence;
    return packet;
}

protocol::PacketEnvelope makePacket(network::ChannelKey key,
                                    protocol::PacketPriority priority,
                                    std::uint64_t sequence)
{
    protocol::PacketEnvelope packet = makePacket(priority, sequence);
    packet.channelId = key.channelId;
    packet.channelType = key.channelType;
    return packet;
}

network::SendOptions options(protocol::PacketPriority priority)
{
    network::SendOptions sendOptions;
    sendOptions.priority = priority;
    return sendOptions;
}

void criticalBypassesBulk()
{
    network::PriorityScheduler scheduler;
    assert(scheduler.enqueue(makePacket(protocol::PacketPriority::Bulk, 1), options(protocol::PacketPriority::Bulk)).queued());
    assert(scheduler.enqueue(makePacket(protocol::PacketPriority::Critical, 2), options(protocol::PacketPriority::Critical)).queued());

    std::optional<protocol::PacketEnvelope> first = scheduler.nextPacket();
    assert(first.has_value());
    assert(first->priority == protocol::PacketPriority::Critical);

    std::optional<protocol::PacketEnvelope> second = scheduler.nextPacket();
    assert(second.has_value());
    assert(second->priority == protocol::PacketPriority::Bulk);
}

void keepsLatestRealtimeDeltaFrame()
{
    network::PriorityScheduler scheduler;
    network::SendOptions sendOptions = options(protocol::PacketPriority::Realtime);
    sendOptions.maxQueueDepth = 1;
    sendOptions.dropPolicy = network::DropPolicy::KeepLatestNonKeyFrame;

    protocol::PacketEnvelope first = makePacket(protocol::PacketPriority::Realtime, 10);
    protocol::PacketEnvelope second = makePacket(protocol::PacketPriority::Realtime, 11);

    assert(scheduler.enqueue(first, sendOptions).queued());
    network::EnqueueResult result = scheduler.enqueue(second, sendOptions);
    assert(result.queued());
    assert(result.droppedPackets == 1);
    assert(scheduler.queuedCount() == 1);

    std::optional<protocol::PacketEnvelope> next = scheduler.nextPacket();
    assert(next.has_value());
    assert(next->sequence == 11);
}

void preservesKeyFrameWhenCongested()
{
    network::PriorityScheduler scheduler;
    network::SendOptions sendOptions = options(protocol::PacketPriority::Realtime);
    sendOptions.maxQueueDepth = 1;
    sendOptions.dropPolicy = network::DropPolicy::KeepLatestNonKeyFrame;

    protocol::PacketEnvelope keyframe = makePacket(protocol::PacketPriority::Realtime, 20);
    keyframe.flags = protocol::PacketFlagKeyFrame;
    protocol::PacketEnvelope delta = makePacket(protocol::PacketPriority::Realtime, 21);

    assert(scheduler.enqueue(keyframe, sendOptions).queued());
    network::EnqueueResult result = scheduler.enqueue(delta, sendOptions);
    assert(result.status == network::EnqueueStatus::Rejected);
    assert(scheduler.queuedCount() == 1);

    std::optional<protocol::PacketEnvelope> next = scheduler.nextPacket();
    assert(next.has_value());
    assert(next->sequence == 20);
    assert((next->flags & protocol::PacketFlagKeyFrame) == protocol::PacketFlagKeyFrame);
}

void reportsAndDrainsPressure()
{
    network::PriorityScheduler scheduler;
    network::SendOptions sendOptions = options(protocol::PacketPriority::Realtime);
    sendOptions.maxQueueDepth = 2;

    assert(scheduler.enqueue(makePacket(protocol::PacketPriority::Realtime, 30), sendOptions).queued());
    assert(scheduler.enqueue(makePacket(protocol::PacketPriority::Realtime, 31), sendOptions).queued());

    network::ChannelPressure pressure = scheduler.pressure(screenKey());
    assert(pressure.queuedPackets == 2);
    assert(pressure.level == network::PressureLevel::BuildingQueue);

    scheduler.drain(screenKey());
    assert(scheduler.queuedCount() == 0);
    assert(scheduler.pressure(screenKey()).level == network::PressureLevel::Healthy);
}

void queueLimitsArePerChannel()
{
    network::PriorityScheduler scheduler;
    network::SendOptions sendOptions = options(protocol::PacketPriority::Realtime);
    sendOptions.maxQueueDepth = 1;
    sendOptions.dropPolicy = network::DropPolicy::KeepLatestNonKeyFrame;

    assert(scheduler.enqueue(makePacket(screenKey(), protocol::PacketPriority::Realtime, 40), sendOptions).queued());
    assert(scheduler.enqueue(makePacket(secondScreenKey(), protocol::PacketPriority::Realtime, 41), sendOptions).queued());
    assert(scheduler.queuedCount(screenKey()) == 1);
    assert(scheduler.queuedCount(secondScreenKey()) == 1);
    assert(scheduler.queuedCount() == 2);
}

} // namespace

int main()
{
    criticalBypassesBulk();
    keepsLatestRealtimeDeltaFrame();
    preservesKeyFrameWhenCongested();
    reportsAndDrainsPressure();
    queueLimitsArePerChannel();
    return 0;
}
