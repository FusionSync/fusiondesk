#ifndef FUSIONDESK_NETWORK_PRIORITY_SCHEDULER_H
#define FUSIONDESK_NETWORK_PRIORITY_SCHEDULER_H

#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

#include "fusiondesk/core/network/channel.h"

namespace fusiondesk {
namespace network {

enum class DropPolicy
{
    None,
    DropNewest,
    DropOldest,
    KeepLatestNonKeyFrame
};

struct SendOptions
{
    protocol::PacketPriority priority = protocol::PacketPriority::Normal;
    DropPolicy dropPolicy = DropPolicy::None;
    std::size_t maxQueueDepth = 1024;
    bool allowCoalesce = false;
};

enum class EnqueueStatus
{
    Queued,
    Dropped,
    Rejected
};

struct EnqueueResult
{
    EnqueueStatus status = EnqueueStatus::Rejected;
    std::size_t droppedPackets = 0;
    ChannelPressure pressure;
    std::string message;

    bool queued() const
    {
        return status == EnqueueStatus::Queued;
    }
};

class PriorityScheduler
{
public:
    EnqueueResult enqueue(const protocol::PacketEnvelope& packet, const SendOptions& options = {});
    std::optional<protocol::PacketEnvelope> nextPacket();
    std::optional<protocol::PacketEnvelope> takePacket(const protocol::PacketEnvelope& packet);
    ChannelPressure pressure(ChannelKey key) const;
    std::size_t queuedCount() const;
    std::size_t queuedCount(ChannelKey key) const;
    void drain(ChannelKey key);

private:
    static std::size_t priorityIndex(protocol::PacketPriority priority);
    static bool hasFlag(protocol::PacketFlags flags, protocol::PacketFlags flag);
    static ChannelKey keyOf(const protocol::PacketEnvelope& packet);
    bool dropOldest(ChannelKey key, protocol::PacketPriority priority);
    bool dropStaleNonKeyFrame(ChannelKey key, protocol::PacketPriority priority);
    ChannelPressure computePressure(ChannelKey key, std::size_t maxQueueDepth) const;

private:
    static constexpr std::size_t kPriorityCount = 6;
    std::array<std::deque<protocol::PacketEnvelope>, kPriorityCount> queues_;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_PRIORITY_SCHEDULER_H
