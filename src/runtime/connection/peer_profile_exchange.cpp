#include "fusiondesk/runtime/connection/peer_profile_exchange.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(PeerProfileExchangeResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

} // namespace

PeerProfileExchangeResult resolvePeerProfileExchange(const PeerProfileExchangeRequest& request)
{
    PeerProfileExchangeResult result;

    const PeerConnectionPlanResult plan = resolvePeerConnectionPlan(request.connectionPlan);
    for (const std::string& message : plan.messages)
        appendFailure(result, message);
    if (!plan.ok)
        return result;

    result.pair.client.sessionId = request.clientSessionId;
    result.pair.agent.sessionId = request.agentSessionId;

    for (const PeerConnectionPlanChannel& channel : plan.channels) {
        PeerProfileConnectChannel clientChannel;
        clientChannel.spec = channel.spec;
        clientChannel.endpoint = channel.endpoint;
        clientChannel.readyEndpoint = channel.clientReadyEndpoint;
        result.pair.client.tcpChannels.push_back(std::move(clientChannel));

        PeerProfileListenChannel agentChannel;
        agentChannel.spec = channel.spec;
        agentChannel.endpoint = channel.endpoint;
        agentChannel.readyEndpoint = channel.agentReadyEndpoint;
        result.pair.agent.tcpListenChannels.push_back(std::move(agentChannel));
    }

    result.ok = true;
    return result;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
