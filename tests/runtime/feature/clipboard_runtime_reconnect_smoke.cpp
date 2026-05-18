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

class RecordingChannel final : public network::IChannel
{
public:
    explicit RecordingChannel(network::ChannelKey key)
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
        sent.push_back(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sent;

private:
    network::ChannelKey key_;
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
    options.profile.profileId = "clipboard-runtime-reconnect-smoke";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    return options;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
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

std::shared_ptr<RecordingChannel> bindReadyChannel(session::Session& session,
                                                   network::ChannelKey key)
{
    std::shared_ptr<RecordingChannel> channel =
        std::make_shared<RecordingChannel>(key);
    assert(session.network()->bindChannel(channel).ok);
    network::ChannelReadyInfo ready;
    ready.endpoint = "clipboard-runtime-reconnect-smoke";
    assert(session.network()->markReady(key, ready).ok);
    return channel;
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

protocol::PacketEnvelope incomingClipboardRequest(protocol::MessageId id,
                                                  protocol::ByteBuffer payload)
{
    protocol::PacketEnvelope packet;
    packet.channelId = clipboardSmallDataChannelKey().channelId;
    packet.channelType = clipboardSmallDataChannelKey().channelType;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Request;
    packet.messageId = id;
    packet.correlationId = id;
    packet.timeoutMs = 1000;
    packet.payload = std::move(payload);
    return packet;
}

ClipboardClientModule* clipboardClientModule(session::Session& session)
{
    module::IModule* module =
        session.moduleHost()->module("clipboard.redirect.client");
    auto* clipboard = dynamic_cast<ClipboardClientModule*>(module);
    assert(clipboard != nullptr);
    return clipboard;
}

void sessionReconnectReleasesClipboardRuntimeState()
{
    auto registry = std::make_shared<InMemoryTransferSourceRegistry>();
    auto largeDataWindow = std::make_shared<ClipboardLargeDataWindow>(4096);
    auto policy = std::make_shared<ClipboardPolicy>();
    policy->maxInlineBytes = 3;
    policy->maxFileRangeBytes = 32;

    runtime::RuntimeHost host;
    assert(host.initialize(clipboardRuntimeOptions()));

    const protocol::SessionId sessionId =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(sessionId);
    assert(session != nullptr);
    assert(session->start());

    std::shared_ptr<RecordingChannel> smallData =
        bindReadyChannel(*session, clipboardSmallDataChannelKey());
    std::shared_ptr<RecordingChannel> largeData =
        bindReadyChannel(*session, clipboardLargeDataChannelKey());

    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardPolicy = policy;
    dependencies.clipboardSourceRegistry = registry;
    dependencies.clipboardLargeDataWindow = largeDataWindow;
    assert(host.mountProfileModules(*session, dependencies).ok());
    assert(allStarted(session->moduleHost()->startAllowedModules()));

    ClipboardClientModule* clipboard = clipboardClientModule(*session);
    assert(clipboard->announceLocalBundle(fileBundle(sessionId)));
    assert(registry->size() == 1);
    assert(!smallData->sent.empty());

    session->network()->router().submitIncoming(
        incomingClipboardRequest(8001,
                                 encodeFdclFileRangeRequest(
                                     fileRangeReadRequest())));
    assert(!largeData->sent.empty());
    assert(largeData->sent.back().messageKind ==
           protocol::MessageKind::Response);
    assert(largeDataWindow->snapshot().inFlightBytes ==
           largeData->sent.back().payload.size());
    assert(clipboard->snapshot().pendingLargeDataResponses == 1);

    session->network()->router().submitIncoming(
        incomingClipboardRequest(8002,
                                 encodeFdclLockObject(objectLockRequest())));
    assert(registry->lockCount() == 1);
    assert(clipboard->snapshot().objectLockResponsesSent == 1);

    session::ReconnectRequest reconnect;
    reconnect.reason = "clipboard large data channel replaced";
    reconnect.degradedChannels = {clipboardLargeDataChannelKey()};
    reconnect.requestDisplayKeyframe = false;
    assert(host.sessions().reconnect(sessionId, reconnect));

    assert(session->state() == session::SessionState::Running);
    assert(largeDataWindow->snapshot().inFlightBytes == 0);
    assert(registry->lockCount() == 0);
    assert(registry->size() == 0);

    const ClipboardModuleSnapshot snapshot = clipboard->snapshot();
    assert(snapshot.pendingLargeDataResponses == 0);
    assert(snapshot.objectLocksReleased == 1);

    const session::ReconnectReport& report = session->lastReconnectReport();
    assert(report.attempted);
    assert(report.ok);
    assert(report.pausedModules.size() == 1);
    assert(report.pausedModules.front().moduleId ==
           "clipboard.redirect.client");
    assert(report.pausedModules.front().notified);
    assert(report.replayedIngress.size() == 1);
    assert(report.replayedIngress.front().replayed);
    assert(report.replayedIngress.front().tokenCount == 2);
    assert(report.resumedModules.size() == 1);
    assert(report.resumedModules.front().notified);
}

} // namespace

int main()
{
    sessionReconnectReleasesClipboardRuntimeState();
    return 0;
}
