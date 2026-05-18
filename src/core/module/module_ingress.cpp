#include "fusiondesk/core/module/module_ingress.h"

namespace fusiondesk {
namespace module {

ModuleIngressRegistry::ModuleIngressRegistry(network::INetworkRouter* network)
    : network_(network)
{
}

RegisteredIngress ModuleIngressRegistry::registerManifest(const ModuleManifest& manifest,
                                                          ModulePacketHandler handler)
{
    RegisteredIngress result;
    result.moduleId = manifest.moduleId;

    if (network_ == nullptr || manifest.moduleId.empty() || !handler)
        return result;

    unregisterModule(manifest.moduleId);

    for (const ChannelBinding& channel : manifest.channels) {
        for (protocol::PacketType packetType : channel.consumes) {
            network::RouteMatch route;
            route.channelId = channel.channelId;
            route.channelType = channel.channelType;
            route.packetType = packetType;

            const std::string moduleId = manifest.moduleId;
            network::SubscriptionToken token = network_->subscribe(
                route,
                [moduleId, handler](const protocol::PacketEnvelope& packet) {
                    handler(moduleId, packet);
                });

            if (token != 0)
                result.tokens.push_back(token);
        }
    }

    if (!result.tokens.empty())
        tokensByModule_[result.moduleId] = result.tokens;

    return result;
}

void ModuleIngressRegistry::unregisterModule(const std::string& moduleId)
{
    if (network_ == nullptr)
        return;

    auto it = tokensByModule_.find(moduleId);
    if (it == tokensByModule_.end())
        return;

    for (network::SubscriptionToken token : it->second)
        network_->unsubscribe(token);

    tokensByModule_.erase(it);
}

} // namespace module
} // namespace fusiondesk
