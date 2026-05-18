#include "fusiondesk/core/network/network_router.h"

#include <utility>
#include <vector>

namespace fusiondesk {
namespace network {

bool NetworkRouter::registerChannel(std::shared_ptr<IChannel> channel)
{
    if (!channel)
        return false;

    channels_[ChannelKey{channel->id(), channel->type()}] = std::move(channel);
    return true;
}

void NetworkRouter::unregisterChannel(protocol::ChannelId channelId, protocol::ChannelType channelType)
{
    channels_.erase(ChannelKey{channelId, channelType});
}

SendResult NetworkRouter::send(const protocol::PacketEnvelope& packet)
{
    auto it = channels_.find(ChannelKey{packet.channelId, packet.channelType});
    if (it == channels_.end())
        return {SendStatus::ChannelNotFound, "channel not found"};

    if (!it->second->isOpen())
        return {SendStatus::ChannelClosed, "channel is closed"};

    return it->second->send(packet);
}

SubscriptionToken NetworkRouter::subscribe(const RouteMatch& route, PacketHandler handler)
{
    if (!handler)
        return 0;

    const SubscriptionToken token = nextToken_++;
    subscriptions_[token] = Subscription{route, std::move(handler)};
    return token;
}

void NetworkRouter::unsubscribe(SubscriptionToken token)
{
    subscriptions_.erase(token);
}

void NetworkRouter::submitIncoming(const protocol::PacketEnvelope& packet)
{
    std::vector<PacketHandler> handlers;
    for (const auto& item : subscriptions_) {
        const Subscription& subscription = item.second;
        if (routeMatches(subscription.route, packet))
            handlers.push_back(subscription.handler);
    }

    for (const PacketHandler& handler : handlers) {
        if (handler)
            handler(packet);
    }
}

bool NetworkRouter::routeMatches(const RouteMatch& route, const protocol::PacketEnvelope& packet)
{
    if (route.channelId.has_value() && route.channelId.value() != packet.channelId)
        return false;

    if (route.channelType.has_value() && route.channelType.value() != packet.channelType)
        return false;

    if (route.messageKind.has_value() && route.messageKind.value() != packet.messageKind)
        return false;

    return route.packetType == packet.packetType;
}

} // namespace network
} // namespace fusiondesk
