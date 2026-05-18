#include "fusiondesk/core/network/priority_scheduler.h"

#include <algorithm>

namespace fusiondesk {
namespace network {

EnqueueResult PriorityScheduler::enqueue(const protocol::PacketEnvelope& packet, const SendOptions& options)
{
    const protocol::PacketPriority priority = options.priority;
    const ChannelKey key = keyOf(packet);
    const std::size_t queuedForChannel = queuedCount(key);
    std::size_t dropped = 0;

    if (options.maxQueueDepth > 0 && queuedForChannel >= options.maxQueueDepth) {
        bool canQueue = false;
        switch (options.dropPolicy) {
        case DropPolicy::DropNewest:
            return {EnqueueStatus::Dropped, 1, computePressure(key, options.maxQueueDepth), "newest packet dropped"};
        case DropPolicy::DropOldest: {
            canQueue = dropOldest(key, priority);
            dropped = canQueue ? 1 : 0;
            break;
        }
        case DropPolicy::KeepLatestNonKeyFrame:
            canQueue = dropStaleNonKeyFrame(key, priority);
            dropped = canQueue ? 1 : 0;
            break;
        case DropPolicy::None:
            break;
        }

        if (!canQueue) {
            return {EnqueueStatus::Rejected, dropped, computePressure(key, options.maxQueueDepth),
                    "priority queue limit reached"};
        }
    }

    queues_[priorityIndex(priority)].push_back(packet);
    return {EnqueueStatus::Queued, dropped, computePressure(key, options.maxQueueDepth), {}};
}

std::optional<protocol::PacketEnvelope> PriorityScheduler::nextPacket()
{
    for (auto& queue : queues_) {
        if (!queue.empty()) {
            protocol::PacketEnvelope packet = queue.front();
            queue.pop_front();
            return packet;
        }
    }
    return std::nullopt;
}

std::optional<protocol::PacketEnvelope> PriorityScheduler::takePacket(const protocol::PacketEnvelope& packet)
{
    const std::size_t index = priorityIndex(packet.priority);
    auto& queue = queues_[index];
    auto it = std::find_if(queue.begin(), queue.end(), [&packet](const protocol::PacketEnvelope& queued) {
        return queued.channelId == packet.channelId &&
               queued.channelType == packet.channelType &&
               queued.packetType == packet.packetType &&
               queued.messageKind == packet.messageKind &&
               queued.messageId == packet.messageId &&
               queued.correlationId == packet.correlationId &&
               queued.responseTo == packet.responseTo &&
               queued.sequence == packet.sequence;
    });

    if (it == queue.end())
        return std::nullopt;

    protocol::PacketEnvelope found = *it;
    queue.erase(it);
    return found;
}

ChannelPressure PriorityScheduler::pressure(ChannelKey key) const
{
    return computePressure(key, 0);
}

std::size_t PriorityScheduler::queuedCount() const
{
    std::size_t count = 0;
    for (const auto& queue : queues_)
        count += queue.size();
    return count;
}

std::size_t PriorityScheduler::queuedCount(ChannelKey key) const
{
    std::size_t count = 0;
    for (const auto& queue : queues_) {
        count += static_cast<std::size_t>(std::count_if(queue.begin(), queue.end(), [key](const protocol::PacketEnvelope& packet) {
            return keyOf(packet) == key;
        }));
    }
    return count;
}

void PriorityScheduler::drain(ChannelKey key)
{
    for (auto& queue : queues_) {
        queue.erase(std::remove_if(queue.begin(), queue.end(), [key](const protocol::PacketEnvelope& packet) {
                        return keyOf(packet) == key;
                    }),
                    queue.end());
    }
}

std::size_t PriorityScheduler::priorityIndex(protocol::PacketPriority priority)
{
    const std::size_t index = static_cast<std::size_t>(priority);
    return index < kPriorityCount ? index : kPriorityCount - 1;
}

bool PriorityScheduler::hasFlag(protocol::PacketFlags flags, protocol::PacketFlags flag)
{
    return (flags & flag) == flag;
}

ChannelKey PriorityScheduler::keyOf(const protocol::PacketEnvelope& packet)
{
    return ChannelKey{packet.channelId, packet.channelType};
}

bool PriorityScheduler::dropOldest(ChannelKey key, protocol::PacketPriority priority)
{
    auto& queue = queues_[priorityIndex(priority)];
    auto it = std::find_if(queue.begin(), queue.end(), [key](const protocol::PacketEnvelope& packet) {
        return keyOf(packet) == key;
    });

    if (it == queue.end())
        return false;

    queue.erase(it);
    return true;
}

bool PriorityScheduler::dropStaleNonKeyFrame(ChannelKey key, protocol::PacketPriority priority)
{
    auto& queue = queues_[priorityIndex(priority)];
    auto it = std::find_if(queue.begin(), queue.end(), [key](const protocol::PacketEnvelope& packet) {
        return keyOf(packet) == key && !hasFlag(packet.flags, protocol::PacketFlagKeyFrame);
    });

    if (it == queue.end())
        return false;

    queue.erase(it);
    return true;
}

ChannelPressure PriorityScheduler::computePressure(ChannelKey key, std::size_t maxQueueDepth) const
{
    ChannelPressure pressure;
    pressure.queuedPackets = queuedCount(key);
    pressure.level = PressureLevel::Healthy;

    if (pressure.queuedPackets > 0)
        pressure.level = PressureLevel::BuildingQueue;

    if (maxQueueDepth > 0 && pressure.queuedPackets >= maxQueueDepth)
        pressure.level = PressureLevel::Congested;

    return pressure;
}

} // namespace network
} // namespace fusiondesk
