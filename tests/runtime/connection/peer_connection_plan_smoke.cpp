#include <cassert>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/peer_connection_plan.h"

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

void resolvesKnownChannels()
{
    runtime::connection::PeerConnectionPlanRequest request;
    request.knownSpecs = network::defaultMvpChannelSpecs();
    request.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:48101",
            "client-control",
            "agent-control"},
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:48102",
            "client-screen",
            "agent-screen"},
    };

    const runtime::connection::PeerConnectionPlanResult resolved =
        runtime::connection::resolvePeerConnectionPlan(request);
    assert(resolved.ok);
    assert(resolved.messages.empty());
    assert(resolved.channels.size() == 2);
    assert(resolved.channels.front().spec.name == "control");
    assert(resolved.channels.back().spec.name == "main_screen");
    assert(resolved.channels.front().clientReadyEndpoint == "client-control");
    assert(resolved.channels.back().agentReadyEndpoint == "agent-screen");
}

void rejectsMissingInputs()
{
    runtime::connection::PeerConnectionPlanRequest request;
    const runtime::connection::PeerConnectionPlanResult resolved =
        runtime::connection::resolvePeerConnectionPlan(request);
    assert(!resolved.ok);
    assert(resolved.channels.empty());
    assert(resolved.messages.size() == 2);
}

void rejectsDuplicateChannelsAndEndpoints()
{
    runtime::connection::PeerConnectionPlanRequest request;
    request.knownSpecs = network::defaultMvpChannelSpecs();
    request.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:48103",
            {},
            {}},
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:48103",
            {},
            {}},
    };

    const runtime::connection::PeerConnectionPlanResult resolved =
        runtime::connection::resolvePeerConnectionPlan(request);
    assert(!resolved.ok);
    assert(resolved.channels.empty());
    assert(!resolved.messages.empty());
}

void rejectsUnknownChannelAndEmptyEndpoint()
{
    runtime::connection::PeerConnectionPlanRequest request;
    request.knownSpecs = network::defaultMvpChannelSpecs();
    request.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            network::ChannelKey{static_cast<protocol::ChannelId>(9999),
                                protocol::ChannelType::Video},
            {},
            {},
            {}},
    };

    const runtime::connection::PeerConnectionPlanResult resolved =
        runtime::connection::resolvePeerConnectionPlan(request);
    assert(!resolved.ok);
    assert(resolved.channels.empty());
    assert(resolved.messages.size() >= 2);
}

} // namespace

int main()
{
    resolvesKnownChannels();
    rejectsMissingInputs();
    rejectsDuplicateChannelsAndEndpoints();
    rejectsUnknownChannelAndEmptyEndpoint();
    return 0;
}
