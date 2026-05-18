#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/peer_profile_runtime_service.h"

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

class LoopbackChannel : public network::IChannel
{
public:
    LoopbackChannel(network::ChannelKey key, network::INetworkRouter* peer)
        : key_(key),
          peer_(peer)
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
        if (peer_)
            peer_->submitIncoming(packet);
        return network::SendResult::sent();
    }

    network::ChannelKey key_;
    network::INetworkRouter* peer_ = nullptr;
    bool open = true;
    std::vector<protocol::PacketEnvelope> sentPackets;
};

runtime::connection::PeerProfileExchangeRequest profileRequest()
{
    runtime::connection::PeerProfileExchangeRequest request;
    request.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:52001",
            "client-control-ready",
            "agent-control-ready"},
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:52002",
            "client-screen-ready",
            "agent-screen-ready"},
    };
    request.clientSessionId = 4101;
    request.agentSessionId = 4202;
    return request;
}

void runtimeServiceDispatchesTracksAndCompletesFdppExchange()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    auto clientControl = std::make_shared<LoopbackChannel>(controlKey(), &agentRouter);
    auto agentControl = std::make_shared<LoopbackChannel>(controlKey(), &clientRouter);
    assert(clientRouter.registerChannel(clientControl));
    assert(agentRouter.registerChannel(agentControl));

    runtime::connection::PeerProfileRuntimeService clientService(clientRouter, 3000);
    runtime::connection::PeerProfileRuntimeService agentService(agentRouter, 5000);

    runtime::connection::PeerProfileRuntimeServiceStartOptions clientStart;
    clientStart.startResponder = false;
    assert(clientService.start(clientStart).ok);

    runtime::connection::PeerProfileRuntimeServiceStartOptions agentStart;
    agentStart.subscribeResponses = false;
    agentStart.responder.firstResponseMessageId = 7000;
    assert(agentService.start(agentStart).ok);

    runtime::connection::PeerProfileRuntimeExchangeOptions options;
    options.wire.messageId = 0;
    options.wire.traceId = 88;
    options.wire.timeoutMs = 1500;
    options.wire.monotonicTimestampUsec = 10000;
    const runtime::connection::PeerProfileRuntimeDispatchResult dispatched =
        clientService.requestPeerProfile(profileRequest(), options);
    assert(dispatched.ok);
    assert(dispatched.request.messageId == 3000);
    assert(dispatched.request.correlationId == 3000);
    assert(dispatched.request.timeoutMs == 1500);
    assert(clientControl->sentPackets.size() == 1);
    assert(agentControl->sentPackets.size() == 1);

    const runtime::connection::PeerProfileRuntimeServiceSnapshot clientSnapshot =
        clientService.snapshot();
    assert(clientSnapshot.active);
    assert(clientSnapshot.pendingRequests == 0);
    assert(clientSnapshot.completedResponses == 1);
    assert(clientSnapshot.completions.front().ok);
    assert(clientSnapshot.completions.front().response.messageId == 7000);
    assert(clientSnapshot.completions.front().response.responseTo == 3000);
    assert(clientSnapshot.completions.front().exchange.pair.client.sessionId == 4101);
    assert(clientSnapshot.completions.front().exchange.pair.agent.sessionId == 4202);
    assert(clientSnapshot.completions.front().exchange.pair.client.tcpChannels.size() == 2);
    assert(clientSnapshot.completions.front().exchange.pair.agent.tcpListenChannels.size() == 2);
    assert(clientSnapshot.completions.front().exchange.pair.client.tcpChannels.back().endpoint ==
           "127.0.0.1:52002");
    assert(clientSnapshot.completions.front().exchange.pair.agent.tcpListenChannels.back().readyEndpoint ==
           "agent-screen-ready");

    const runtime::connection::PeerProfileRuntimeServiceSnapshot agentSnapshot =
        agentService.snapshot();
    assert(agentSnapshot.responder.active);
    assert(agentSnapshot.responder.handledRequests == 1);
    assert(agentSnapshot.responder.sentResponses == 1);
    assert(agentSnapshot.pendingRequests == 0);
}

void runtimeServiceExpiresUnansweredFdppRequest()
{
    network::NetworkRouter clientRouter;
    auto clientControl = std::make_shared<LoopbackChannel>(controlKey(), nullptr);
    assert(clientRouter.registerChannel(clientControl));

    runtime::connection::PeerProfileRuntimeService clientService(clientRouter, 8000);
    runtime::connection::PeerProfileRuntimeServiceStartOptions start;
    start.startResponder = false;
    assert(clientService.start(start).ok);

    runtime::connection::PeerProfileRuntimeExchangeOptions options;
    options.wire.timeoutMs = 10;
    options.wire.monotonicTimestampUsec = 20000;
    const runtime::connection::PeerProfileRuntimeDispatchResult dispatched =
        clientService.requestPeerProfile(profileRequest(), options);
    assert(dispatched.ok);
    assert(clientService.snapshot().pendingRequests == 1);

    assert(clientService.expire(30000) == 1);
    const runtime::connection::PeerProfileRuntimeServiceSnapshot snapshot =
        clientService.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.expiredRequests == 1);
    assert(snapshot.completedResponses == 1);
    assert(!snapshot.completions.front().ok);
    assert(snapshot.completions.front().response.responseStatus ==
           protocol::ResponseStatus::Timeout);
}

} // namespace

int main()
{
    runtimeServiceDispatchesTracksAndCompletesFdppExchange();
    runtimeServiceExpiresUnansweredFdppRequest();
    return 0;
}
