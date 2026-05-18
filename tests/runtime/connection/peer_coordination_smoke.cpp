#include <cassert>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/peer_coordination.h"

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

void resolvesPeerCoordinationPlan()
{
    runtime::connection::PeerCoordinationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:49201",
            "client-control-ready",
            "agent-control-ready"},
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:49202",
            "client-screen-ready",
            "agent-screen-ready"},
    };
    request.profile.clientSessionId = 31;
    request.profile.agentSessionId = 32;
    request.degradedChannels = {screenKey()};
    request.requestDisplayKeyframe = true;

    const runtime::connection::PeerCoordinationResult result =
        runtime::connection::resolvePeerCoordination(request);
    assert(result.ok);
    assert(result.plan.profile.ok);
    assert(result.plan.profile.pair.client.sessionId == 31);
    assert(result.plan.profile.pair.agent.sessionId == 32);
    assert(result.plan.degradedChannels.size() == 1);
    assert(result.plan.degradedChannels.front() == screenKey());
    assert(result.plan.reconnect.requested);
    assert(result.plan.reconnect.degradedChannels.size() == 1);
    assert(result.plan.reconnect.degradedChannels.front() == screenKey());
    assert(result.plan.reconnect.clientReplacementTcpChannels.size() == 1);
    assert(result.plan.reconnect.clientReplacementTcpChannels.front().spec.key == screenKey());
    assert(result.plan.reconnect.clientReplacementTcpChannels.front().endpoint == "127.0.0.1:49202");
    assert(result.plan.reconnect.agentReplacementTcpListenChannels.size() == 1);
    assert(result.plan.reconnect.agentReplacementTcpListenChannels.front().spec.key == screenKey());
    assert(result.plan.reconnect.teardownAfterSuccessfulRebind.size() == 1);
    assert(result.plan.reconnect.teardownAfterSuccessfulRebind.front() == screenKey());
    assert(result.plan.requestDisplayKeyframe);
    assert(result.plan.reconnect.requestDisplayKeyframe);
}

void rejectsUnknownReconnectChannel()
{
    runtime::connection::PeerCoordinationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:49203",
            {},
            {}},
    };
    request.degradedChannels = {
        network::ChannelKey{static_cast<protocol::ChannelId>(9999), protocol::ChannelType::Video},
    };

    const runtime::connection::PeerCoordinationResult result =
        runtime::connection::resolvePeerCoordination(request);
    assert(!result.ok);
    assert(!result.messages.empty());
}

void rejectsReconnectChannelWithoutReplacementPlan()
{
    runtime::connection::PeerCoordinationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:49204",
            {},
            {}},
    };
    request.degradedChannels = {screenKey()};

    const runtime::connection::PeerCoordinationResult result =
        runtime::connection::resolvePeerCoordination(request);
    assert(!result.ok);
    assert(!result.messages.empty());
}

} // namespace

int main()
{
    resolvesPeerCoordinationPlan();
    rejectsUnknownReconnectChannel();
    rejectsReconnectChannelWithoutReplacementPlan();
    return 0;
}
