#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/module/module_factory.h"
#include "fusiondesk/modules/clipboard/clipboard_factory.h"
#include "fusiondesk/modules/clipboard/fdcl_codec.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

class FakeRouter : public network::INetworkRouter
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

class FakeEndpoint : public IClipboardEndpoint
{
public:
    ClipboardSnapshot snapshot() override
    {
        ClipboardSnapshot result;
        result.ownerEpoch = bundle.ownerEpoch;
        result.sequence = bundle.sequence;
        result.bundle = bundle;
        return result;
    }

    protocol::ResponseStatus publishBundle(const ClipboardPublishRequest& request) override
    {
        bundle = request.bundle;
        ++published;
        return publishStatus;
    }

    protocol::ResponseStatus clearPublishedBundle(TransferOfferId offerId) override
    {
        if (bundle.offerId == offerId)
            bundle = {};
        ++cleared;
        return protocol::ResponseStatus::Ok;
    }

    TransferSourceBundle bundle;
    protocol::ResponseStatus publishStatus = protocol::ResponseStatus::Ok;
    int published = 0;
    int cleared = 0;
};

class CallbackEndpoint final : public IClipboardEndpoint
{
public:
    ClipboardSnapshot snapshot() override
    {
        return {};
    }

    protocol::ResponseStatus publishBundle(
        const ClipboardPublishRequest& request) override
    {
        ++published;
        bundle = request.bundle;
        if (onPublish)
            return onPublish(request);
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus clearPublishedBundle(TransferOfferId) override
    {
        return protocol::ResponseStatus::Ok;
    }

    std::function<protocol::ResponseStatus(const ClipboardPublishRequest&)>
        onPublish;
    TransferSourceBundle bundle;
    int published = 0;
};

protocol::ByteBuffer bytes(const std::string& value);

class FakeDragSink final : public IRemoteDragCoordinateSink
{
public:
    protocol::ResponseStatus dragStart(const DragSessionStart& start) override
    {
        lastStart = start;
        ++starts;
        return status;
    }

    protocol::ResponseStatus dragMove(DragSessionId dragSessionId,
                                      const DragSurfaceCoordinate& point,
                                      TransferAction proposedAction) override
    {
        lastDragSessionId = dragSessionId;
        lastPoint = point;
        lastAction = proposedAction;
        ++moves;
        return status;
    }

    protocol::ResponseStatus dragDrop(DragSessionId dragSessionId,
                                      const DragSurfaceCoordinate& point,
                                      TransferAction proposedAction) override
    {
        lastDragSessionId = dragSessionId;
        lastPoint = point;
        lastAction = proposedAction;
        ++drops;
        return status;
    }

    protocol::ResponseStatus dragCancel(DragSessionId dragSessionId,
                                        DragCancelReason reason) override
    {
        lastDragSessionId = dragSessionId;
        lastCancelReason = reason;
        ++cancels;
        return status;
    }

    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
    DragSessionStart lastStart;
    DragSurfaceCoordinate lastPoint;
    DragSessionId lastDragSessionId = 0;
    DragCancelReason lastCancelReason = DragCancelReason::Unknown;
    TransferAction lastAction = TransferAction::None;
    int starts = 0;
    int moves = 0;
    int drops = 0;
    int cancels = 0;
};

class NativeToCanonicalTextTranscoder final : public ITransferTranscoder
{
public:
    bool canTranscode(const TransferTranscodeRequest& request) const override
    {
        return request.canonicalFormat == TextPlainUtf8Format &&
               request.sourceEncoding == TransferEncodingMode::NativePassthrough &&
               request.targetEncoding == TransferEncodingMode::CanonicalBytes;
    }

    TransferTranscodeResult transcode(
        const TransferTranscodeRequest& request) const override
    {
        ++calls;
        lastRequest = request;
        TransferTranscodeResult result;
        if (!canTranscode(request)) {
            result.status = protocol::ResponseStatus::Unsupported;
            result.message = "unsupported test transcode";
            return result;
        }

        result.status = protocol::ResponseStatus::Ok;
        result.encoding = TransferEncodingMode::CanonicalBytes;
        result.bytes = bytes("canonical:");
        result.bytes.insert(result.bytes.end(),
                            request.bytes.begin(),
                            request.bytes.end());
        return result;
    }

    mutable int calls = 0;
    mutable TransferTranscodeRequest lastRequest;
};

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

TransferSourceBundle textBundle(protocol::SessionId originSessionId = 100)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 44;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 5;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::NativePassthrough;

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
    bundle.policyVersion = 66;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(77, std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

TransferSourceBundle presentedTextBundle(protocol::SessionId originSessionId = 100)
{
    TransferSourceBundle bundle = textBundle(originSessionId);
    TransferPresentation presentation;
    presentation.displayName = "document.txt";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    presentation.allowedActions = transfer_action::Copy;
    presentation.preferredAction = TransferAction::Copy;
    presentation.previewAllowedByPolicy = true;
    bundle.presentation = presentation;
    return bundle;
}

FdclReadFormatRequest textReadRequest()
{
    FdclReadFormatRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.itemIndex = 0;
    request.formatId = 55;
    request.localFormatToken = 44;
    request.acceptedMaxBytes = 1024;
    request.streamAccepted = false;
    request.requestedEncoding = TransferEncodingMode::NativePassthrough;
    request.canonicalFormat = TextPlainUtf8Format;
    return request;
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

FdclDragStart dragStartRequest()
{
    FdclDragStart start;
    start.start.dragSessionId = 7001;
    start.start.bundleId = 11;
    start.start.offerId = 22;
    start.start.ownerEpoch = 33;
    start.start.allowedActions = transfer_action::Copy;
    start.start.preferredAction = TransferAction::Copy;
    start.start.start = dragPoint();
    return start;
}

protocol::PacketEnvelope clipboardEvent(protocol::ByteBuffer payload)
{
    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Event;
    packet.payload = std::move(payload);
    return packet;
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

void formatListPublishesRemoteBundleAndLazyReadRoundTrips()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto clientEndpoint = std::make_shared<FakeEndpoint>();
    auto agentEndpoint = std::make_shared<FakeEndpoint>();
    ClipboardClientModule client(ClipboardModuleDependencies{clientEndpoint, {}});
    ClipboardAgentModule agent(ClipboardModuleDependencies{agentEndpoint, {}});
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    assert(clientRouter.sent.size() == 1);

    agent.handlePacket(clientRouter.sent.back());
    ClipboardModuleSnapshot agentSnapshot = agent.snapshot();
    assert(agentSnapshot.formatListsReceived == 1);
    assert(agentEndpoint->published == 1);
    assert(agentEndpoint->bundle.offerId == 22);
    assert(agentEndpoint->bundle.sources.size() == 1);

    assert(agent.requestRemoteFormat(textReadRequest(), 1000, 1000000));
    assert(agentRouter.sent.size() == 1);
    client.handlePacket(agentRouter.sent.back());
    assert(clientRouter.sent.size() == 2);
    agent.handlePacket(clientRouter.sent.back());

    agentSnapshot = agent.snapshot();
    assert(agentSnapshot.readRequestsSent == 1);
    assert(agentSnapshot.inlineResponsesReceived == 1);
    assert(agentSnapshot.lastReadResult.has_value());
    assert(agentSnapshot.lastReadResult->status == protocol::ResponseStatus::Ok);
    assert(agentSnapshot.lastReadResult->encoding == TransferEncodingMode::NativePassthrough);
    assert(agentSnapshot.lastReadResult->bytes == bytes("hello"));
}

void sendPolicyDenialReturnsError()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardPolicy deniedPolicy;
    deniedPolicy.allowSendContent = false;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, deniedPolicy});
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    agent.requestRemoteFormat(textReadRequest(), 1000, 1000000);
    client.handlePacket(agentRouter.sent.back());
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status == protocol::ResponseStatus::DeniedByPolicy);
}

void staleOfferReturnsConflict()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardClientModule client;
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    FdclReadFormatRequest stale = textReadRequest();
    stale.ownerEpoch = 34;
    agent.requestRemoteFormat(stale, 1000, 1000000);
    client.handlePacket(agentRouter.sent.back());
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status == protocol::ResponseStatus::Conflict);
}

void tooLargeReturnsTooLarge()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardClientModule client;
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    FdclReadFormatRequest request = textReadRequest();
    request.acceptedMaxBytes = 1;
    agent.requestRemoteFormat(request, 1000, 1000000);
    client.handlePacket(agentRouter.sent.back());
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status == protocol::ResponseStatus::TooLarge);
}

void acceptedMaxBytesAbovePolicyIsClamped()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardPolicy policy;
    policy.maxInlineBytes = 6;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    FdclReadFormatRequest request = textReadRequest();
    request.acceptedMaxBytes = 100;
    agent.requestRemoteFormat(request, 1000, 1000000);
    client.handlePacket(agentRouter.sent.back());
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status == protocol::ResponseStatus::Ok);
    assert(snapshot.lastReadResult->bytes == bytes("hello"));
}

void nativeReadCanTranscodeToRequestedCanonicalEncoding()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto transcoder = std::make_shared<NativeToCanonicalTextTranscoder>();

    ClipboardModuleDependencies clientDependencies;
    clientDependencies.transcoder = transcoder;
    ClipboardClientModule client(clientDependencies);
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    FdclReadFormatRequest request = textReadRequest();
    request.requestedEncoding = TransferEncodingMode::CanonicalBytes;
    assert(agent.requestRemoteFormat(request, 1000, 1000000));
    client.handlePacket(agentRouter.sent.back());
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(transcoder->calls == 1);
    assert(transcoder->lastRequest.sourceEncoding ==
           TransferEncodingMode::NativePassthrough);
    assert(transcoder->lastRequest.targetEncoding ==
           TransferEncodingMode::CanonicalBytes);
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status == protocol::ResponseStatus::Ok);
    assert(snapshot.lastReadResult->encoding ==
           TransferEncodingMode::CanonicalBytes);
    assert(snapshot.lastReadResult->bytes == bytes("canonical:hello"));
}

void receivePolicyCanStripPresentation()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto endpoint = std::make_shared<FakeEndpoint>();
    ClipboardPolicy receivePolicy;
    receivePolicy.allowPresentationMetadata = false;
    ClipboardClientModule client;
    ClipboardAgentModule agent(ClipboardModuleDependencies{endpoint, receivePolicy});
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(presentedTextBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.formatListsReceived == 1);
    assert(endpoint->published == 1);
    assert(!endpoint->bundle.presentation.has_value());
    assert(!snapshot.remoteBundle.presentation.has_value());
}

void remoteBundleIsInstalledBeforeEndpointPublish()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardClientModule client;
    auto endpoint = std::make_shared<CallbackEndpoint>();
    ClipboardAgentModule* agentPtr = nullptr;
    ClipboardRemoteReadDispatchResult dispatchResult;

    endpoint->onPublish = [&](const ClipboardPublishRequest& request) {
        assert(agentPtr != nullptr);
        assert(request.bundle.offerId == 22);
        assert(!request.bundle.sources.empty());

        const std::vector<TransferFormatDescriptor> formats =
            request.bundle.sources.front()->formats();
        assert(!formats.empty());

        FdclReadFormatRequest read;
        read.bundleId = request.bundle.bundleId;
        read.offerId = request.bundle.offerId;
        read.ownerEpoch = request.bundle.ownerEpoch;
        read.sourceId = request.bundle.sources.front()->id();
        read.itemIndex = formats.front().itemIndex;
        read.formatId = formats.front().formatId;
        read.localFormatToken = formats.front().localFormatToken;
        read.acceptedMaxBytes = 1024;
        read.streamAccepted = false;
        read.requestedEncoding = TransferEncodingMode::NativePassthrough;
        read.canonicalFormat = TextPlainUtf8Format;

        dispatchResult =
            agentPtr->requestRemoteFormatTracked(read, 1000, 1000000);
        return dispatchResult.dispatched ? protocol::ResponseStatus::Ok
                                         : dispatchResult.status;
    };

    ClipboardAgentModule agent(ClipboardModuleDependencies{endpoint, {}});
    agentPtr = &agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(endpoint->published == 1);
    assert(dispatchResult.dispatched);
    assert(agentRouter.sent.size() == 1);
    assert(snapshot.formatListsReceived == 1);
    assert(snapshot.remoteBundle.offerId == 22);
    assert(snapshot.readRequestsSent == 1);
}

void dragPolicyDoesNotBlockClipboardFormatLists()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto endpoint = std::make_shared<FakeEndpoint>();
    ClipboardPolicy policy;
    policy.allowDrag = false;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    ClipboardAgentModule agent(ClipboardModuleDependencies{endpoint, policy});
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    assert(clientRouter.sent.size() == 1);
    assert(client.snapshot().policyDenials == 0);

    agent.handlePacket(clientRouter.sent.back());
    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.formatListsReceived == 1);
    assert(snapshot.policyDenials == 0);
    assert(endpoint->published == 1);
}

void pendingReadCanTimeout()
{
    FakeRouter agentRouter;
    ClipboardAgentModule agent;
    start(agent, runtime(200, agentRouter));

    assert(agent.requestRemoteFormat(textReadRequest(), 10, 1000000));
    assert(agent.snapshot().pendingReads == 1);
    assert(agent.expirePendingReads(1010001) == 1);

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.pendingReads == 0);
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status == protocol::ResponseStatus::Timeout);
}

void incomingCancelCompletesPendingRead()
{
    FakeRouter agentRouter;
    ClipboardAgentModule agent;
    start(agent, runtime(200, agentRouter));

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFormatTracked(textReadRequest(), 1000, 1000000);
    assert(dispatched.dispatched);
    assert(agent.snapshot().pendingReads == 1);

    FdclCancel cancel;
    cancel.correlationId = dispatched.messageId;
    cancel.bundleId = 11;
    cancel.offerId = 22;
    cancel.ownerEpoch = 33;
    cancel.sourceId = 77;
    cancel.formatId = 55;
    cancel.reason = FdclCancelReason::UserCancelled;
    cancel.message = "user stopped paste";

    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Cancel;
    packet.payload = encodeFdclCancel(cancel);
    agent.handlePacket(packet);

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.pendingReads == 0);
    assert(snapshot.cancelsReceived == 1);
    assert(snapshot.cancelMisses == 0);
    assert(snapshot.lastReadResult.has_value());
    assert(snapshot.lastReadResult->status ==
           protocol::ResponseStatus::Cancelled);
    assert(snapshot.lastReadResult->message == "user stopped paste");
}

void sendCancelEmitsFdclCancelPacket()
{
    FakeRouter agentRouter;
    ClipboardAgentModule agent;
    start(agent, runtime(200, agentRouter));

    FdclCancel cancel;
    cancel.correlationId = 1234;
    cancel.bundleId = 11;
    cancel.offerId = 22;
    cancel.ownerEpoch = 33;
    cancel.sourceId = 77;
    cancel.formatId = 55;
    cancel.reason = FdclCancelReason::Timeout;
    cancel.message = "read deadline exceeded";

    assert(agent.sendCancel(cancel, 777, 1000000));
    assert(agentRouter.sent.size() == 1);
    const protocol::PacketEnvelope& packet = agentRouter.sent.back();
    assert(packet.messageKind == protocol::MessageKind::Cancel);
    assert(packet.flags == protocol::PacketFlagNoResponseRequired);
    assert(packet.messageId != 0);
    assert(packet.correlationId == 1234);
    assert(packet.timeoutMs == 777);

    const FdclDecodeResult decoded = decodeFdclPayload(packet.payload);
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::Cancel);
    assert(decoded.cancel.correlationId == 1234);
    assert(decoded.cancel.reason == FdclCancelReason::Timeout);
    assert(decoded.cancel.message == "read deadline exceeded");
    assert(agent.snapshot().cancelsSent == 1);
}

void ownerLoopIsSuppressed()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto endpoint = std::make_shared<FakeEndpoint>();
    ClipboardClientModule client;
    ClipboardAgentModule agent(ClipboardModuleDependencies{endpoint, {}});
    start(client, runtime(100, clientRouter));
    start(agent, runtime(100, agentRouter));

    assert(client.announceLocalBundle(textBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.loopSuppressions == 1);
    assert(endpoint->published == 0);
}

void dragPolicyCanDenyCoordinateEvents()
{
    FakeRouter clientRouter;
    ClipboardPolicy policy;
    policy.allowDrag = false;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    start(client, runtime(100, clientRouter));

    assert(!client.sendRemoteDragStart(dragStartRequest()));
    assert(clientRouter.sent.empty());
    assert(client.snapshot().policyDenials == 1);

    FakeRouter agentRouter;
    auto sink = std::make_shared<FakeDragSink>();
    ClipboardModuleDependencies dependencies;
    dependencies.dragSink = sink;
    dependencies.policy = policy;
    ClipboardAgentModule agent(dependencies);
    start(agent, runtime(200, agentRouter));
    agent.handlePacket(clipboardEvent(encodeFdclDragStart(dragStartRequest())));

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.dragStartsReceived == 1);
    assert(snapshot.policyDenials == 1);
    assert(sink->starts == 0);
}

void factoryResolvesAliasByRole()
{
    module::ModuleCreateOptions clientOptions;
    clientOptions.role = session::SessionRole::Client;
    module::ModuleCreateOptions agentOptions;
    agentOptions.role = session::SessionRole::Agent;

    ClipboardModuleFactory factory;
    assert(factory.supports("clipboard.redirect", clientOptions));
    assert(factory.supports("clipboard.redirect", agentOptions));
    assert(factory.manifest(clientOptions).moduleId == "clipboard.redirect.client");
    assert(factory.manifest(agentOptions).moduleId == "clipboard.redirect.agent");
    assert(factory.create(clientOptions)->manifest().moduleId == "clipboard.redirect.client");
    assert(factory.create(agentOptions)->manifest().moduleId == "clipboard.redirect.agent");
}

} // namespace

int main()
{
    formatListPublishesRemoteBundleAndLazyReadRoundTrips();
    sendPolicyDenialReturnsError();
    staleOfferReturnsConflict();
    tooLargeReturnsTooLarge();
    acceptedMaxBytesAbovePolicyIsClamped();
    nativeReadCanTranscodeToRequestedCanonicalEncoding();
    receivePolicyCanStripPresentation();
    remoteBundleIsInstalledBeforeEndpointPublish();
    dragPolicyDoesNotBlockClipboardFormatLists();
    pendingReadCanTimeout();
    incomingCancelCompletesPendingRead();
    sendCancelEmitsFdclCancelPacket();
    ownerLoopIsSuppressed();
    dragPolicyCanDenyCoordinateEvents();
    factoryResolvesAliasByRole();
    return 0;
}
