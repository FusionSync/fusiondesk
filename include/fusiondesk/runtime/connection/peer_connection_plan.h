#ifndef FUSIONDESK_RUNTIME_CONNECTION_PEER_CONNECTION_PLAN_H
#define FUSIONDESK_RUNTIME_CONNECTION_PEER_CONNECTION_PLAN_H

#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct PeerConnectionChannelRequest
{
    network::ChannelKey key;
    std::string endpoint;
    std::string clientReadyEndpoint;
    std::string agentReadyEndpoint;
};

struct PeerConnectionPlanRequest
{
    std::vector<network::ChannelSpec> knownSpecs;
    std::vector<PeerConnectionChannelRequest> channels;
};

struct PeerConnectionPlanChannel
{
    network::ChannelSpec spec;
    std::string endpoint;
    std::string clientReadyEndpoint;
    std::string agentReadyEndpoint;
};

struct PeerConnectionPlanResult
{
    bool ok = false;
    std::vector<PeerConnectionPlanChannel> channels;
    std::vector<std::string> messages;
};

PeerConnectionPlanResult resolvePeerConnectionPlan(
    const PeerConnectionPlanRequest& request);

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_PEER_CONNECTION_PLAN_H
