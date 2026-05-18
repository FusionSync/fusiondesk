#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/modules/clipboard/fdcl_codec.h"

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

class FakeChannel final : public network::IChannel
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

    network::SendResult send(const protocol::PacketEnvelope&) override
    {
        return network::SendResult::sent();
    }

private:
    network::ChannelKey key_;
};

class MemoryFileContentSource final
    : public TransferSource,
      public ITransferFileContentProvider
{
public:
    MemoryFileContentSource(TransferSourceId sourceId,
                            TransferFormatDescriptor descriptor,
                            TransferFileList fileList,
                            protocol::ByteBuffer content)
        : sourceId_(sourceId),
          descriptor_(std::move(descriptor)),
          fileList_(std::move(fileList)),
          content_(std::move(content))
    {
        descriptor_.canonicalFormat = FdclFileListFormat;
    }

    TransferSourceId id() const override
    {
        return sourceId_;
    }

    std::vector<TransferFormatDescriptor> formats() const override
    {
        return {descriptor_};
    }

    TransferReadResult read(const TransferReadRequest& request) override
    {
        FileGroupTransferSource fileListSource(sourceId_, descriptor_, fileList_);
        return fileListSource.read(request);
    }

    TransferFileRangeResult readFileRange(
        const TransferFileRangeRequest& request) override
    {
        TransferFileRangeResult result;
        if (request.sourceId != 0 && request.sourceId != sourceId_) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "file source id is not found";
            return result;
        }
        if (request.fileIndex != 0 ||
            fileList_.files.empty() ||
            request.objectId != fileList_.files.front().objectId) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "file object is not found";
            return result;
        }
        if (request.offset >= content_.size()) {
            result.status = protocol::ResponseStatus::Ok;
            result.endOfFile = true;
            return result;
        }

        const std::size_t offset = static_cast<std::size_t>(request.offset);
        const std::size_t remaining = content_.size() - offset;
        const std::size_t requested =
            request.requestedBytes == 0
                ? remaining
                : static_cast<std::size_t>(request.requestedBytes);
        const std::size_t count = std::min(remaining, requested);
        result.status = protocol::ResponseStatus::Ok;
        result.bytes.assign(content_.begin() + static_cast<std::ptrdiff_t>(offset),
                            content_.begin() + static_cast<std::ptrdiff_t>(offset + count));
        result.endOfFile = offset + count >= content_.size();
        return result;
    }

private:
    TransferSourceId sourceId_ = 0;
    TransferFormatDescriptor descriptor_;
    TransferFileList fileList_;
    protocol::ByteBuffer content_;
};

TransferSourceBundle fileListBundle(protocol::SessionId originSessionId = 100)
{
    TransferFileList files;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "..\\report.pdf";
    file.sizeBytes = 4096;
    files.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.localFormatToken = 500;
    descriptor.formatId = 501;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = false;
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
        std::make_shared<FileGroupTransferSource>(888,
                                                  descriptor,
                                                  files));
    return bundle;
}

FdclReadFormatRequest fileListReadRequest()
{
    FdclReadFormatRequest request;
    request.bundleId = 111;
    request.offerId = 222;
    request.ownerEpoch = 333;
    request.sourceId = 888;
    request.itemIndex = 0;
    request.formatId = 501;
    request.localFormatToken = 500;
    request.acceptedMaxBytes = 1024;
    request.streamAccepted = false;
    request.requestedEncoding = TransferEncodingMode::CanonicalBytes;
    request.canonicalFormat = FdclFileListFormat;
    return request;
}

TransferSourceBundle fileContentBundle(protocol::SessionId originSessionId = 100)
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
        std::make_shared<MemoryFileContentSource>(888,
                                                  descriptor,
                                                  files,
                                                  bytes("hello world")));
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
    request.offset = 6;
    request.requestedBytes = 5;
    return request;
}

protocol::NegotiatedCapabilities clipboardNegotiatedCapabilities()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Standard};
    capabilities.packetTypes = {protocol::PacketType::Clipboard};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::Cancel};
    return capabilities;
}

void registerReadyChannel(network::ChannelRegistry& channels,
                          const network::ChannelSpec& spec)
{
    assert(channels.registerSpec(spec).ok);
    assert(channels.bind(spec.key,
                         std::make_shared<FakeChannel>(spec.key))
               .ok);
    network::ChannelReadyInfo ready;
    ready.endpoint = spec.name;
    assert(channels.markReady(spec.key, ready).ok);
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

module::ModuleRuntime runtimeWithChannels(protocol::SessionId sessionId,
                                          FakeRouter& router,
                                          network::ChannelRegistry& channels)
{
    module::ModuleRuntime value = runtime(sessionId, router);
    value.channels = &channels;
    return value;
}

void start(module::IModule& module, const module::ModuleRuntime& runtime)
{
    assert(module.attach(runtime));
    assert(module.start({}));
    assert(module.state() == module::ModuleState::Running);
}

void fileListBundleRoundTripsThroughReadRequest()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardClientModule client;
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileListBundle(100)));
    assert(clientRouter.sent.size() == 1);

    agent.handlePacket(clientRouter.sent.back());
    ClipboardModuleSnapshot agentSnapshot = agent.snapshot();
    assert(agentSnapshot.formatListsReceived == 1);
    assert(agentSnapshot.remoteBundle.sources.size() == 1);
    assert(agentSnapshot.remoteBundle.sources.front()->formats().front().canonicalFormat ==
           FdclFileListFormat);

    assert(agent.requestRemoteFormat(fileListReadRequest(), 1000, 1000000));
    assert(agentRouter.sent.size() == 1);
    client.handlePacket(agentRouter.sent.back());
    assert(clientRouter.sent.size() == 2);
    agent.handlePacket(clientRouter.sent.back());

    agentSnapshot = agent.snapshot();
    assert(agentSnapshot.lastReadResult.has_value());
    assert(agentSnapshot.lastReadResult->status == protocol::ResponseStatus::Ok);
    assert(agentSnapshot.lastReadResult->canonicalFormat == FdclFileListFormat);

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(agentSnapshot.lastReadResult->bytes);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 1);
    assert(decoded.fileList.files.front().displayName == "___report.pdf");
    assert(decoded.fileList.files.front().sizeBytes == 4096);
}

void fileListPolicyCanDenyAnnouncement()
{
    FakeRouter clientRouter;
    ClipboardPolicy policy;
    policy.allowFileList = false;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    start(client, runtime(100, clientRouter));

    assert(!client.announceLocalBundle(fileListBundle(100)));
    const ClipboardModuleSnapshot snapshot = client.snapshot();
    assert(snapshot.policyDenials == 1);
    assert(clientRouter.sent.empty());
}

void fileRangeRequestRoundTripsThroughModule()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardClientModule client;
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle(100)));
    assert(clientRouter.sent.size() == 1);

    agent.handlePacket(clientRouter.sent.back());
    ClipboardModuleSnapshot agentSnapshot = agent.snapshot();
    assert(agentSnapshot.formatListsReceived == 1);
    assert(agentSnapshot.remoteBundle.sources.size() == 1);
    assert(agentSnapshot.remoteBundle.sources.front()->formats().front().canStream);

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(), 1000, 1000000);
    assert(dispatched.dispatched);
    assert(dispatched.messageId != 0);
    assert(agentRouter.sent.size() == 1);

    client.handlePacket(agentRouter.sent.back());
    assert(clientRouter.sent.size() == 2);
    assert(clientRouter.sent.back().priority == protocol::PacketPriority::Bulk);
    assert(clientRouter.sent.back().channelId ==
           static_cast<protocol::ChannelId>(
               protocol::ChannelIdValue::SmallData));
    ClipboardModuleSnapshot clientSnapshot = client.snapshot();
    assert(clientSnapshot.fileRangeRequestsReceived == 1);
    assert(clientSnapshot.fileRangeResponsesSent == 1);
    assert(clientSnapshot.fileRangeSmallDataResponsesSent == 1);
    assert(clientSnapshot.fileRangeLargeDataResponsesSent == 0);
    assert(clientSnapshot.fileRangeBytesSent == 5);

    agent.handlePacket(clientRouter.sent.back());
    agentSnapshot = agent.snapshot();
    assert(agentSnapshot.lastFileRangeResponseTo == dispatched.messageId);
    assert(agentSnapshot.fileRangeRequestsSent == 1);
    assert(agentSnapshot.fileRangeResponsesReceived == 1);
    assert(agentSnapshot.fileRangeBytesReceived == 5);
    assert(agentSnapshot.lastFileRangeResult.has_value());
    assert(agentSnapshot.lastFileRangeResult->status == protocol::ResponseStatus::Ok);
    assert(agentSnapshot.lastFileRangeResult->bytes == bytes("world"));
    assert(agentSnapshot.lastFileRangeResult->endOfFile);
}

void fileRangeResponseUsesLargeDataWhenReady()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    network::ChannelRegistry clientChannels(clipboardNegotiatedCapabilities());
    registerReadyChannel(clientChannels, network::defaultLargeDataChannelSpec());

    ClipboardClientModule client;
    ClipboardAgentModule agent;
    start(client, runtimeWithChannels(100, clientRouter, clientChannels));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());

    assert(clientRouter.sent.size() == 2);
    const protocol::PacketEnvelope& response = clientRouter.sent.back();
    assert(response.priority == protocol::PacketPriority::Bulk);
    assert(response.channelId ==
           static_cast<protocol::ChannelId>(
               protocol::ChannelIdValue::LargeData));
    assert(response.channelType == protocol::ChannelType::Standard);
    const ClipboardModuleSnapshot clientSnapshot = client.snapshot();
    assert(clientSnapshot.fileRangeLargeDataResponsesSent == 1);
    assert(clientSnapshot.fileRangeSmallDataResponsesSent == 0);
    assert(clientSnapshot.fileRangeBytesSent == 5);

    agent.handlePacket(response);
    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastFileRangeResponseTo == dispatched.messageId);
    assert(snapshot.lastFileRangeResult.has_value());
    assert(snapshot.lastFileRangeResult->bytes == bytes("world"));
    assert(snapshot.fileRangeBytesReceived == 5);
}

void fileContentsPolicyCanDenyRangeDispatch()
{
    FakeRouter agentRouter;
    ClipboardPolicy policy;
    policy.allowFileContents = false;
    ClipboardAgentModule agent(ClipboardModuleDependencies{nullptr, policy});
    start(agent, runtime(200, agentRouter));

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(!dispatched.dispatched);
    assert(dispatched.status == protocol::ResponseStatus::DeniedByPolicy);
    assert(agentRouter.sent.empty());
    assert(agent.snapshot().policyDenials == 1);
}

void fileRangeDispatchRequiresCurrentStreamableOffer()
{
    {
        FakeRouter agentRouter;
        ClipboardAgentModule agent;
        start(agent, runtime(200, agentRouter));

        const ClipboardRemoteReadDispatchResult dispatched =
            agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                                1000,
                                                1000000);
        assert(!dispatched.dispatched);
        assert(dispatched.status == protocol::ResponseStatus::Conflict);
        assert(agentRouter.sent.empty());
        assert(agent.snapshot().staleOfferFailures == 1);
    }

    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardClientModule client;
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileListBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(!dispatched.dispatched);
    assert(dispatched.status == protocol::ResponseStatus::Unsupported);
    assert(agentRouter.sent.empty());
}

void fileRangePolicyClampsRequestedBytes()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    ClipboardPolicy policy;
    policy.maxFileRangeBytes = 3;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    ClipboardAgentModule agent;
    start(client, runtime(100, clientRouter));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle(100)));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastFileRangeResult.has_value());
    assert(snapshot.lastFileRangeResult->status == protocol::ResponseStatus::Ok);
    assert(snapshot.lastFileRangeResult->bytes == bytes("wor"));
    assert(!snapshot.lastFileRangeResult->endOfFile);
}

} // namespace

int main()
{
    fileListBundleRoundTripsThroughReadRequest();
    fileListPolicyCanDenyAnnouncement();
    fileRangeRequestRoundTripsThroughModule();
    fileRangeResponseUsesLargeDataWhenReady();
    fileContentsPolicyCanDenyRangeDispatch();
    fileRangeDispatchRequiresCurrentStreamableOffer();
    fileRangePolicyClampsRequestedBytes();
    return 0;
}
