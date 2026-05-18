#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"

#include <set>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(ReconnectOrchestrationResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

std::set<network::ChannelKey> keysFromSpecs(const std::vector<network::ChannelSpec>& specs)
{
    std::set<network::ChannelKey> result;
    for (const network::ChannelSpec& spec : specs)
        result.insert(spec.key);
    return result;
}

std::set<network::ChannelKey> connectChannelKeys(
    const std::vector<PeerProfileConnectChannel>& channels)
{
    std::set<network::ChannelKey> result;
    for (const PeerProfileConnectChannel& channel : channels)
        result.insert(channel.spec.key);
    return result;
}

std::set<network::ChannelKey> listenChannelKeys(
    const std::vector<PeerProfileListenChannel>& channels)
{
    std::set<network::ChannelKey> result;
    for (const PeerProfileListenChannel& channel : channels)
        result.insert(channel.spec.key);
    return result;
}

void addClientReplacementChannels(ReconnectOrchestrationSidePlan& plan,
                                  const std::vector<PeerProfileConnectChannel>& channels,
                                  const std::set<network::ChannelKey>& degraded)
{
    for (const PeerProfileConnectChannel& channel : channels) {
        if (degraded.find(channel.spec.key) != degraded.end())
            plan.tcpChannels.push_back(channel);
    }
}

void addAgentReplacementListeners(ReconnectOrchestrationSidePlan& plan,
                                  const std::vector<PeerProfileListenChannel>& channels,
                                  const std::set<network::ChannelKey>& degraded)
{
    for (const PeerProfileListenChannel& channel : channels) {
        if (degraded.find(channel.spec.key) != degraded.end())
            plan.tcpListenChannels.push_back(channel);
    }
}

} // namespace

ReconnectOrchestrationResult resolveReconnectOrchestrationPlan(
    const ReconnectOrchestrationRequest& request)
{
    ReconnectOrchestrationResult result;

    if (request.degradedChannels.empty()) {
        appendFailure(result, "reconnect orchestration requires degraded channels");
        return result;
    }

    result.plan.profile = resolvePeerProfileExchange(request.profile);
    for (const std::string& message : result.plan.profile.messages)
        appendFailure(result, message);
    if (!result.plan.profile.ok)
        return result;

    const std::set<network::ChannelKey> known =
        keysFromSpecs(request.profile.connectionPlan.knownSpecs);
    const std::set<network::ChannelKey> clientReplacementKeys =
        connectChannelKeys(result.plan.profile.pair.client.tcpChannels);
    const std::set<network::ChannelKey> agentReplacementKeys =
        listenChannelKeys(result.plan.profile.pair.agent.tcpListenChannels);

    std::set<network::ChannelKey> degraded;
    for (const network::ChannelKey& key : request.degradedChannels) {
        if (known.find(key) == known.end()) {
            appendFailure(result, "reconnect orchestration channel is not in known specs");
            break;
        }
        if (clientReplacementKeys.find(key) == clientReplacementKeys.end() ||
            agentReplacementKeys.find(key) == agentReplacementKeys.end()) {
            appendFailure(result, "reconnect orchestration channel has no replacement plan");
            break;
        }
        const auto inserted = degraded.insert(key);
        if (!inserted.second) {
            appendFailure(result, "reconnect orchestration has duplicate degraded channels");
            break;
        }
    }

    if (!result.messages.empty())
        return result;

    const std::string reason =
        request.reason.empty() ? std::string("service reconnect") : request.reason;

    result.plan.client.sessionId = result.plan.profile.pair.client.sessionId;
    result.plan.client.degradedChannels = request.degradedChannels;
    result.plan.client.teardownAfterSuccessfulRebind = request.degradedChannels;
    result.plan.client.reason = reason;
    result.plan.client.requestDisplayKeyframe = request.requestDisplayKeyframe;
    addClientReplacementChannels(result.plan.client,
                                 result.plan.profile.pair.client.tcpChannels,
                                 degraded);

    result.plan.agent.sessionId = result.plan.profile.pair.agent.sessionId;
    result.plan.agent.degradedChannels = request.degradedChannels;
    result.plan.agent.teardownAfterSuccessfulRebind = request.degradedChannels;
    result.plan.agent.reason = reason;
    result.plan.agent.requestDisplayKeyframe = request.requestDisplayKeyframe;
    addAgentReplacementListeners(result.plan.agent,
                                 result.plan.profile.pair.agent.tcpListenChannels,
                                 degraded);

    result.ok = true;
    return result;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
