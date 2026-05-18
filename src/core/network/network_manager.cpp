#include "fusiondesk/core/network/network_manager.h"

#include <utility>

namespace fusiondesk {
namespace network {

NetworkManager::NetworkManager(protocol::NegotiatedCapabilities capabilities)
    : registry_(std::move(capabilities))
{
}

ChannelRegistry& NetworkManager::registry()
{
    return registry_;
}

const ChannelRegistry& NetworkManager::registry() const
{
    return registry_;
}

PriorityScheduler& NetworkManager::scheduler()
{
    return scheduler_;
}

const PriorityScheduler& NetworkManager::scheduler() const
{
    return scheduler_;
}

SocketGroup& NetworkManager::socketGroup()
{
    return socketGroup_;
}

const SocketGroup& NetworkManager::socketGroup() const
{
    return socketGroup_;
}

NetworkRouter& NetworkManager::router()
{
    return router_;
}

const NetworkRouter& NetworkManager::router() const
{
    return router_;
}

ChannelRegistryResult NetworkManager::registerSpec(const ChannelSpec& spec)
{
    return registry_.registerSpec(spec);
}

std::vector<ChannelRegistryResult> NetworkManager::registerDefaultMvpChannels()
{
    std::vector<ChannelRegistryResult> results;
    std::vector<ChannelSpec> specs = defaultMvpChannelSpecs();
    results.reserve(specs.size());
    for (const ChannelSpec& spec : specs)
        results.push_back(registry_.registerSpec(spec));
    return results;
}

SocketGroupResult NetworkManager::registerTransportSocket(std::shared_ptr<ITransportSocket> socket)
{
    return socketGroup_.registerSocket(std::move(socket));
}

SocketGroupResult NetworkManager::openTransportSocket(SocketClass socketClass, const SocketOpenOptions& options)
{
    return socketGroup_.open(socketClass, options);
}

void NetworkManager::closeTransportSocket(SocketClass socketClass, const CloseReason& reason)
{
    socketGroup_.close(socketClass, reason);
}

ChannelRegistryResult NetworkManager::bindChannel(std::shared_ptr<IChannel> channel)
{
    if (!channel) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::InvalidArgument,
                                             protocol::ResponseStatus::InvalidArgument,
                                             "channel instance is required");
    }

    const ChannelKey key{channel->id(), channel->type()};
    ChannelRegistryResult result = registry_.bind(key, channel);
    if (!result.ok)
        return result;

    router_.registerChannel(std::move(channel));
    return ChannelRegistryResult::success();
}

ChannelRegistryResult NetworkManager::markReady(ChannelKey key, const ChannelReadyInfo& ready)
{
    return registry_.markReady(key, ready);
}

ChannelRegistryResult NetworkManager::markDegraded(ChannelKey key, const std::string& reason)
{
    return registry_.markDegraded(key, reason);
}

ChannelRegistryResult NetworkManager::rebindChannel(std::shared_ptr<IChannel> channel, const ChannelReadyInfo& ready)
{
    if (!channel) {
        return ChannelRegistryResult::failed(ChannelRegistryStatus::InvalidArgument,
                                             protocol::ResponseStatus::InvalidArgument,
                                             "channel instance is required");
    }

    const ChannelKey key{channel->id(), channel->type()};
    ChannelRegistryResult result = registry_.bind(key, channel);
    if (!result.ok)
        return result;

    router_.registerChannel(std::move(channel));
    return registry_.markReady(key, ready);
}

SocketSnapshot NetworkManager::socketSnapshotForChannel(ChannelKey key) const
{
    ChannelSnapshot channel = registry_.snapshot(key);
    if (channel.spec.key.channelId == 0)
        return {};
    return socketGroup_.snapshot(channel.spec.socketClass);
}

SocketGroupResult NetworkManager::writeTransportBytes(const protocol::PacketEnvelope& packet,
                                                      const protocol::ByteBuffer& bytes)
{
    ChannelRegistryResult validation = registry_.validatePacket(packet);
    if (!validation.ok)
        return SocketGroupResult::failed(SocketGroupStatus::Failed, validation.message);

    ChannelSnapshot channel = registry_.snapshot(ChannelKey{packet.channelId, packet.channelType});
    return socketGroup_.write(channel.spec.socketClass, bytes);
}

EnqueueResult NetworkManager::enqueue(const protocol::PacketEnvelope& packet)
{
    ChannelRegistryResult validation = registry_.validatePacket(packet);
    if (!validation.ok) {
        EnqueueResult result;
        result.status = EnqueueStatus::Rejected;
        result.message = validation.message;
        return result;
    }

    EnqueueResult result = scheduler_.enqueue(packet, defaultSendOptions(packet));
    registry_.updatePressure(ChannelKey{packet.channelId, packet.channelType}, result.pressure);
    return result;
}

SendResult NetworkManager::flushNext()
{
    std::optional<protocol::PacketEnvelope> packet = scheduler_.nextPacket();
    if (!packet.has_value())
        return {SendStatus::Failed, "scheduler queue is empty"};

    const ChannelKey key{packet->channelId, packet->channelType};
    SendResult result = router_.send(packet.value());
    registry_.updatePressure(key, scheduler_.pressure(key));
    return result;
}

SendResult NetworkManager::flushPacket(const protocol::PacketEnvelope& packet)
{
    std::optional<protocol::PacketEnvelope> queued = scheduler_.takePacket(packet);
    if (!queued.has_value()) {
        protocol::PacketEnvelope effective = packet;
        effective.priority = defaultSendOptions(packet).priority;
        queued = scheduler_.takePacket(effective);
    }
    if (!queued.has_value())
        return {SendStatus::Failed, "packet is not queued"};

    const ChannelKey key{queued->channelId, queued->channelType};
    SendResult result = router_.send(queued.value());
    registry_.updatePressure(key, scheduler_.pressure(key));
    return result;
}

} // namespace network
} // namespace fusiondesk
