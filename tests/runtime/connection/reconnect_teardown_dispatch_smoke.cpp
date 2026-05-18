#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_dispatch.h"

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
        if (status != network::SendStatus::Sent)
            return {status, failureMessage};
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    network::ChannelKey key_;
    bool open = true;
    network::SendStatus status = network::SendStatus::Sent;
    std::string failureMessage;
    std::vector<protocol::PacketEnvelope> sentPackets;
};

runtime::connection::ReconnectTeardownPlan makeZeroIdPlan()
{
    runtime::connection::ReconnectTeardownCommand command;
    command.sessionId = 9001;
    command.targetChannel = screenKey();
    command.timeoutMs = 2500;
    command.reason = "dispatch after rebind";

    runtime::connection::ReconnectTeardownPlan plan;
    plan.commands.push_back(command);
    return plan;
}

void dispatchAssignsMessageIdTracksAndSends()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));
    network::RequestTracker tracker(3000);
    runtime::connection::ReconnectTeardownDispatcher dispatcher(router, tracker);

    runtime::connection::ReconnectTeardownDispatchOptions options;
    options.wire.traceId = 77;
    options.wire.sequence = 10;
    options.wire.monotonicTimestampUsec = 100000;
    const runtime::connection::ReconnectTeardownDispatchResult result =
        dispatcher.dispatch(makeZeroIdPlan(), options);

    assert(result.ok);
    assert(result.sentRequests.size() == 1);
    assert(control->sentPackets.size() == 1);
    assert(tracker.pendingCount() == 1);
    assert(dispatcher.activePlan().commands.front().messageId == 3000);
    assert(dispatcher.activePlan().commands.front().correlationId == 3000);

    const protocol::PacketEnvelope& request = control->sentPackets.front();
    assert(request.sessionId == 9001);
    assert(request.traceId == 77);
    assert(request.messageId == 3000);
    assert(request.correlationId == 3000);
    assert(request.channelId == controlKey().channelId);
    assert(request.channelType == controlKey().channelType);
    assert(request.packetType == protocol::PacketType::Control);
    assert(request.messageKind == protocol::MessageKind::Request);
    assert(request.sequence == 10);
    assert(request.monotonicTimestampUsec == 100000);
    assert((request.flags & protocol::PacketFlagResponseRequired) != 0);

    const runtime::connection::ReconnectTeardownWireDecodeResult decoded =
        runtime::connection::decodeReconnectTeardownCommandPacket(request);
    assert(decoded.ok);
    assert(decoded.command.targetChannel == screenKey());
    assert(decoded.command.reason == "dispatch after rebind");
}

void ackDoesNotCompleteButTerminalResponseDoes()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));
    network::RequestTracker tracker(4000);
    runtime::connection::ReconnectTeardownDispatcher dispatcher(router, tracker);

    const runtime::connection::ReconnectTeardownDispatchResult result =
        dispatcher.dispatch(makeZeroIdPlan());
    assert(result.ok);
    assert(control->sentPackets.size() == 1);
    const protocol::PacketEnvelope request = control->sentPackets.front();

    runtime::connection::ReconnectTeardownWireResponseOptions ackOptions;
    ackOptions.messageId = 5000;
    ackOptions.status = protocol::ResponseStatus::Accepted;
    assert(dispatcher.complete(
        runtime::connection::makeReconnectTeardownResponsePacket(request, ackOptions)));
    assert(tracker.pendingCount() == 1);
    assert(dispatcher.interimResponses().size() == 1);
    assert(dispatcher.terminalResponses().empty());
    assert(!dispatcher.summary().complete);
    assert(!dispatcher.summary().ok);

    runtime::connection::ReconnectTeardownWireResponseOptions responseOptions;
    responseOptions.messageId = 5001;
    responseOptions.status = protocol::ResponseStatus::Ok;
    responseOptions.message = "closed";
    assert(dispatcher.complete(
        runtime::connection::makeReconnectTeardownResponsePacket(request, responseOptions)));
    assert(tracker.pendingCount() == 0);
    assert(dispatcher.terminalResponses().size() == 1);

    const runtime::connection::ReconnectTeardownResponseSummary summary =
        dispatcher.summary();
    assert(summary.complete);
    assert(summary.ok);
    assert(summary.completedTargetChannels.size() == 1);
    assert(summary.completedTargetChannels.front() == screenKey());
}

void sendFailureBecomesTerminalError()
{
    network::NetworkRouter router;
    network::RequestTracker tracker(6000);
    runtime::connection::ReconnectTeardownDispatcher dispatcher(router, tracker);

    const runtime::connection::ReconnectTeardownDispatchResult result =
        dispatcher.dispatch(makeZeroIdPlan());

    assert(!result.ok);
    assert(result.sentRequests.empty());
    assert(!result.messages.empty());
    assert(tracker.pendingCount() == 0);
    assert(dispatcher.terminalResponses().size() == 1);

    const runtime::connection::ReconnectTeardownResponseSummary summary =
        dispatcher.summary();
    assert(summary.complete);
    assert(!summary.ok);
    assert(!summary.messages.empty());
}

} // namespace

int main()
{
    dispatchAssignsMessageIdTracksAndSends();
    ackDoesNotCompleteButTerminalResponseDoes();
    sendFailureBecomesTerminalError();
    return 0;
}
