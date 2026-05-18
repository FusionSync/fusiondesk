#include "fusiondesk/runtime/connection/peer_coordination.h"

#include <set>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(PeerCoordinationResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

std::set<network::ChannelKey> knownChannelKeys(const std::vector<network::ChannelSpec>& specs)
{
    std::set<network::ChannelKey> result;
    for (const network::ChannelSpec& spec : specs)
        result.insert(spec.key);
    return result;
}

std::set<network::ChannelKey> plannedChannelKeys(const PeerProfilePair& pair)
{
    std::set<network::ChannelKey> result;
    for (const PeerProfileConnectChannel& channel : pair.client.tcpChannels)
        result.insert(channel.spec.key);
    for (const PeerProfileListenChannel& channel : pair.agent.tcpListenChannels)
        result.insert(channel.spec.key);
    return result;
}

void appendReconnectChannels(PeerReconnectPlan& plan,
                             const PeerProfilePair& pair,
                             const std::set<network::ChannelKey>& degraded)
{
    for (const PeerProfileConnectChannel& channel : pair.client.tcpChannels) {
        if (degraded.find(channel.spec.key) != degraded.end())
            plan.clientReplacementTcpChannels.push_back(channel);
    }

    for (const PeerProfileListenChannel& channel : pair.agent.tcpListenChannels) {
        if (degraded.find(channel.spec.key) != degraded.end())
            plan.agentReplacementTcpListenChannels.push_back(channel);
    }
}

} // namespace

PeerCoordinationResult resolvePeerCoordination(const PeerCoordinationRequest& request)
{
    PeerCoordinationResult result;

    result.plan.profile = resolvePeerProfileExchange(request.profile);
    for (const std::string& message : result.plan.profile.messages)
        appendFailure(result, message);
    if (!result.plan.profile.ok)
        return result;

    result.plan.degradedChannels = request.degradedChannels;
    result.plan.requestDisplayKeyframe = request.requestDisplayKeyframe;
    result.plan.reconnect.degradedChannels = request.degradedChannels;
    result.plan.reconnect.requestDisplayKeyframe = request.requestDisplayKeyframe;

    const std::set<network::ChannelKey> available = knownChannelKeys(request.profile.connectionPlan.knownSpecs);
    const std::set<network::ChannelKey> planned =
        plannedChannelKeys(result.plan.profile.pair);
    std::set<network::ChannelKey> degraded;

    for (const network::ChannelKey& key : request.degradedChannels) {
        if (available.find(key) == available.end()) {
            appendFailure(result, "peer coordination request reconnect channel is not in known specs");
            break;
        }
        if (planned.find(key) == planned.end()) {
            appendFailure(result, "peer coordination reconnect channel has no replacement plan");
            break;
        }
        const auto inserted = degraded.insert(key);
        if (!inserted.second) {
            appendFailure(result, "peer coordination request has duplicate reconnect channels");
            break;
        }
    }

    if (!result.messages.empty())
        return result;

    if (!degraded.empty()) {
        result.plan.reconnect.requested = true;
        appendReconnectChannels(result.plan.reconnect,
                                result.plan.profile.pair,
                                degraded);
        result.plan.reconnect.teardownAfterSuccessfulRebind.assign(
            request.degradedChannels.begin(),
            request.degradedChannels.end());
    }

    result.ok = true;
    return result;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
