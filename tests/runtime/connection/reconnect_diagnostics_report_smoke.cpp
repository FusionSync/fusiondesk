#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/reconnect_diagnostics_report.h"
#include "fusiondesk/runtime/connection/reconnect_runtime_service.h"

using namespace fusiondesk;

namespace {

network::ChannelKey controlKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
}

class FakeChannel : public network::IChannel
{
public:
    explicit FakeChannel(network::ChannelKey key)
        : key_(key)
    {
    }

    protocol::ChannelId id() const override
    {
        return key_.channelId;
    }

    protocol::ChannelType type() const override
    {
        return key_.channelType;
    }

    bool isOpen() const override
    {
        return open;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    network::ChannelKey key_;
    bool open = true;
    std::vector<protocol::PacketEnvelope> sentPackets;
};

class FakeReplacementExecutor
    : public runtime::connection::IReconnectReplacementExecutor
{
public:
    runtime::connection::ReconnectReplacementExecutionResult startAgentReplacements(
        const runtime::connection::ReconnectOrchestrationSidePlan& agent) override
    {
        agentStarts.push_back(agent);
        return {true, agent.degradedChannels, {}};
    }

    runtime::connection::ReconnectReplacementExecutionResult reconnectClientReplacements(
        const runtime::connection::ReconnectOrchestrationSidePlan& client) override
    {
        clientReconnects.push_back(client);
        runtime::connection::ReconnectReplacementExecutionResult result;
        result.ok = true;
        result.channels = client.degradedChannels;
        result.hasSessionReport = hasSessionReport;
        result.sessionReport = sessionReport;
        return result;
    }

    bool hasSessionReport = false;
    session::ReconnectReport sessionReport;
    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> agentStarts;
    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> clientReconnects;
};

class FakeTeardownExecutor
    : public runtime::connection::IReconnectTeardownExecutor
{
public:
    runtime::connection::ReconnectTeardownDispatchResult dispatchTeardown(
        const runtime::connection::ReconnectOrchestrationSidePlan& sidePlan,
        std::uint32_t timeoutMs) override
    {
        (void)timeoutMs;
        runtime::connection::ReconnectTeardownDispatchResult result;
        result.ok = true;
        result.summary.complete = true;
        result.summary.ok = true;
        result.summary.completedTargetChannels =
            sidePlan.teardownAfterSuccessfulRebind;
        return result;
    }
};

class FakeCloseTarget
    : public runtime::connection::IReconnectTeardownCloseTarget
{
public:
    runtime::connection::ReconnectTeardownCloseResult closeOldTransport(
        const runtime::connection::ReconnectTeardownCloseRequest& request) override
    {
        requests.push_back(request);
        return runtime::connection::ReconnectTeardownCloseResult::closed("closed");
    }

    std::vector<runtime::connection::ReconnectTeardownCloseRequest> requests;
};

runtime::connection::ReconnectOrchestrationRequest reconnectRequest()
{
    runtime::connection::ReconnectOrchestrationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:49351",
            "client-reconnect-ready",
            "agent-reconnect-ready"},
    };
    request.profile.clientSessionId = 8101;
    request.profile.agentSessionId = 8102;
    request.degradedChannels = {screenKey()};
    request.reason = "diagnostics smoke";
    request.requestDisplayKeyframe = true;
    return request;
}

bool hasStage(const runtime::connection::ReconnectDiagnosticsReport& report,
              runtime::connection::ReconnectDiagnosticStage stage)
{
    for (const runtime::connection::ReconnectDiagnosticStageReport& item :
         report.stages) {
        if (item.stage == stage)
            return true;
    }
    return false;
}

std::size_t messageCount(const runtime::connection::ReconnectDiagnosticsReport& report,
                         const std::string& message)
{
    std::size_t count = 0;
    for (const std::string& item : report.messages) {
        if (item == message)
            ++count;
    }
    return count;
}

void buildsCombinedReconnectDiagnosticsReport()
{
    FakeReplacementExecutor replacement;
    FakeTeardownExecutor teardown;
    runtime::connection::ReconnectCoordinator coordinator(replacement, &teardown);
    const runtime::connection::ReconnectCoordinatorRunResult run =
        coordinator.run(reconnectRequest());
    assert(run.ok);

    session::ReconnectReport sessionReport;
    sessionReport.attempted = true;
    sessionReport.ok = true;
    sessionReport.reason = "diagnostics smoke";
    sessionReport.requestedFreshState = true;
    session::ReconnectChannelReport degraded;
    degraded.key = screenKey();
    degraded.operation = session::ReconnectChannelOperation::Degrade;
    degraded.ok = true;
    sessionReport.degradedChannels.push_back(degraded);
    session::ReconnectChannelReport rebound = degraded;
    rebound.operation = session::ReconnectChannelOperation::Rebind;
    sessionReport.reboundChannels.push_back(rebound);

    runtime::connection::ReconnectTeardownServiceSnapshot teardownSnapshot;
    teardownSnapshot.active = true;
    teardownSnapshot.summary.complete = true;
    teardownSnapshot.summary.ok = true;

    runtime::connection::ReconnectDiagnosticsInput input;
    input.active = true;
    input.hasRun = true;
    input.run = run;
    input.run.messages.push_back("shared diagnostic");
    input.hasSessionReport = true;
    input.sessionReport = sessionReport;
    input.teardown = teardownSnapshot;
    input.runtimeMessages.push_back("shared diagnostic");

    const runtime::connection::ReconnectDiagnosticsReport report =
        runtime::connection::buildReconnectDiagnosticsReport(input);
    assert(report.active);
    assert(report.attempted);
    assert(report.complete);
    assert(report.ok);
    assert(report.clientSessionId == 8101);
    assert(report.agentSessionId == 8102);
    assert(report.reason == "diagnostics smoke");
    assert(report.requestDisplayKeyframe);
    assert(report.degradedChannels.size() == 1);
    assert(report.pendingRequests == 0);
    assert(report.expiredRequests == 0);
    assert(report.timeoutOk);
    assert(messageCount(report, "shared diagnostic") == 1);
    assert(hasStage(report, runtime::connection::ReconnectDiagnosticStage::Plan));
    assert(hasStage(report, runtime::connection::ReconnectDiagnosticStage::AgentReplacement));
    assert(hasStage(report, runtime::connection::ReconnectDiagnosticStage::ClientReplacement));
    assert(hasStage(report, runtime::connection::ReconnectDiagnosticStage::ClientTeardown));
    assert(hasStage(report, runtime::connection::ReconnectDiagnosticStage::SessionRebind));
    assert(std::string(runtime::connection::reconnectDiagnosticStageName(
               runtime::connection::ReconnectDiagnosticStage::Timeout)) == "timeout");
}

void runtimeSnapshotReportsPendingTeardown()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    FakeReplacementExecutor replacement;
    replacement.hasSessionReport = true;
    replacement.sessionReport.attempted = true;
    replacement.sessionReport.ok = true;
    replacement.sessionReport.reason = "diagnostics smoke";
    replacement.sessionReport.requestedFreshState = true;
    session::ReconnectChannelReport rebind;
    rebind.key = screenKey();
    rebind.operation = session::ReconnectChannelOperation::Rebind;
    rebind.ok = true;
    replacement.sessionReport.reboundChannels.push_back(rebind);
    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectRuntimeService service(replacement,
                                                         router,
                                                         closeTarget,
                                                         9000);
    assert(service.start().ok);

    runtime::connection::ReconnectCoordinatorOptions options;
    options.teardownTimeoutMs = 3000;
    const runtime::connection::ReconnectCoordinatorRunResult run =
        service.run(reconnectRequest(), options);
    assert(run.ok);
    assert(control->sentPackets.size() == 1);

    runtime::connection::ReconnectRuntimeServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.pendingRequests.size() == 1);
    assert(snapshot.diagnostics.attempted);
    assert(!snapshot.diagnostics.complete);
    assert(!snapshot.diagnostics.ok);
    assert(snapshot.diagnostics.pendingRequests == 1);
    assert(hasStage(snapshot.diagnostics,
                    runtime::connection::ReconnectDiagnosticStage::Timeout));
    assert(hasStage(snapshot.diagnostics,
                    runtime::connection::ReconnectDiagnosticStage::SessionRebind));

    assert(service.expire(9000 + 3000 * 1000) == 1);
    snapshot = service.snapshot();
    assert(snapshot.pendingRequests.empty());
    assert(snapshot.diagnostics.complete);
    assert(!snapshot.diagnostics.ok);
    assert(snapshot.diagnostics.expiredRequests == 1);
}

} // namespace

int main()
{
    buildsCombinedReconnectDiagnosticsReport();
    runtimeSnapshotReportsPendingTeardown();
    return 0;
}
