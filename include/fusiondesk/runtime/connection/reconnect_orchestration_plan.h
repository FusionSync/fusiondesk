#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_ORCHESTRATION_PLAN_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_ORCHESTRATION_PLAN_H

#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/runtime/connection/peer_profile_exchange.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ReconnectOrchestrationRequest
{
    PeerProfileExchangeRequest profile;
    std::vector<network::ChannelKey> degradedChannels;
    std::string reason = "service reconnect";
    bool requestDisplayKeyframe = true;
};

struct ReconnectOrchestrationSidePlan
{
    protocol::SessionId sessionId = 0;
    std::vector<network::ChannelKey> degradedChannels;
    std::vector<PeerProfileConnectChannel> tcpChannels;
    std::vector<PeerProfileListenChannel> tcpListenChannels;
    std::vector<network::ChannelKey> teardownAfterSuccessfulRebind;
    std::string reason;
    bool requestDisplayKeyframe = true;
};

struct ReconnectOrchestrationPlan
{
    PeerProfileExchangeResult profile;
    ReconnectOrchestrationSidePlan client;
    ReconnectOrchestrationSidePlan agent;
};

struct ReconnectOrchestrationResult
{
    bool ok = false;
    ReconnectOrchestrationPlan plan;
    std::vector<std::string> messages;
};

ReconnectOrchestrationResult resolveReconnectOrchestrationPlan(
    const ReconnectOrchestrationRequest& request);

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_ORCHESTRATION_PLAN_H
