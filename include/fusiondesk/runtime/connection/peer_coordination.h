#ifndef FUSIONDESK_RUNTIME_CONNECTION_PEER_COORDINATION_H
#define FUSIONDESK_RUNTIME_CONNECTION_PEER_COORDINATION_H

#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/runtime/connection/peer_profile_exchange.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct PeerCoordinationRequest
{
    PeerProfileExchangeRequest profile;
    std::vector<network::ChannelKey> degradedChannels;
    bool requestDisplayKeyframe = true;
};

struct PeerReconnectPlan
{
    bool requested = false;
    std::vector<network::ChannelKey> degradedChannels;
    std::vector<PeerProfileConnectChannel> clientReplacementTcpChannels;
    std::vector<PeerProfileListenChannel> agentReplacementTcpListenChannels;
    std::vector<network::ChannelKey> teardownAfterSuccessfulRebind;
    bool requestDisplayKeyframe = true;
};

struct PeerCoordinationPlan
{
    PeerProfileExchangeResult profile;
    std::vector<network::ChannelKey> degradedChannels;
    PeerReconnectPlan reconnect;
    bool requestDisplayKeyframe = true;
};

struct PeerCoordinationResult
{
    bool ok = false;
    PeerCoordinationPlan plan;
    std::vector<std::string> messages;
};

PeerCoordinationResult resolvePeerCoordination(const PeerCoordinationRequest& request);

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_PEER_COORDINATION_H
