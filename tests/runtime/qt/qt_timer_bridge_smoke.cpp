#include <cassert>
#include <functional>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_service.h"
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"

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
        const runtime::connection::ReconnectTeardownCloseRequest&) override
    {
        return runtime::connection::ReconnectTeardownCloseResult::closed("closed");
    }
};

protocol::PacketEnvelope makeRequest(protocol::MessageId messageId)
{
    protocol::PacketEnvelope request;
    request.sessionId = 7;
    request.traceId = 9;
    request.messageId = messageId;
    request.correlationId = messageId;
    request.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    request.channelType = protocol::ChannelType::Video;
    request.packetType = protocol::PacketType::PayloadAck;
    request.messageKind = protocol::MessageKind::Request;
    request.priority = protocol::PacketPriority::Interactive;
    request.monotonicTimestampUsec = runtime::qt::QtTimerBridge::monotonicNowUsec();
    request.timeoutMs = 10;
    return request;
}

bool waitUntil(const std::function<bool()>& done, int timeoutMs = 1000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

void timerTicksAndExpiresRequestTracker()
{
    network::RequestTracker tracker;
    std::vector<protocol::PacketEnvelope> callbacks;
    const protocol::MessageId messageId = tracker.nextMessageId();
    const protocol::PacketEnvelope request = makeRequest(messageId);
    const network::TrackResult tracked = tracker.track(
        request,
        [&callbacks](const protocol::PacketEnvelope& response) {
            callbacks.push_back(response);
        });
    assert(tracked.tracked);

    runtime::qt::QtTimerBridge bridge([&tracker](std::uint64_t nowUsec) {
        tracker.expire(nowUsec);
    });
    assert(!bridge.start(0));
    assert(bridge.start(1));
    assert(bridge.running());
    assert(bridge.intervalMs() == 1);

    assert(waitUntil([&callbacks]() { return callbacks.size() == 1; }));
    bridge.stop();
    assert(!bridge.running());
    assert(tracker.pendingCount() == 0);
    assert(callbacks.front().messageKind == protocol::MessageKind::Error);
    assert(callbacks.front().responseStatus == protocol::ResponseStatus::Timeout);
    assert(callbacks.front().responseTo == messageId);
}

void timerTicksAndExpiresReconnectTeardownService()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    network::RequestTracker tracker(3000);
    FakeCloseTarget closeTarget;
    runtime::connection::ReconnectTeardownService service(router, tracker, closeTarget);
    assert(service.start().ok);

    runtime::connection::ReconnectOrchestrationSidePlan sidePlan;
    sidePlan.sessionId = 71;
    sidePlan.teardownAfterSuccessfulRebind = {screenKey()};
    sidePlan.reason = "qt timer service timeout";

    runtime::connection::ReconnectTeardownDispatchOptions options;
    options.wire.monotonicTimestampUsec = runtime::qt::QtTimerBridge::monotonicNowUsec();
    const runtime::connection::ReconnectTeardownDispatchResult dispatched =
        service.dispatch(sidePlan, 10, options);
    assert(dispatched.ok);
    assert(control->sentPackets.size() == 1);
    assert(service.snapshot().pendingRequests == 1);

    runtime::qt::QtTimerBridge bridge([&service](std::uint64_t nowUsec) {
        service.expire(nowUsec);
    });
    assert(bridge.start(1));
    assert(waitUntil([&service]() {
        const runtime::connection::ReconnectTeardownServiceSnapshot snapshot =
            service.snapshot();
        return snapshot.pendingRequests == 0 && snapshot.terminalResponses == 1;
    }));
    bridge.stop();

    const runtime::connection::ReconnectTeardownServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.summary.complete);
    assert(!snapshot.summary.ok);
}

void handlerCanBeInstalledAfterConstruction()
{
    int ticks = 0;
    runtime::qt::QtTimerBridge bridge;
    bridge.setTickHandler([&ticks](std::uint64_t) {
        ++ticks;
    });
    assert(bridge.start(1));
    assert(waitUntil([&ticks]() { return ticks > 0; }));
    bridge.stop();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    timerTicksAndExpiresRequestTracker();
    timerTicksAndExpiresReconnectTeardownService();
    handlerCanBeInstalledAfterConstruction();
    return 0;
}
