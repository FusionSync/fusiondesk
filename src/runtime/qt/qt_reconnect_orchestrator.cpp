#include "fusiondesk/runtime/qt/qt_reconnect_orchestrator.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

void appendFailure(QtTransportConnectResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(QtReconnectResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendMessage(QtReconnectOrchestrationStartResult& result, std::string message)
{
    result.messages.push_back(std::move(message));
}

std::vector<QtTcpChannelProfile> toQtTcpChannels(
    const std::vector<connection::PeerProfileConnectChannel>& channels)
{
    std::vector<QtTcpChannelProfile> result;
    for (const connection::PeerProfileConnectChannel& channel : channels) {
        QtTcpChannelProfile profile;
        profile.spec = channel.spec;
        profile.endpoint = channel.endpoint;
        profile.ready.endpoint = channel.readyEndpoint;
        result.push_back(std::move(profile));
    }
    return result;
}

std::vector<QtTcpListenChannelProfile> toQtTcpListenChannels(
    const std::vector<connection::PeerProfileListenChannel>& channels)
{
    std::vector<QtTcpListenChannelProfile> result;
    for (const connection::PeerProfileListenChannel& channel : channels) {
        QtTcpListenChannelProfile profile;
        profile.spec = channel.spec;
        profile.endpoint = channel.endpoint;
        profile.ready.endpoint = channel.readyEndpoint;
        result.push_back(std::move(profile));
    }
    return result;
}

} // namespace

QtReconnectOrchestrator::QtReconnectOrchestrator(
    QtRuntimeTransportManager& transportManager)
    : transportManager_(transportManager)
{
}

QtTransportConnectResult QtReconnectOrchestrator::startAgentReplacementListeners(
    const connection::ReconnectOrchestrationSidePlan& sidePlan)
{
    QtTransportConnectResult result;
    if (sidePlan.tcpListenChannels.empty()) {
        appendFailure(result, "qt reconnect orchestration requires agent listen channels");
        return result;
    }

    return transportManager_.listenReconnectTcpChannels(
        sidePlan.sessionId,
        toQtTcpListenChannels(sidePlan.tcpListenChannels),
        sidePlan.reason,
        sidePlan.requestDisplayKeyframe);
}

QtReconnectResult QtReconnectOrchestrator::reconnectClientReplacements(
    const connection::ReconnectOrchestrationSidePlan& sidePlan)
{
    QtReconnectResult result;
    if (sidePlan.tcpChannels.empty()) {
        appendFailure(result, "qt reconnect orchestration requires client tcp channels");
        return result;
    }

    return transportManager_.reconnectTcpChannels(
        sidePlan.sessionId,
        toQtTcpChannels(sidePlan.tcpChannels),
        sidePlan.reason,
        sidePlan.requestDisplayKeyframe);
}

QtReconnectOrchestrationStartResult QtReconnectOrchestrator::startLocalTcpPlan(
    const connection::ReconnectOrchestrationPlan& plan)
{
    QtReconnectOrchestrationStartResult result;
    result.ok = true;

    result.agentListeners = startAgentReplacementListeners(plan.agent);
    if (!result.agentListeners.ok)
        result.ok = false;
    for (const std::string& message : result.agentListeners.messages)
        appendMessage(result, message);

    if (!result.agentListeners.ok)
        return result;

    result.clientReconnect = reconnectClientReplacements(plan.client);
    if (!result.clientReconnect.ok)
        result.ok = false;
    for (const std::string& message : result.clientReconnect.messages)
        appendMessage(result, message);

    return result;
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
