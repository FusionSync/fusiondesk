#ifndef FUSIONDESK_NETWORK_NETWORK_MANAGER_H
#define FUSIONDESK_NETWORK_NETWORK_MANAGER_H

#include <memory>
#include <string>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/network/socket_group.h"

namespace fusiondesk {
namespace network {

class NetworkManager
{
public:
    explicit NetworkManager(protocol::NegotiatedCapabilities capabilities);

    ChannelRegistry& registry();
    const ChannelRegistry& registry() const;
    PriorityScheduler& scheduler();
    const PriorityScheduler& scheduler() const;
    SocketGroup& socketGroup();
    const SocketGroup& socketGroup() const;
    NetworkRouter& router();
    const NetworkRouter& router() const;

    ChannelRegistryResult registerSpec(const ChannelSpec& spec);
    std::vector<ChannelRegistryResult> registerDefaultMvpChannels();
    SocketGroupResult registerTransportSocket(std::shared_ptr<ITransportSocket> socket);
    SocketGroupResult openTransportSocket(SocketClass socketClass, const SocketOpenOptions& options);
    void closeTransportSocket(SocketClass socketClass, const CloseReason& reason);
    ChannelRegistryResult bindChannel(std::shared_ptr<IChannel> channel);
    ChannelRegistryResult markReady(ChannelKey key, const ChannelReadyInfo& ready);
    ChannelRegistryResult markDegraded(ChannelKey key, const std::string& reason);
    ChannelRegistryResult rebindChannel(std::shared_ptr<IChannel> channel, const ChannelReadyInfo& ready);
    SocketSnapshot socketSnapshotForChannel(ChannelKey key) const;
    SocketGroupResult writeTransportBytes(const protocol::PacketEnvelope& packet,
                                          const protocol::ByteBuffer& bytes);
    EnqueueResult enqueue(const protocol::PacketEnvelope& packet);
    SendResult flushNext();
    SendResult flushPacket(const protocol::PacketEnvelope& packet);

private:
    ChannelRegistry registry_;
    PriorityScheduler scheduler_;
    SocketGroup socketGroup_;
    NetworkRouter router_;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_NETWORK_MANAGER_H
