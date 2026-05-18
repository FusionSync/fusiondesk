#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

class ControlledBridgeChannel final : public network::IChannel
{
public:
    ControlledBridgeChannel(network::ChannelKey key,
                            network::NetworkRouter* peer,
                            bool* dropClipboardAcks = nullptr)
        : key_(key),
          peer_(peer),
          dropClipboardAcks_(dropClipboardAcks)
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
        return peer_ != nullptr;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        if (peer_ == nullptr)
            return {network::SendStatus::ChannelClosed, "peer router missing"};

        if (dropClipboardAcks_ != nullptr &&
            *dropClipboardAcks_ &&
            packet.packetType == protocol::PacketType::Clipboard &&
            packet.messageKind == protocol::MessageKind::Ack) {
            ++droppedAcks;
            return network::SendResult::sent();
        }

        peer_->submitIncoming(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;
    int droppedAcks = 0;

private:
    network::ChannelKey key_;
    network::NetworkRouter* peer_ = nullptr;
    bool* dropClipboardAcks_ = nullptr;
};

class RuntimeFileSource final
    : public TransferSource,
      public ITransferFileContentProvider
{
public:
    RuntimeFileSource(TransferFileList fileList, protocol::ByteBuffer content)
        : fileList_(std::move(fileList)),
          content_(std::move(content))
    {
    }

    TransferSourceId id() const override
    {
        return 888;
    }

    std::vector<TransferFormatDescriptor> formats() const override
    {
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
        return {descriptor};
    }

    TransferReadResult read(const TransferReadRequest&) override
    {
        TransferReadResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.canonicalFormat = FdclFileListFormat;
        result.encoding = TransferEncodingMode::CanonicalBytes;
        result.bytes = encodeTransferFileList(fileList_);
        return result;
    }

    TransferFileRangeResult readFileRange(
        const TransferFileRangeRequest& request) override
    {
        TransferFileRangeResult result;
        if (request.offset >= content_.size()) {
            result.status = protocol::ResponseStatus::Ok;
            result.endOfFile = true;
            return result;
        }

        const std::uint64_t available =
            static_cast<std::uint64_t>(content_.size()) - request.offset;
        const std::uint64_t count =
            std::min<std::uint64_t>(available, request.requestedBytes);
        result.status = protocol::ResponseStatus::Ok;
        result.bytes.insert(result.bytes.end(),
                            content_.begin() + request.offset,
                            content_.begin() + request.offset + count);
        result.endOfFile = request.offset + count >= content_.size();
        return result;
    }

private:
    TransferFileList fileList_;
    protocol::ByteBuffer content_;
};

struct PeerRuntime
{
    runtime::RuntimeHost host;
    session::Session* session = nullptr;
    ClipboardModuleBase* clipboard = nullptr;
    std::shared_ptr<InMemoryTransferSourceRegistry> registry;
    std::shared_ptr<ClipboardLargeDataWindow> largeDataWindow;
};

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Clipboard,
                                protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::Ack};
    return capabilities;
}

runtime::RuntimeOptions clipboardRuntimeOptions()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "clipboard-two-peer-reconnect-smoke";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    return options;
}

session::SessionCreateOptions makeSessionOptions(
    const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "clipboard-user";
    options.context.tenantId = "clipboard-tenant";
    options.context.clientDeviceId = "clipboard-client";
    options.context.agentDeviceId = "clipboard-agent";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

bool allStarted(const std::vector<module::ModuleStartReport>& reports)
{
    if (reports.empty())
        return false;

    for (const module::ModuleStartReport& report : reports) {
        if (!report.started)
            return false;
    }
    return true;
}

std::shared_ptr<ControlledBridgeChannel> bindBridgeChannel(
    session::Session& local,
    session::Session& peer,
    network::ChannelKey key,
    bool* dropClipboardAcks = nullptr)
{
    auto channel = std::make_shared<ControlledBridgeChannel>(
        key,
        &peer.network()->router(),
        dropClipboardAcks);
    assert(local.network()->bindChannel(channel).ok);
    network::ChannelReadyInfo ready;
    ready.endpoint = "clipboard-two-peer-reconnect-smoke";
    assert(local.network()->markReady(key, ready).ok);
    return channel;
}

PeerRuntime makePeer(session::SessionRole role)
{
    PeerRuntime peer;
    assert(peer.host.initialize(clipboardRuntimeOptions()));

    const protocol::SessionId id =
        role == session::SessionRole::Client
            ? peer.host.sessions().createClientSession(
                  makeSessionOptions(peer.host))
            : peer.host.sessions().createAgentSession(
                  makeSessionOptions(peer.host));
    peer.session = peer.host.sessions().find(id);
    assert(peer.session != nullptr);
    assert(peer.session->start());

    peer.registry = std::make_shared<InMemoryTransferSourceRegistry>();
    peer.largeDataWindow = std::make_shared<ClipboardLargeDataWindow>(4096);

    auto policy = std::make_shared<ClipboardPolicy>();
    policy->maxInlineBytes = 3;
    policy->maxFileRangeBytes = 32;

    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardPolicy = policy;
    dependencies.clipboardSourceRegistry = peer.registry;
    dependencies.clipboardLargeDataWindow = peer.largeDataWindow;
    assert(peer.host.mountProfileModules(*peer.session, dependencies).ok());

    const char* moduleId = role == session::SessionRole::Client
                               ? "clipboard.redirect.client"
                               : "clipboard.redirect.agent";
    peer.clipboard = dynamic_cast<ClipboardModuleBase*>(
        peer.session->moduleHost()->module(moduleId));
    assert(peer.clipboard != nullptr);
    return peer;
}

void startPeerModules(PeerRuntime& peer)
{
    assert(allStarted(peer.session->moduleHost()->startAllowedModules()));
}

TransferFileList fileList()
{
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "report.txt";
    file.sizeBytes = 11;

    TransferFileList files;
    files.files.push_back(std::move(file));
    return files;
}

TransferSourceBundle fileBundle(protocol::SessionId originSessionId)
{
    TransferSourceBundle bundle;
    bundle.bundleId = 111;
    bundle.offerId = 222;
    bundle.ownerEpoch = 333;
    bundle.sequence = 444;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = originSessionId;
    bundle.sources.push_back(
        std::make_shared<RuntimeFileSource>(fileList(), bytes("hello world")));
    return bundle;
}

FdclFileRangeRequest fileRangeReadRequest()
{
    FdclFileRangeRequest request;
    request.bundleId = 111;
    request.offerId = 222;
    request.ownerEpoch = 333;
    request.sourceId = 888;
    request.objectId = 9001;
    request.fileIndex = 0;
    request.offset = 0;
    request.requestedBytes = 11;
    return request;
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

void twoPeerSessionReconnectReleasesClipboardState()
{
    PeerRuntime client = makePeer(session::SessionRole::Client);
    PeerRuntime agent = makePeer(session::SessionRole::Agent);

    bool dropClientClipboardAcks = true;
    const network::ChannelKey smallData = clipboardSmallDataChannelKey();
    const network::ChannelKey largeData = clipboardLargeDataChannelKey();
    std::shared_ptr<ControlledBridgeChannel> clientSmallData =
        bindBridgeChannel(*client.session,
                          *agent.session,
                          smallData,
                          &dropClientClipboardAcks);
    bindBridgeChannel(*agent.session, *client.session, smallData);
    bindBridgeChannel(*client.session, *agent.session, largeData);
    std::shared_ptr<ControlledBridgeChannel> agentLargeData =
        bindBridgeChannel(*agent.session, *client.session, largeData);

    startPeerModules(client);
    startPeerModules(agent);

    assert(agent.clipboard->announceLocalBundle(fileBundle(200)));
    assert(agent.registry->size() == 1);
    assert(client.clipboard->snapshot().remoteBundle.offerId == 222);

    const ClipboardRemoteReadDispatchResult fileRange =
        client.clipboard->requestRemoteFileRangeTracked(
            fileRangeReadRequest(),
            1000,
            1000000);
    assert(fileRange.dispatched);
    assert(agent.clipboard->snapshot().pendingLargeDataResponses == 1);
    assert(agent.largeDataWindow->snapshot().inFlightBytes > 0);
    assert(agentLargeData->sentPackets.size() == 1);
    assert(clientSmallData->droppedAcks == 1);
    assert(client.clipboard->snapshot().fileRangeResponsesReceived == 1);

    const ClipboardRemoteReadDispatchResult lock =
        client.clipboard->requestRemoteObjectLockTracked(
            objectLockRequest(),
            1000,
            1001000);
    assert(lock.dispatched);
    assert(client.clipboard->snapshot().objectLockResponsesReceived == 1);
    assert(agent.registry->lockCount() == 1);

    session::ReconnectRequest reconnect;
    reconnect.reason = "two-peer clipboard large_data replaced";
    reconnect.degradedChannels = {largeData};
    reconnect.requestDisplayKeyframe = false;
    assert(agent.host.sessions().reconnect(agent.session->id(), reconnect));

    assert(agent.largeDataWindow->snapshot().inFlightBytes == 0);
    assert(agent.registry->lockCount() == 0);
    assert(agent.registry->size() == 0);

    const ClipboardModuleSnapshot snapshot = agent.clipboard->snapshot();
    assert(snapshot.pendingLargeDataResponses == 0);
    assert(snapshot.objectLocksReleased == 1);

    const session::ReconnectReport& report =
        agent.session->lastReconnectReport();
    assert(report.attempted);
    assert(report.ok);
    assert(report.pausedModules.size() == 1);
    assert(report.pausedModules.front().moduleId ==
           "clipboard.redirect.agent");
    assert(report.pausedModules.front().notified);
    assert(report.replayedIngress.size() == 1);
    assert(report.replayedIngress.front().replayed);
    assert(report.resumedModules.size() == 1);
    assert(report.resumedModules.front().notified);
}

} // namespace

int main()
{
    twoPeerSessionReconnectReleasesClipboardState();
    return 0;
}
