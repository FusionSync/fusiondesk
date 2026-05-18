#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

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

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
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

TransferSourceBundle fileObjectBundle(protocol::SessionId originSessionId = 100)
{
    TransferFileList files;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "report.txt";
    file.sizeBytes = 11;
    files.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.localFormatToken = 500;
    descriptor.formatId = 501;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 111;
    bundle.offerId = 222;
    bundle.ownerEpoch = 333;
    bundle.sequence = 444;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = originSessionId;
    bundle.sources.push_back(
        std::make_shared<FileGroupTransferSource>(888, descriptor, files));
    return bundle;
}

FdclObjectLock objectLockRequest()
{
    FdclObjectLock request;
    request.bundleId = 111;
    request.offerId = 222;
    request.ownerEpoch = 333;
    request.sourceId = 888;
    request.objectId = 9001;
    request.fileIndex = 0;
    request.leaseUsec = 30000000;
    return request;
}

protocol::PacketEnvelope objectLockPacket(const FdclObjectLock& request)
{
    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Request;
    packet.messageId = 7001;
    packet.correlationId = 7001;
    packet.timeoutMs = 1000;
    packet.payload = encodeFdclLockObject(request);
    return packet;
}

void objectLockAndUnlockRoundTripThroughModule()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto clientRegistry =
        std::make_shared<InMemoryTransferSourceRegistry>();

    ClipboardModuleDependencies clientDeps;
    clientDeps.sourceRegistry = clientRegistry;
    ClipboardClientModule client(clientDeps);
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileObjectBundle(100)));
    assert(clientRouter.sent.size() == 1);
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult lockDispatch =
        agent.requestRemoteObjectLockTracked(objectLockRequest(),
                                             1000,
                                             1000000);
    assert(lockDispatch.dispatched);
    assert(lockDispatch.messageId != 0);
    assert(agentRouter.sent.size() == 1);

    client.handlePacket(agentRouter.sent.back());
    assert(clientRegistry->lockCount() == 1);
    assert(clientRouter.sent.size() == 2);
    ClipboardModuleSnapshot clientSnapshot = client.snapshot();
    assert(clientSnapshot.objectLockRequestsReceived == 1);
    assert(clientSnapshot.objectLockResponsesSent == 1);

    agent.handlePacket(clientRouter.sent.back());
    ClipboardModuleSnapshot agentSnapshot = agent.snapshot();
    assert(agentSnapshot.objectLockRequestsSent == 1);
    assert(agentSnapshot.objectLockResponsesReceived == 1);
    assert(agentSnapshot.lastObjectLockResponseTo == lockDispatch.messageId);
    assert(agentSnapshot.lastObjectLockResult.has_value());
    assert(agentSnapshot.lastObjectLockResult->ok());
    assert(agentSnapshot.lastObjectLockResult->lockId != 0);

    FdclObjectLock unlock = objectLockRequest();
    unlock.lockId = agentSnapshot.lastObjectLockResult->lockId;
    const ClipboardRemoteReadDispatchResult unlockDispatch =
        agent.requestRemoteObjectUnlockTracked(unlock, 1000, 2000000);
    assert(unlockDispatch.dispatched);
    assert(agentRouter.sent.size() == 2);

    client.handlePacket(agentRouter.sent.back());
    assert(clientRegistry->lockCount() == 0);
    assert(clientRouter.sent.size() == 3);
    clientSnapshot = client.snapshot();
    assert(clientSnapshot.objectUnlockRequestsReceived == 1);
    assert(clientSnapshot.objectUnlockResponsesSent == 1);

    agent.handlePacket(clientRouter.sent.back());
    agentSnapshot = agent.snapshot();
    assert(agentSnapshot.objectUnlockRequestsSent == 1);
    assert(agentSnapshot.objectUnlockResponsesReceived == 1);
    assert(agentSnapshot.lastObjectUnlockResponseTo == unlockDispatch.messageId);
    assert(agentSnapshot.lastObjectUnlockResult.has_value());
    assert(agentSnapshot.lastObjectUnlockResult->ok());
    assert(agentSnapshot.lastObjectUnlockResult->lockId == unlock.lockId);
}

void fileContentsPolicyDeniesIncomingObjectLock()
{
    FakeRouter clientRouter;
    auto registry = std::make_shared<InMemoryTransferSourceRegistry>();

    ClipboardPolicy policy;
    policy.allowFileContents = false;
    ClipboardModuleDependencies deps;
    deps.policy = policy;
    deps.sourceRegistry = registry;

    ClipboardClientModule client(deps);
    start(client, runtime(100, clientRouter));
    assert(client.announceLocalBundle(fileObjectBundle(100)));

    client.handlePacket(objectLockPacket(objectLockRequest()));
    assert(registry->lockCount() == 0);
    assert(clientRouter.sent.size() == 2);
    assert(clientRouter.sent.back().messageKind == protocol::MessageKind::Error);
    assert(clientRouter.sent.back().responseStatus ==
           protocol::ResponseStatus::DeniedByPolicy);

    const FdclDecodeResult decoded =
        decodeFdclPayload(clientRouter.sent.back().payload);
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::ErrorDetail);
    assert(decoded.errorDetail.status ==
           protocol::ResponseStatus::DeniedByPolicy);

    const ClipboardModuleSnapshot snapshot = client.snapshot();
    assert(snapshot.objectLockRequestsReceived == 1);
    assert(snapshot.objectLockResponsesSent == 0);
    assert(snapshot.policyDenials == 1);
}

void reconnectPauseReleasesLocalObjectLocks()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    auto clientRegistry =
        std::make_shared<InMemoryTransferSourceRegistry>();

    ClipboardModuleDependencies clientDeps;
    clientDeps.sourceRegistry = clientRegistry;
    ClipboardClientModule client(clientDeps);
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileObjectBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult lockDispatch =
        agent.requestRemoteObjectLockTracked(objectLockRequest(),
                                             1000,
                                             1000000);
    assert(lockDispatch.dispatched);
    client.handlePacket(agentRouter.sent.back());
    assert(clientRegistry->lockCount() == 1);
    assert(clientRegistry->size() == 1);

    module::ModuleReconnectOptions reconnect;
    client.pauseForReconnect(reconnect);
    assert(clientRegistry->lockCount() == 0);
    assert(clientRegistry->size() == 0);

    const ClipboardModuleSnapshot snapshot = client.snapshot();
    assert(snapshot.objectLocksReleased == 1);
}

} // namespace

int main()
{
    objectLockAndUnlockRoundTripThroughModule();
    fileContentsPolicyDeniesIncomingObjectLock();
    reconnectPauseReleasesLocalObjectLocks();
    return 0;
}
