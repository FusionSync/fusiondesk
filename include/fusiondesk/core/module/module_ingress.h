#ifndef FUSIONDESK_MODULE_MODULE_INGRESS_H
#define FUSIONDESK_MODULE_MODULE_INGRESS_H

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_manifest.h"
#include "fusiondesk/core/network/network_router.h"

namespace fusiondesk {
namespace module {

using ModulePacketHandler = std::function<void(const std::string& moduleId,
                                               const protocol::PacketEnvelope& packet)>;

struct RegisteredIngress
{
    std::string moduleId;
    std::vector<network::SubscriptionToken> tokens;
};

class ModuleIngressRegistry
{
public:
    explicit ModuleIngressRegistry(network::INetworkRouter* network);

    RegisteredIngress registerManifest(const ModuleManifest& manifest, ModulePacketHandler handler);
    void unregisterModule(const std::string& moduleId);

private:
    network::INetworkRouter* network_ = nullptr;
    std::map<std::string, std::vector<network::SubscriptionToken>> tokensByModule_;
};

} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_INGRESS_H
