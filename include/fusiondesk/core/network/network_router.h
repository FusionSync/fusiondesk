#ifndef FUSIONDESK_NETWORK_NETWORK_ROUTER_H
#define FUSIONDESK_NETWORK_NETWORK_ROUTER_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "fusiondesk/core/network/channel.h"

namespace fusiondesk {
namespace network {

using SubscriptionToken = std::uint64_t;
using PacketHandler = std::function<void(const protocol::PacketEnvelope&)>;

struct RouteMatch
{
    std::optional<protocol::ChannelId> channelId;
    std::optional<protocol::ChannelType> channelType;
    std::optional<protocol::MessageKind> messageKind;
    protocol::PacketType packetType = protocol::PacketType::Heartbeat;
};

class INetworkRouter
{
public:
    virtual ~INetworkRouter() = default;

    virtual bool registerChannel(std::shared_ptr<IChannel> channel) = 0;
    virtual void unregisterChannel(protocol::ChannelId channelId, protocol::ChannelType channelType) = 0;
    virtual SendResult send(const protocol::PacketEnvelope& packet) = 0;
    virtual SubscriptionToken subscribe(const RouteMatch& route, PacketHandler handler) = 0;
    virtual void unsubscribe(SubscriptionToken token) = 0;
    virtual void submitIncoming(const protocol::PacketEnvelope& packet) = 0;
};

class NetworkRouter : public INetworkRouter
{
public:
    bool registerChannel(std::shared_ptr<IChannel> channel) override;
    void unregisterChannel(protocol::ChannelId channelId, protocol::ChannelType channelType) override;
    SendResult send(const protocol::PacketEnvelope& packet) override;
    SubscriptionToken subscribe(const RouteMatch& route, PacketHandler handler) override;
    void unsubscribe(SubscriptionToken token) override;
    void submitIncoming(const protocol::PacketEnvelope& packet) override;

private:
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
    };

    struct Subscription
    {
        RouteMatch route;
        PacketHandler handler;
    };

    static bool routeMatches(const RouteMatch& route, const protocol::PacketEnvelope& packet);

private:
    std::map<ChannelKey, std::shared_ptr<IChannel>> channels_;
    std::map<SubscriptionToken, Subscription> subscriptions_;
    SubscriptionToken nextToken_ = 1;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_NETWORK_ROUTER_H
