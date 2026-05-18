#ifndef FUSIONDESK_NETWORK_CHANNEL_H
#define FUSIONDESK_NETWORK_CHANNEL_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace network {

struct ChannelKey
{
    protocol::ChannelId channelId = 0;
    protocol::ChannelType channelType = protocol::ChannelType::Standard;

    bool operator<(const ChannelKey& other) const
    {
        if (channelId != other.channelId)
            return channelId < other.channelId;
        return static_cast<std::uint16_t>(channelType) < static_cast<std::uint16_t>(other.channelType);
    }

    bool operator==(const ChannelKey& other) const
    {
        return channelId == other.channelId && channelType == other.channelType;
    }
};

enum class SocketClass
{
    Control,
    Realtime,
    Bulk,
    Auxiliary
};

enum class ReliabilityMode
{
    Reliable,
    BestEffort
};

enum class OrderingMode
{
    Ordered,
    LatestOnly,
    Unordered
};

enum class FlowControlMode
{
    None,
    Credit,
    Pressure
};

enum class QueuePolicy
{
    Bounded,
    DropNewest,
    DropOldest,
    KeepLatest
};

enum class ChannelLifecycleState
{
    Registered,
    Bound,
    Ready,
    Degraded,
    Closed,
    Failed
};

enum class PressureLevel
{
    Healthy,
    BuildingQueue,
    Congested,
    Draining,
    Closed,
    Failed
};

struct ChannelPressure
{
    PressureLevel level = PressureLevel::Healthy;
    std::size_t queuedPackets = 0;
    std::size_t queuedBytes = 0;
};

enum class SendStatus
{
    Sent,
    ChannelClosed,
    ChannelNotFound,
    BackPressure,
    Failed
};

struct SendResult
{
    SendStatus status = SendStatus::Failed;
    std::string message;

    static SendResult sent()
    {
        return {SendStatus::Sent, {}};
    }
};

class IChannel
{
public:
    virtual ~IChannel() = default;

    virtual protocol::ChannelId id() const = 0;
    virtual protocol::ChannelType type() const = 0;
    virtual bool isOpen() const = 0;
    virtual SendResult send(const protocol::PacketEnvelope& packet) = 0;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_CHANNEL_H
