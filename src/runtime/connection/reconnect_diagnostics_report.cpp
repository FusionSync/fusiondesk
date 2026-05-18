#include "fusiondesk/runtime/connection/reconnect_diagnostics_report.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

bool sameChannel(network::ChannelKey lhs, network::ChannelKey rhs)
{
    return lhs.channelId == rhs.channelId && lhs.channelType == rhs.channelType;
}

void appendMessage(std::vector<std::string>& target, const std::string& message)
{
    if (message.empty())
        return;

    for (const std::string& existing : target) {
        if (existing == message)
            return;
    }

    target.push_back(message);
}

void appendMessages(std::vector<std::string>& target,
                    const std::vector<std::string>& messages)
{
    for (const std::string& message : messages)
        appendMessage(target, message);
}

void appendUniqueChannel(std::vector<network::ChannelKey>& target,
                         network::ChannelKey channel)
{
    if (channel.channelId == 0)
        return;

    for (network::ChannelKey existing : target) {
        if (sameChannel(existing, channel))
            return;
    }

    target.push_back(channel);
}

ReconnectDiagnosticStage stageFromPhase(ReconnectCoordinatorPhase phase)
{
    switch (phase) {
    case ReconnectCoordinatorPhase::ResolvePlan:
        return ReconnectDiagnosticStage::Plan;
    case ReconnectCoordinatorPhase::StartAgentReplacements:
        return ReconnectDiagnosticStage::AgentReplacement;
    case ReconnectCoordinatorPhase::ReconnectClientReplacements:
        return ReconnectDiagnosticStage::ClientReplacement;
    case ReconnectCoordinatorPhase::DispatchClientTeardown:
        return ReconnectDiagnosticStage::ClientTeardown;
    case ReconnectCoordinatorPhase::DispatchAgentTeardown:
        return ReconnectDiagnosticStage::AgentTeardown;
    }
    return ReconnectDiagnosticStage::Plan;
}

ReconnectDiagnosticStageReport stageFromCoordinatorStep(
    const ReconnectCoordinatorStepReport& step)
{
    ReconnectDiagnosticStageReport report;
    report.stage = stageFromPhase(step.phase);
    report.attempted = step.attempted;
    report.ok = step.ok;
    report.channels = step.channels;
    report.messages = step.messages;
    return report;
}

void appendSessionReportMessages(ReconnectDiagnosticStageReport& stage,
                                 const session::ReconnectReport& sessionReport)
{
    for (const session::ReconnectChannelReport& channel :
         sessionReport.degradedChannels) {
        appendUniqueChannel(stage.channels, channel.key);
        if (!channel.ok)
            appendMessage(stage.messages, channel.message);
    }

    for (const session::ReconnectChannelReport& channel :
         sessionReport.reboundChannels) {
        appendUniqueChannel(stage.channels, channel.key);
        if (!channel.ok)
            appendMessage(stage.messages, channel.message);
    }

    for (const module::ModuleIngressReplayReport& replay :
         sessionReport.replayedIngress) {
        if (!replay.replayed)
            appendMessage(stage.messages,
                          replay.message.empty() ? replay.diagnostics
                                                 : replay.message);
    }
}

ReconnectDiagnosticStageReport stageFromSessionReport(
    const session::ReconnectReport& sessionReport)
{
    ReconnectDiagnosticStageReport stage;
    stage.stage = ReconnectDiagnosticStage::SessionRebind;
    stage.attempted = sessionReport.attempted;
    stage.ok = sessionReport.ok;
    appendSessionReportMessages(stage, sessionReport);
    return stage;
}

ReconnectDiagnosticStageReport timeoutStage(
    const ReconnectDiagnosticsInput& input)
{
    ReconnectDiagnosticStageReport stage;
    stage.stage = ReconnectDiagnosticStage::Timeout;
    stage.attempted = input.teardown.pendingRequests != 0 ||
                      !input.teardown.pending.empty() ||
                      input.expiredRequests != 0;
    stage.pending = input.teardown.pendingRequests != 0 ||
                    !input.teardown.pending.empty();
    stage.ok = input.expiredRequests == 0 && !stage.pending;
    for (const network::PendingRequestSnapshot& pending :
         input.teardown.pending) {
        appendUniqueChannel(stage.channels,
                            network::ChannelKey{pending.channelId,
                                                pending.channelType});
    }
    if (stage.pending)
        stage.messages.push_back("reconnect has pending teardown requests");
    if (input.expiredRequests != 0)
        stage.messages.push_back("reconnect requests expired");
    return stage;
}

bool allAttemptedStagesOk(
    const std::vector<ReconnectDiagnosticStageReport>& stages)
{
    for (const ReconnectDiagnosticStageReport& stage : stages) {
        if (stage.attempted && !stage.ok)
            return false;
    }
    return true;
}

void copyPlanSummary(ReconnectDiagnosticsReport& report,
                     const ReconnectCoordinatorRunResult& run)
{
    if (run.plan.client.sessionId != 0)
        report.clientSessionId = run.plan.client.sessionId;
    if (run.plan.agent.sessionId != 0)
        report.agentSessionId = run.plan.agent.sessionId;

    report.reason = !run.plan.client.reason.empty()
        ? run.plan.client.reason
        : run.plan.agent.reason;
    report.requestDisplayKeyframe =
        run.plan.client.requestDisplayKeyframe ||
        run.plan.agent.requestDisplayKeyframe;
    report.degradedChannels = run.plan.client.degradedChannels;
    for (network::ChannelKey channel : run.plan.agent.degradedChannels)
        appendUniqueChannel(report.degradedChannels, channel);
}

} // namespace

ReconnectDiagnosticsReport buildReconnectDiagnosticsReport(
    const ReconnectDiagnosticsInput& input)
{
    ReconnectDiagnosticsReport report;
    report.active = input.active;
    report.pendingRequests = input.teardown.pendingRequests;
    if (report.pendingRequests == 0)
        report.pendingRequests = input.teardown.pending.size();
    report.expiredRequests = input.expiredRequests;
    report.timeoutOk = report.pendingRequests == 0 && input.expiredRequests == 0;
    appendMessages(report.messages, input.runtimeMessages);
    appendMessages(report.messages, input.run.messages);
    appendMessages(report.messages, input.teardown.messages);
    appendMessages(report.messages, input.teardown.summary.messages);

    if (input.hasRun) {
        report.attempted = true;
        copyPlanSummary(report, input.run);
        for (const ReconnectCoordinatorStepReport& step : input.run.steps)
            report.stages.push_back(stageFromCoordinatorStep(step));
    }

    if (input.hasSessionReport) {
        report.attempted = report.attempted || input.sessionReport.attempted;
        if (report.reason.empty())
            report.reason = input.sessionReport.reason;
        report.requestDisplayKeyframe =
            report.requestDisplayKeyframe ||
            input.sessionReport.requestedFreshState;
        for (const session::ReconnectChannelReport& channel :
             input.sessionReport.degradedChannels) {
            appendUniqueChannel(report.degradedChannels, channel.key);
        }
        report.stages.push_back(stageFromSessionReport(input.sessionReport));
    }

    const ReconnectDiagnosticStageReport timeout = timeoutStage(input);
    if (timeout.attempted)
        report.stages.push_back(timeout);

    report.complete = report.attempted && report.pendingRequests == 0;
    report.ok = report.attempted &&
                (input.hasRun ? input.run.ok : true) &&
                (!input.hasSessionReport || input.sessionReport.ok) &&
                allAttemptedStagesOk(report.stages) &&
                report.timeoutOk;
    return report;
}

const char* reconnectDiagnosticStageName(ReconnectDiagnosticStage stage)
{
    switch (stage) {
    case ReconnectDiagnosticStage::Plan:
        return "plan";
    case ReconnectDiagnosticStage::AgentReplacement:
        return "agent_replacement";
    case ReconnectDiagnosticStage::ClientReplacement:
        return "client_replacement";
    case ReconnectDiagnosticStage::SessionRebind:
        return "session_rebind";
    case ReconnectDiagnosticStage::ClientTeardown:
        return "client_teardown";
    case ReconnectDiagnosticStage::AgentTeardown:
        return "agent_teardown";
    case ReconnectDiagnosticStage::Timeout:
        return "timeout";
    }
    return "unknown";
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
