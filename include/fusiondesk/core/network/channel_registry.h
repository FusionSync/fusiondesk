#ifndef FUSIONDESK_NETWORK_CHANNEL_REGISTRY_H
#define FUSIONDESK_NETWORK_CHANNEL_REGISTRY_H

#include <map>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_allowlist.h"

namespace fusiondesk {
namespace network {

struct ChannelSpec
{
    ChannelKey key;
    std::string name;
    SocketClass socketClass = SocketClass::Control;
    protocol::PacketPriority defaultPriority = protocol::PacketPriority::Normal;
    ReliabilityMode reliability = ReliabilityMode::Reliable;
    OrderingMode ordering = OrderingMode::Ordered;
    FlowControlMode flowControl = FlowControlMode::Pressure;
    QueuePolicy queuePolicy = QueuePolicy::Bounded;
    std::vector<protocol::PacketType> allowlist;
    std::string ownerModuleId;
    bool required = false;
};

struct ChannelReadyInfo
{
    std::uint64_t monotonicTimestampUsec = 0;
    std::string endpoint;
};

struct ChannelSnapshot
{
    ChannelSpec spec;
    ChannelLifecycleState state = ChannelLifecycleState::Registered;
    bool bound = false;
    bool ready = false;
    ChannelReadyInfo readyInfo;
    ChannelPressure pressure;
    std::string message;
};

enum class ChannelRegistryStatus
{
    Ok,
    AlreadyRegistered,
    NotFound,
    NotReady,
    NotNegotiated,
    PacketTypeNotAllowed,
    InvalidArgument
};

struct ChannelRegistryResult
{
    bool ok = false;
    ChannelRegistryStatus status = ChannelRegistryStatus::InvalidArgument;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Failed;
    std::string message;

    static ChannelRegistryResult success()
    {
        return {true, ChannelRegistryStatus::Ok, protocol::ResponseStatus::Ok, {}};
    }

    static ChannelRegistryResult failed(ChannelRegistryStatus status,
                                        protocol::ResponseStatus responseStatus,
                                        std::string message)
    {
        return {false, status, responseStatus, message};
    }
};

class ChannelRegistry
{
public:
    explicit ChannelRegistry(protocol::NegotiatedCapabilities capabilities);

    ChannelRegistryResult registerSpec(const ChannelSpec& spec);
    ChannelRegistryResult bind(ChannelKey key, std::shared_ptr<IChannel> channel);
    void unbind(ChannelKey key, const std::string& reason);
    ChannelRegistryResult markReady(ChannelKey key, const ChannelReadyInfo& ready);
    ChannelRegistryResult markDegraded(ChannelKey key, const std::string& reason);
    ChannelRegistryResult markFailed(ChannelKey key, const std::string& reason);
    ChannelRegistryResult updatePressure(ChannelKey key, ChannelPressure pressure);
    bool isReady(ChannelKey key) const;
    ChannelSnapshot snapshot(ChannelKey key) const;
    std::vector<ChannelSnapshot> snapshots() const;
    ChannelRegistryResult validatePacket(const protocol::PacketEnvelope& packet) const;

private:
    struct Entry
    {
        ChannelSnapshot snapshot;
        std::shared_ptr<IChannel> channel;
    };

    Entry* find(ChannelKey key);
    const Entry* find(ChannelKey key) const;
    bool packetTypeAllowed(const ChannelSpec& spec, protocol::PacketType packetType) const;

private:
    ChannelAllowlistValidator allowlist_;
    std::map<ChannelKey, Entry> entries_;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_CHANNEL_REGISTRY_H
