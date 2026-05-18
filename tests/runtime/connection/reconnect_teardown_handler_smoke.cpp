#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/runtime/connection/reconnect_teardown_handler.h"

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
        return true;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    network::ChannelKey key_;
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

protocol::PacketEnvelope makeTeardownRequest(protocol::MessageId messageId = 100)
{
    runtime::connection::ReconnectTeardownCommand command;
    command.sessionId = 9101;
    command.targetChannel = screenKey();
    command.messageId = messageId;
    command.correlationId = messageId;
    command.timeoutMs = 2000;
    command.reason = "handler close";
    return runtime::connection::makeReconnectTeardownRequestPacket(command);
}

void handlerClosesTargetAndRespondsOk()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownHandler handler(router, closeTarget);
    runtime::connection::ReconnectTeardownHandlerOptions options;
    options.controlChannel = screenKey();
    options.firstResponseMessageId = 7000;
    options.sequence = 3;
    options.monotonicTimestampUsec = 123456;
    const runtime::connection::ReconnectTeardownHandlerStartResult started =
        handler.start(options);
    assert(started.ok);

    router.submitIncoming(makeTeardownRequest(101));

    assert(closeTarget.requests.size() == 1);
    assert(closeTarget.requests.front().sessionId == 9101);
    assert(closeTarget.requests.front().targetChannel == screenKey());
    assert(closeTarget.requests.front().reason == "handler close");
    assert(control->sentPackets.size() == 1);

    const protocol::PacketEnvelope& response = control->sentPackets.front();
    assert(response.messageId == 7000);
    assert(response.responseTo == 101);
    assert(response.correlationId == 101);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);
    assert(response.sequence == 3);
    assert(response.monotonicTimestampUsec == 123456);

    const runtime::connection::ReconnectTeardownHandlerSnapshot snapshot =
        handler.snapshot();
    assert(snapshot.active);
    assert(snapshot.handledRequests == 1);
    assert(snapshot.malformedRequests == 0);
}

void handlerReportsCloseFailure()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    FakeCloseTarget closeTarget;
    closeTarget.result = runtime::connection::ReconnectTeardownCloseResult::failed(
        protocol::ResponseStatus::ChannelUnavailable,
        "old transport missing");
    runtime::connection::ReconnectTeardownHandler handler(router, closeTarget);
    assert(handler.start().ok);

    router.submitIncoming(makeTeardownRequest(102));

    assert(closeTarget.requests.size() == 1);
    assert(control->sentPackets.size() == 1);
    const protocol::PacketEnvelope& response = control->sentPackets.front();
    assert(response.responseTo == 102);
    assert(response.messageKind == protocol::MessageKind::Error);
    assert(response.responseStatus == protocol::ResponseStatus::ChannelUnavailable);
}

void handlerRejectsMalformedFdrtPayload()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownHandler handler(router, closeTarget);
    assert(handler.start().ok);

    protocol::PacketEnvelope request = makeTeardownRequest(103);
    request.payload = {static_cast<std::uint8_t>('F'),
                       static_cast<std::uint8_t>('D'),
                       static_cast<std::uint8_t>('R'),
                       static_cast<std::uint8_t>('T')};
    router.submitIncoming(request);

    assert(closeTarget.requests.empty());
    assert(control->sentPackets.size() == 1);
    const protocol::PacketEnvelope& response = control->sentPackets.front();
    assert(response.responseTo == 103);
    assert(response.messageKind == protocol::MessageKind::Error);
    assert(response.responseStatus == protocol::ResponseStatus::ProtocolError);
    assert(handler.snapshot().malformedRequests == 1);
}

void handlerIgnoresOtherControlPayloadsAndStops()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownHandler handler(router, closeTarget);
    assert(handler.start().ok);

    protocol::PacketEnvelope request = makeTeardownRequest(104);
    request.payload = {0x01, 0x02, 0x03};
    router.submitIncoming(request);

    assert(closeTarget.requests.empty());
    assert(control->sentPackets.empty());
    assert(handler.snapshot().ignoredPackets == 1);

    handler.stop();
    assert(!handler.snapshot().active);
    router.submitIncoming(makeTeardownRequest(105));
    assert(closeTarget.requests.empty());
    assert(control->sentPackets.empty());
}

} // namespace

int main()
{
    handlerClosesTargetAndRespondsOk();
    handlerReportsCloseFailure();
    handlerRejectsMalformedFdrtPayload();
    handlerIgnoresOtherControlPayloadsAndStops();
    return 0;
}
