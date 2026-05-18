#include <cassert>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/peer_profile_exchange.h"

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

void resolvesPeerProfileExchangePair()
{
    runtime::connection::PeerProfileExchangeRequest request;
    request.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:49141",
            "client-control-ready",
            "agent-control-ready"},
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:49142",
            "client-screen-ready",
            "agent-screen-ready"},
    };
    request.clientSessionId = 11;
    request.agentSessionId = 22;

    const runtime::connection::PeerProfileExchangeResult result =
        runtime::connection::resolvePeerProfileExchange(request);
    assert(result.ok);
    assert(result.pair.client.sessionId == 11);
    assert(result.pair.agent.sessionId == 22);
    assert(result.pair.client.tcpChannels.size() == 2);
    assert(result.pair.agent.tcpListenChannels.size() == 2);
    assert(result.pair.client.tcpChannels.front().endpoint == "127.0.0.1:49141");
    assert(result.pair.client.tcpChannels.front().readyEndpoint == "client-control-ready");
    assert(result.pair.agent.tcpListenChannels.back().readyEndpoint == "agent-screen-ready");
}

void rejectsInvalidPeerProfileExchange()
{
    runtime::connection::PeerProfileExchangeRequest request;
    request.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            network::ChannelKey{static_cast<protocol::ChannelId>(9999),
                                protocol::ChannelType::Video},
            "127.0.0.1:49143",
            {},
            {}},
    };

    const runtime::connection::PeerProfileExchangeResult result =
        runtime::connection::resolvePeerProfileExchange(request);
    assert(!result.ok);
    assert(!result.messages.empty());
}

} // namespace

int main()
{
    resolvesPeerProfileExchangePair();
    rejectsInvalidPeerProfileExchange();
    return 0;
}
