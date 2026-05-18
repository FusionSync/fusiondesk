#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_DIAGNOSTICS_REPORT_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_DIAGNOSTICS_REPORT_H

#include <cstddef>
#include <string>
#include <vector>

#include "fusiondesk/core/session/session.h"
#include "fusiondesk/runtime/connection/reconnect_coordinator.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

enum class ReconnectDiagnosticStage
{
    Plan,
    AgentReplacement,
    ClientReplacement,
    SessionRebind,
    ClientTeardown,
    AgentTeardown,
    Timeout
};

struct ReconnectDiagnosticStageReport
{
    ReconnectDiagnosticStage stage = ReconnectDiagnosticStage::Plan;
    bool attempted = false;
    bool ok = false;
    bool pending = false;
    std::vector<network::ChannelKey> channels;
    std::vector<std::string> messages;
};

struct ReconnectDiagnosticsInput
{
    bool active = false;
    bool hasRun = false;
    ReconnectCoordinatorRunResult run;
    bool hasSessionReport = false;
    session::ReconnectReport sessionReport;
    ReconnectTeardownServiceSnapshot teardown;
    std::size_t expiredRequests = 0;
    std::vector<std::string> runtimeMessages;
};

struct ReconnectDiagnosticsReport
{
    bool active = false;
    bool attempted = false;
    bool complete = false;
    bool ok = false;
    protocol::SessionId clientSessionId = 0;
    protocol::SessionId agentSessionId = 0;
    std::string reason;
    bool requestDisplayKeyframe = false;
    std::vector<network::ChannelKey> degradedChannels;
    std::size_t pendingRequests = 0;
    std::size_t expiredRequests = 0;
    bool timeoutOk = true;
    std::vector<ReconnectDiagnosticStageReport> stages;
    std::vector<std::string> messages;
};

ReconnectDiagnosticsReport buildReconnectDiagnosticsReport(
    const ReconnectDiagnosticsInput& input);

const char* reconnectDiagnosticStageName(ReconnectDiagnosticStage stage);

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_DIAGNOSTICS_REPORT_H
