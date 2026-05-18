#include "fusiondesk/runtime/qt/qt_reconnect_executor.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

connection::ReconnectReplacementExecutionResult fromTransportConnect(
    const QtTransportConnectResult& result)
{
    connection::ReconnectReplacementExecutionResult execution;
    execution.ok = result.ok;
    execution.channels = result.listeningChannels;
    execution.messages = result.messages;
    return execution;
}

std::vector<network::ChannelKey> channelKeysFromReports(
    const std::vector<session::ReconnectChannelReport>& reports)
{
    std::vector<network::ChannelKey> keys;
    keys.reserve(reports.size());
    for (const session::ReconnectChannelReport& report : reports)
        keys.push_back(report.key);
    return keys;
}

connection::ReconnectReplacementExecutionResult fromReconnect(
    const QtReconnectResult& result)
{
    connection::ReconnectReplacementExecutionResult execution;
    execution.ok = result.ok;
    execution.channels = channelKeysFromReports(result.report.reboundChannels);
    if (execution.channels.empty())
        execution.channels = result.prepared.preparedChannels;
    execution.messages = result.messages;
    execution.hasSessionReport = result.report.attempted;
    execution.sessionReport = result.report;
    return execution;
}

} // namespace

QtReconnectExecutor::QtReconnectExecutor(QtRuntimeTransportManager& transportManager)
    : ownedOrchestrator_(std::make_unique<QtReconnectOrchestrator>(transportManager)),
      orchestrator_(ownedOrchestrator_.get())
{
}

QtReconnectExecutor::QtReconnectExecutor(QtReconnectOrchestrator& orchestrator)
    : orchestrator_(&orchestrator)
{
}

connection::ReconnectReplacementExecutionResult
QtReconnectExecutor::startAgentReplacements(
    const connection::ReconnectOrchestrationSidePlan& agent)
{
    return fromTransportConnect(orchestrator_->startAgentReplacementListeners(agent));
}

connection::ReconnectReplacementExecutionResult
QtReconnectExecutor::reconnectClientReplacements(
    const connection::ReconnectOrchestrationSidePlan& client)
{
    return fromReconnect(orchestrator_->reconnectClientReplacements(client));
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
