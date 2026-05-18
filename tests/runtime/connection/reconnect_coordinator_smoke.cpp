#include <cassert>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/reconnect_coordinator.h"

using namespace fusiondesk;

namespace {

network::ChannelKey screenKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
}

runtime::connection::ReconnectOrchestrationRequest makeRequest()
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
    request.profile.clientSessionId = 801;
    request.profile.agentSessionId = 802;
    request.degradedChannels = {screenKey()};
    request.reason = "coordinator smoke";
    request.requestDisplayKeyframe = true;
    return request;
}

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
        return {true, client.degradedChannels, {}};
    }

    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> agentStarts;
    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> clientReconnects;
};

class FakeTeardownExecutor : public runtime::connection::IReconnectTeardownExecutor
{
public:
    runtime::connection::ReconnectTeardownDispatchResult dispatchTeardown(
        const runtime::connection::ReconnectOrchestrationSidePlan& sidePlan,
        std::uint32_t timeoutMs) override
    {
        teardowns.push_back(sidePlan);
        timeouts.push_back(timeoutMs);
        runtime::connection::ReconnectTeardownDispatchResult result;
        result.ok = true;
        result.summary.complete = true;
        result.summary.ok = true;
        result.summary.completedTargetChannels = sidePlan.teardownAfterSuccessfulRebind;
        return result;
    }

    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> teardowns;
    std::vector<std::uint32_t> timeouts;
};

void coordinatorRunsMainReconnectFrame()
{
    FakeReplacementExecutor replacement;
    FakeTeardownExecutor teardown;
    runtime::connection::ReconnectCoordinator coordinator(replacement, &teardown);
    runtime::connection::ReconnectCoordinatorOptions options;
    options.teardownTimeoutMs = 2500;

    const runtime::connection::ReconnectCoordinatorRunResult result =
        coordinator.run(makeRequest(), options);

    assert(result.ok);
    assert(result.steps.size() == 4);
    assert(result.steps[0].phase ==
           runtime::connection::ReconnectCoordinatorPhase::ResolvePlan);
    assert(result.steps[1].phase ==
           runtime::connection::ReconnectCoordinatorPhase::StartAgentReplacements);
    assert(result.steps[2].phase ==
           runtime::connection::ReconnectCoordinatorPhase::ReconnectClientReplacements);
    assert(result.steps[3].phase ==
           runtime::connection::ReconnectCoordinatorPhase::DispatchClientTeardown);
    assert(replacement.agentStarts.size() == 1);
    assert(replacement.clientReconnects.size() == 1);
    assert(teardown.teardowns.size() == 1);
    assert(teardown.timeouts.front() == 2500);
    assert(result.plan.client.sessionId == 801);
    assert(result.plan.agent.sessionId == 802);
    assert(result.clientTeardown.summary.ok);
    assert(result.clientTeardown.summary.completedTargetChannels.front() == screenKey());
}

} // namespace

int main()
{
    coordinatorRunsMainReconnectFrame();
    return 0;
}
