#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/reconnect_runtime_service.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_service.h"

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

class FakeCloseTarget : public runtime::connection::IReconnectTeardownCloseTarget
{
public:
    runtime::connection::ReconnectTeardownCloseResult closeOldTransport(
        const runtime::connection::ReconnectTeardownCloseRequest& request) override
    {
        requests.push_back(request);
        return result;
    }

    runtime::connection::ReconnectTeardownCloseResult result =
        runtime::connection::ReconnectTeardownCloseResult::closed("closed");
    std::vector<runtime::connection::ReconnectTeardownCloseRequest> requests;
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
        return {true, client.degradedChannels, {}};
    }

    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> agentStarts;
    std::vector<runtime::connection::ReconnectOrchestrationSidePlan> clientReconnects;
};

runtime::connection::ReconnectOrchestrationSidePlan sidePlan(protocol::SessionId sessionId)
{
    runtime::connection::ReconnectOrchestrationSidePlan plan;
    plan.sessionId = sessionId;
    plan.teardownAfterSuccessfulRebind = {screenKey()};
    plan.reason = "service teardown";
    return plan;
}

runtime::connection::ReconnectOrchestrationRequest reconnectRequest(
    protocol::SessionId clientSessionId,
    protocol::SessionId agentSessionId)
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
    request.profile.clientSessionId = clientSessionId;
    request.profile.agentSessionId = agentSessionId;
    request.degradedChannels = {screenKey()};
    request.reason = "runtime service stop";
    request.requestDisplayKeyframe = true;
    return request;
}

protocol::PacketEnvelope responseFor(const protocol::PacketEnvelope& request,
                                     protocol::ResponseStatus status,
                                     protocol::MessageId messageId)
{
    runtime::connection::ReconnectTeardownWireResponseOptions options;
    options.status = status;
    options.messageId = messageId;
    options.message = status == protocol::ResponseStatus::Ok ? "closed" : "not closed";
    return runtime::connection::makeReconnectTeardownResponsePacket(request, options);
}

void serviceDispatchesTracksAckAndTerminalResponse()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    network::RequestTracker tracker(5000);
    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownService service(router, tracker, closeTarget);
    runtime::connection::ReconnectTeardownServiceStartResult started = service.start();
    assert(started.ok);
    assert(started.responseTokens.size() == 5);
    assert(service.active());

    runtime::connection::ReconnectTeardownDispatchOptions options;
    options.wire.traceId = 77;
    options.wire.monotonicTimestampUsec = 1000;
    const runtime::connection::ReconnectTeardownDispatchResult dispatched =
        service.dispatch(sidePlan(9101), 1000, options);
    assert(dispatched.ok);
    assert(dispatched.sentRequests.size() == 1);
    assert(control->sentPackets.size() == 1);
    assert(dispatched.sentRequests.front().messageId == 5000);
    assert(tracker.pendingCount() == 1);

    router.submitIncoming(responseFor(dispatched.sentRequests.front(),
                                      protocol::ResponseStatus::Accepted,
                                      6000));
    runtime::connection::ReconnectTeardownServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.pendingRequests == 1);
    assert(snapshot.interimResponses == 1);
    assert(snapshot.terminalResponses == 0);
    assert(!snapshot.summary.complete);

    router.submitIncoming(responseFor(dispatched.sentRequests.front(),
                                      protocol::ResponseStatus::Ok,
                                      6001));
    snapshot = service.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.interimResponses == 1);
    assert(snapshot.terminalResponses == 1);
    assert(snapshot.summary.complete);
    assert(snapshot.summary.ok);
    assert(snapshot.summary.completedTargetChannels.size() == 1);
    assert(snapshot.summary.completedTargetChannels.front() == screenKey());
}

void serviceExpiresPendingTeardown()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    network::RequestTracker tracker(7000);
    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownService service(router, tracker, closeTarget);
    assert(service.start().ok);

    runtime::connection::ReconnectTeardownDispatchOptions options;
    options.wire.monotonicTimestampUsec = 2000;
    const runtime::connection::ReconnectTeardownDispatchResult dispatched =
        service.dispatch(sidePlan(9201), 10, options);
    assert(dispatched.ok);
    assert(tracker.pendingCount() == 1);

    assert(service.expire(2000 + 10 * 1000) == 1);
    const runtime::connection::ReconnectTeardownServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.terminalResponses == 1);
    assert(snapshot.summary.complete);
    assert(!snapshot.summary.ok);
}

void serviceHandlesPeerSideCloseRequest()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    network::RequestTracker tracker(8000);
    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownService service(router, tracker, closeTarget);

    runtime::connection::ReconnectTeardownServiceOptions options;
    options.handler.firstResponseMessageId = 9000;
    assert(service.start(options).ok);

    runtime::connection::ReconnectTeardownCommand command;
    command.sessionId = 9301;
    command.targetChannel = screenKey();
    command.messageId = 8100;
    command.correlationId = 8100;
    command.timeoutMs = 1000;
    command.reason = "peer close";
    router.submitIncoming(runtime::connection::makeReconnectTeardownRequestPacket(command));

    assert(closeTarget.requests.size() == 1);
    assert(closeTarget.requests.front().sessionId == 9301);
    assert(closeTarget.requests.front().targetChannel == screenKey());
    assert(control->sentPackets.size() == 1);
    const protocol::PacketEnvelope& response = control->sentPackets.front();
    assert(response.messageId == 9000);
    assert(response.responseTo == 8100);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);
    assert(service.snapshot().handler.handledRequests == 1);
}

void serviceRejectsInactiveDispatch()
{
    network::NetworkRouter router;
    network::RequestTracker tracker(10000);
    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownService service(router, tracker, closeTarget);

    const runtime::connection::ReconnectTeardownDispatchResult dispatched =
        service.dispatch(sidePlan(9401), 1000);
    assert(!dispatched.ok);
    assert(!dispatched.messages.empty());
}

void runtimeServiceStopCancelsPendingTeardown()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    FakeCloseTarget closeTarget;
    FakeReplacementExecutor replacement;
    runtime::connection::ReconnectRuntimeService service(
        replacement,
        router,
        closeTarget,
        12000);
    assert(service.start().ok);

    runtime::connection::ReconnectCoordinatorOptions options;
    options.teardownTimeoutMs = 3000;
    const runtime::connection::ReconnectCoordinatorRunResult run =
        service.run(reconnectRequest(9501, 9502), options);
    assert(run.ok);
    assert(run.clientTeardown.ok);
    assert(control->sentPackets.size() == 1);
    assert(service.snapshot().pendingRequests.size() == 1);

    service.stop();

    const runtime::connection::ReconnectRuntimeServiceSnapshot snapshot =
        service.snapshot();
    assert(!snapshot.active);
    assert(snapshot.pendingRequests.empty());
    assert(snapshot.teardown.pendingRequests == 0);
    assert(snapshot.teardown.terminalResponses == 1);
    assert(snapshot.teardown.summary.complete);
    assert(!snapshot.teardown.summary.ok);
}

} // namespace

int main()
{
    serviceDispatchesTracksAckAndTerminalResponse();
    serviceExpiresPendingTeardown();
    serviceHandlesPeerSideCloseRequest();
    serviceRejectsInactiveDispatch();
    runtimeServiceStopCancelsPendingTeardown();
    return 0;
}
