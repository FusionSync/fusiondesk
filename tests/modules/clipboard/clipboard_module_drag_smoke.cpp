#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_modules.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

class FakeRouter final : public network::INetworkRouter
{
public:
    bool registerChannel(std::shared_ptr<network::IChannel>) override
    {
        return true;
    }

    void unregisterChannel(protocol::ChannelId, protocol::ChannelType) override
    {
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sent.push_back(packet);
        return network::SendResult::sent();
    }

    network::SubscriptionToken subscribe(const network::RouteMatch&,
                                         network::PacketHandler) override
    {
        return 1;
    }

    void unsubscribe(network::SubscriptionToken) override
    {
    }

    void submitIncoming(const protocol::PacketEnvelope&) override
    {
    }

    std::vector<protocol::PacketEnvelope> sent;
};

class FakeDragSink final : public IRemoteDragCoordinateSink
{
public:
    protocol::ResponseStatus dragStart(const DragSessionStart& start) override
    {
        lastStart = start;
        ++starts;
        return startStatus;
    }

    protocol::ResponseStatus dragMove(DragSessionId dragSessionId,
                                      const DragSurfaceCoordinate& point,
                                      TransferAction proposedAction) override
    {
        lastDragSessionId = dragSessionId;
        lastPoint = point;
        lastAction = proposedAction;
        ++moves;
        return moveStatus;
    }

    protocol::ResponseStatus dragDrop(DragSessionId dragSessionId,
                                      const DragSurfaceCoordinate& point,
                                      TransferAction proposedAction) override
    {
        lastDragSessionId = dragSessionId;
        lastPoint = point;
        lastAction = proposedAction;
        ++drops;
        return dropStatus;
    }

    protocol::ResponseStatus dragCancel(DragSessionId dragSessionId,
                                        DragCancelReason reason) override
    {
        lastDragSessionId = dragSessionId;
        lastCancelReason = reason;
        ++cancels;
        return cancelStatus;
    }

    protocol::ResponseStatus startStatus = protocol::ResponseStatus::Ok;
    protocol::ResponseStatus moveStatus = protocol::ResponseStatus::Ok;
    protocol::ResponseStatus dropStatus = protocol::ResponseStatus::Ok;
    protocol::ResponseStatus cancelStatus = protocol::ResponseStatus::Ok;
    DragSessionStart lastStart;
    DragSurfaceCoordinate lastPoint;
    TransferAction lastAction = TransferAction::None;
    DragCancelReason lastCancelReason = DragCancelReason::Unknown;
    DragSessionId lastDragSessionId = 0;
    int starts = 0;
    int moves = 0;
    int drops = 0;
    int cancels = 0;
};

TransferSourceBundle textBundle(protocol::SessionId originSessionId = 100)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 16;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("hello");

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = originSessionId;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            77,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

DragSurfaceCoordinate dragPoint(std::int32_t x = 10, std::int32_t y = 20)
{
    DragSurfaceCoordinate point;
    point.coordinateSpace = DragCoordinateSpace::RemoteLogical;
    point.x = x;
    point.y = y;
    point.surfaceWidth = 1920;
    point.surfaceHeight = 1080;
    point.scale = 1.0;
    return point;
}

FdclDragStart dragStartRequest(DragSessionId id = 7001)
{
    FdclDragStart start;
    start.start.dragSessionId = id;
    start.start.bundleId = 11;
    start.start.offerId = 22;
    start.start.ownerEpoch = 33;
    start.start.allowedActions = transfer_action::Copy;
    start.start.preferredAction = TransferAction::Copy;
    start.start.start = dragPoint();
    return start;
}

module::ModuleRuntime runtime(protocol::SessionId sessionId, FakeRouter& router)
{
    module::ModuleRuntime value;
    value.session.sessionId = sessionId;
    value.session.traceId = sessionId + 1000;
    value.session.localPlatform = "windows";
    value.network = &router;
    return value;
}

void start(module::IModule& module, const module::ModuleRuntime& runtime)
{
    assert(module.attach(runtime));
    assert(module.start({}));
    assert(module.state() == module::ModuleState::Running);
}

void dragDropTerminatesLocalAndRemoteSessions()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto sink = std::make_shared<FakeDragSink>();
    ClipboardModuleDependencies agentDependencies;
    agentDependencies.dragSink = sink;

    ClipboardClientModule client;
    ClipboardAgentModule agent(agentDependencies);
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    assert(client.sendRemoteDragStart(dragStartRequest()));
    agent.handlePacket(clientRouter.sent.back());

    FdclDragMove move;
    move.dragSessionId = 7001;
    move.point = dragPoint(30, 40);
    move.proposedAction = TransferAction::Copy;
    assert(client.sendRemoteDragMove(move));
    agent.handlePacket(clientRouter.sent.back());

    FdclDragDrop drop;
    drop.dragSessionId = 7001;
    drop.point = dragPoint(50, 60);
    drop.proposedAction = TransferAction::Copy;
    assert(client.sendRemoteDragDrop(drop));
    agent.handlePacket(clientRouter.sent.back());

    assert(sink->starts == 1);
    assert(sink->moves == 1);
    assert(sink->drops == 1);
    assert(client.snapshot().activeLocalDragSessionId == 0);
    assert(agent.snapshot().activeRemoteDragSessionId == 0);
    assert(!client.sendRemoteDragMove(move));
}

void dragCancelTerminatesLocalAndRemoteSessions()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto sink = std::make_shared<FakeDragSink>();
    ClipboardModuleDependencies agentDependencies;
    agentDependencies.dragSink = sink;

    ClipboardClientModule client;
    ClipboardAgentModule agent(agentDependencies);
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    assert(client.sendRemoteDragStart(dragStartRequest(7002)));
    agent.handlePacket(clientRouter.sent.back());

    FdclDragCancel cancel;
    cancel.dragSessionId = 7002;
    cancel.reason = DragCancelReason::UserCancelled;
    assert(client.sendRemoteDragCancel(cancel));
    agent.handlePacket(clientRouter.sent.back());

    assert(sink->starts == 1);
    assert(sink->cancels == 1);
    assert(sink->lastCancelReason == DragCancelReason::UserCancelled);
    assert(client.snapshot().activeLocalDragSessionId == 0);
    assert(agent.snapshot().activeRemoteDragSessionId == 0);
}

void dragCoordinatesRequireStartedSession()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto sink = std::make_shared<FakeDragSink>();
    ClipboardModuleDependencies agentDependencies;
    agentDependencies.dragSink = sink;

    ClipboardClientModule client;
    ClipboardAgentModule agent(agentDependencies);
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    FdclDragMove move;
    move.dragSessionId = 7001;
    move.point = dragPoint(30, 40);
    move.proposedAction = TransferAction::Copy;
    assert(!client.sendRemoteDragMove(move));
    assert(client.snapshot().staleOfferFailures == 1);

    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Event;
    packet.payload = encodeFdclDragMove(move);
    agent.handlePacket(packet);

    assert(agent.snapshot().staleOfferFailures == 1);
    assert(sink->moves == 0);
}

} // namespace

int main()
{
    dragDropTerminatesLocalAndRemoteSessions();
    dragCancelTerminatesLocalAndRemoteSessions();
    dragCoordinatesRequireStartedSession();
    return 0;
}
