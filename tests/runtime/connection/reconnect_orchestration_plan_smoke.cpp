#include <cassert>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"

using namespace fusiondesk;

namespace {

network::ChannelKey controlKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                               protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

runtime::connection::ReconnectOrchestrationRequest makeScreenReconnectRequest()
{
    runtime::connection::ReconnectOrchestrationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:49301",
            "client-screen-reconnect-ready",
            "agent-screen-reconnect-ready"},
    };
    request.profile.clientSessionId = 501;
    request.profile.agentSessionId = 502;
    request.degradedChannels = {screenKey()};
    request.reason = "service selected screen replacement";
    request.requestDisplayKeyframe = false;
    return request;
}

void resolvesServiceReconnectPlan()
{
    const runtime::connection::ReconnectOrchestrationResult result =
        runtime::connection::resolveReconnectOrchestrationPlan(makeScreenReconnectRequest());

    assert(result.ok);
    assert(result.plan.profile.ok);
    assert(result.plan.client.sessionId == 501);
    assert(result.plan.agent.sessionId == 502);
    assert(result.plan.client.reason == "service selected screen replacement");
    assert(result.plan.agent.reason == "service selected screen replacement");
    assert(!result.plan.client.requestDisplayKeyframe);
    assert(!result.plan.agent.requestDisplayKeyframe);
    assert(result.plan.client.degradedChannels.size() == 1);
    assert(result.plan.client.degradedChannels.front() == screenKey());
    assert(result.plan.agent.degradedChannels.size() == 1);
    assert(result.plan.agent.degradedChannels.front() == screenKey());
    assert(result.plan.client.tcpChannels.size() == 1);
    assert(result.plan.client.tcpChannels.front().spec.key == screenKey());
    assert(result.plan.client.tcpChannels.front().endpoint == "127.0.0.1:49301");
    assert(result.plan.client.tcpChannels.front().readyEndpoint == "client-screen-reconnect-ready");
    assert(result.plan.agent.tcpListenChannels.size() == 1);
    assert(result.plan.agent.tcpListenChannels.front().spec.key == screenKey());
    assert(result.plan.agent.tcpListenChannels.front().readyEndpoint == "agent-screen-reconnect-ready");
    assert(result.plan.client.teardownAfterSuccessfulRebind.size() == 1);
    assert(result.plan.client.teardownAfterSuccessfulRebind.front() == screenKey());
    assert(result.plan.agent.teardownAfterSuccessfulRebind.size() == 1);
    assert(result.plan.agent.teardownAfterSuccessfulRebind.front() == screenKey());
}

void rejectsUnknownDegradedChannel()
{
    runtime::connection::ReconnectOrchestrationRequest request =
        makeScreenReconnectRequest();
    request.degradedChannels = {
        network::ChannelKey{static_cast<protocol::ChannelId>(9999),
                            protocol::ChannelType::Video},
    };

    const runtime::connection::ReconnectOrchestrationResult result =
        runtime::connection::resolveReconnectOrchestrationPlan(request);

    assert(!result.ok);
    assert(!result.messages.empty());
}

void rejectsDegradedChannelMissingReplacementPlan()
{
    runtime::connection::ReconnectOrchestrationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:49302",
            {},
            {}},
    };
    request.profile.clientSessionId = 601;
    request.profile.agentSessionId = 602;
    request.degradedChannels = {screenKey()};

    const runtime::connection::ReconnectOrchestrationResult result =
        runtime::connection::resolveReconnectOrchestrationPlan(request);

    assert(!result.ok);
    assert(!result.messages.empty());
}

void rejectsDuplicateDegradedChannels()
{
    runtime::connection::ReconnectOrchestrationRequest request =
        makeScreenReconnectRequest();
    request.degradedChannels = {screenKey(), screenKey()};

    const runtime::connection::ReconnectOrchestrationResult result =
        runtime::connection::resolveReconnectOrchestrationPlan(request);

    assert(!result.ok);
    assert(!result.messages.empty());
}

void defaultsReconnectReason()
{
    runtime::connection::ReconnectOrchestrationRequest request =
        makeScreenReconnectRequest();
    request.reason.clear();

    const runtime::connection::ReconnectOrchestrationResult result =
        runtime::connection::resolveReconnectOrchestrationPlan(request);

    assert(result.ok);
    assert(result.plan.client.reason == "service reconnect");
    assert(result.plan.agent.reason == "service reconnect");
}

} // namespace

int main()
{
    resolvesServiceReconnectPlan();
    rejectsUnknownDegradedChannel();
    rejectsDegradedChannelMissingReplacementPlan();
    rejectsDuplicateDegradedChannels();
    defaultsReconnectReason();
    return 0;
}
