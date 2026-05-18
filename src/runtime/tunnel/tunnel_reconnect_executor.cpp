#include "fusiondesk/runtime/tunnel/tunnel_reconnect_executor.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace tunnel {

namespace {

void appendFailure(TunnelReplacementRequestBuildResult& result,
                   std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

TunnelReplacementCandidate connectCandidate(
    const connection::PeerProfileConnectChannel& channel,
    TunnelTransportMode mode)
{
    TunnelReplacementCandidate candidate;
    candidate.spec = channel.spec;
    candidate.endpoint = channel.endpoint;
    candidate.readyEndpoint = channel.readyEndpoint;
    candidate.mode = mode;
    candidate.listener = false;
    return candidate;
}

TunnelReplacementCandidate listenCandidate(
    const connection::PeerProfileListenChannel& channel,
    TunnelTransportMode mode)
{
    TunnelReplacementCandidate candidate;
    candidate.spec = channel.spec;
    candidate.endpoint = channel.endpoint;
    candidate.readyEndpoint = channel.readyEndpoint;
    candidate.mode = mode;
    candidate.listener = true;
    return candidate;
}

void appendMessages(std::vector<std::string>& target,
                    const std::vector<std::string>& messages)
{
    target.insert(target.end(), messages.begin(), messages.end());
}

connection::ReconnectReplacementExecutionResult failedExecution(
    const std::vector<std::string>& messages)
{
    connection::ReconnectReplacementExecutionResult result;
    result.ok = false;
    result.messages = messages;
    return result;
}

} // namespace

TunnelReconnectExecutor::TunnelReconnectExecutor(
    ITunnelReplacementBackend& backend,
    TunnelReconnectExecutorOptions options)
    : backend_(backend),
      options_(options)
{
}

connection::ReconnectReplacementExecutionResult
TunnelReconnectExecutor::startAgentReplacements(
    const connection::ReconnectOrchestrationSidePlan& agent)
{
    const TunnelReplacementRequestBuildResult built =
        buildTunnelReplacementRequest(agent,
                                      TunnelReplacementSide::Agent,
                                      options_);
    if (!built.ok)
        return failedExecution(built.messages);

    connection::ReconnectReplacementExecutionResult result =
        backend_.startAgentReplacements(built.request);
    appendMessages(result.messages, built.messages);
    return result;
}

connection::ReconnectReplacementExecutionResult
TunnelReconnectExecutor::reconnectClientReplacements(
    const connection::ReconnectOrchestrationSidePlan& client)
{
    const TunnelReplacementRequestBuildResult built =
        buildTunnelReplacementRequest(client,
                                      TunnelReplacementSide::Client,
                                      options_);
    if (!built.ok)
        return failedExecution(built.messages);

    connection::ReconnectReplacementExecutionResult result =
        backend_.reconnectClientReplacements(built.request);
    appendMessages(result.messages, built.messages);
    return result;
}

TunnelReplacementRequestBuildResult buildTunnelReplacementRequest(
    const connection::ReconnectOrchestrationSidePlan& sidePlan,
    TunnelReplacementSide side,
    const TunnelReconnectExecutorOptions& options)
{
    TunnelReplacementRequestBuildResult result;
    result.request.side = side;
    result.request.sessionId = sidePlan.sessionId;
    result.request.degradedChannels = sidePlan.degradedChannels;
    result.request.teardownAfterSuccessfulRebind =
        sidePlan.teardownAfterSuccessfulRebind;
    result.request.reason = sidePlan.reason;
    result.request.requestDisplayKeyframe = sidePlan.requestDisplayKeyframe;
    result.request.options = options;

    if (sidePlan.sessionId == 0)
        appendFailure(result, "tunnel replacement requires a session id");

    if (side == TunnelReplacementSide::Agent) {
        for (const connection::PeerProfileListenChannel& channel :
             sidePlan.tcpListenChannels) {
            result.request.candidates.push_back(
                listenCandidate(channel, options.preferredMode));
        }
    } else {
        for (const connection::PeerProfileConnectChannel& channel :
             sidePlan.tcpChannels) {
            result.request.candidates.push_back(
                connectCandidate(channel, options.preferredMode));
        }
    }

    if (result.request.candidates.empty()) {
        appendFailure(result,
                      side == TunnelReplacementSide::Agent
                          ? "tunnel agent replacement requires listener candidates"
                          : "tunnel client replacement requires connect candidates");
    }

    result.ok = result.messages.empty();
    return result;
}

const char* tunnelTransportModeName(TunnelTransportMode mode)
{
    switch (mode) {
    case TunnelTransportMode::LanTcp:
        return "lan_tcp";
    case TunnelTransportMode::Relay:
        return "relay";
    case TunnelTransportMode::DirectP2P:
        return "direct_p2p";
    }
    return "unknown";
}

const char* tunnelReplacementSideName(TunnelReplacementSide side)
{
    switch (side) {
    case TunnelReplacementSide::Client:
        return "client";
    case TunnelReplacementSide::Agent:
        return "agent";
    }
    return "unknown";
}

} // namespace tunnel
} // namespace runtime
} // namespace fusiondesk
