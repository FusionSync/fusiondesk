#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"
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

class MemoryFileContentSource final
    : public TransferSource,
      public ITransferFileContentProvider
{
public:
    explicit MemoryFileContentSource(protocol::ByteBuffer content)
        : content_(std::move(content))
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
        result.status = protocol::ResponseStatus::Unsupported;
        result.message = "file list read is not used by this test";
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
    protocol::ByteBuffer content_;
};

TransferSourceBundle fileContentBundle()
{
    TransferSourceBundle bundle;
    bundle.bundleId = 111;
    bundle.offerId = 222;
    bundle.ownerEpoch = 333;
    bundle.sequence = 444;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = 100;
    bundle.sources.push_back(
        std::make_shared<MemoryFileContentSource>(bytes("hello world")));
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

protocol::NegotiatedCapabilities clipboardCapabilities()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Standard};
    capabilities.packetTypes = {protocol::PacketType::Clipboard};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Error};
    return capabilities;
}

void registerReadyChannel(network::ChannelRegistry& channels,
                          const network::ChannelSpec& spec)
{
    assert(channels.registerSpec(spec).ok);
    assert(channels.bind(spec.key,
                         std::make_shared<FakeChannel>(spec.key))
               .ok);
    assert(channels.markReady(spec.key, {}).ok);
}

void registerReadyRecordingChannel(
    network::NetworkRouter& router,
    network::ChannelRegistry& channels,
    const network::ChannelSpec& spec,
    std::shared_ptr<RecordingChannel>* outChannel)
{
    auto channel = std::make_shared<RecordingChannel>(spec.key);
    assert(channels.registerSpec(spec).ok);
    assert(channels.bind(spec.key, channel).ok);
    assert(channels.markReady(spec.key, {}).ok);
    assert(router.registerChannel(channel));
    if (outChannel != nullptr)
        *outChannel = std::move(channel);
}

module::ModuleRuntime runtime(protocol::SessionId sessionId,
                              FakeRouter& router,
                              network::ChannelRegistry* channels = nullptr)
{
    module::ModuleRuntime value;
    value.session.sessionId = sessionId;
    value.session.traceId = sessionId + 1000;
    value.session.localPlatform = "windows";
    value.network = &router;
    value.channels = channels;
    return value;
}

void start(module::IModule& module, const module::ModuleRuntime& runtime)
{
    assert(module.attach(runtime));
    assert(module.start({}));
    assert(module.state() == module::ModuleState::Running);
}

protocol::PacketEnvelope fileRangeRequestPacket()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 100;
    packet.traceId = 1100;
    packet.channelId = clipboardSmallDataChannelKey().channelId;
    packet.channelType = clipboardSmallDataChannelKey().channelType;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Request;
    packet.priority = protocol::PacketPriority::Normal;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.messageId = 777;
    packet.correlationId = 777;
    packet.timeoutMs = 1000;
    packet.payload = encodeFdclFileRangeRequest(fileRangeReadRequest());
    return packet;
}

protocol::PacketEnvelope largeDataAckFor(
    const protocol::PacketEnvelope& response)
{
    protocol::PacketEnvelope ack;
    ack.sessionId = response.sessionId;
    ack.traceId = response.traceId;
    ack.channelId = clipboardSmallDataChannelKey().channelId;
    ack.channelType = clipboardSmallDataChannelKey().channelType;
    ack.packetType = protocol::PacketType::Clipboard;
    ack.messageKind = protocol::MessageKind::Ack;
    ack.priority = protocol::PacketPriority::Interactive;
    ack.responseStatus = protocol::ResponseStatus::Ok;
    ack.responseTo = response.messageId;
    ack.correlationId = response.correlationId;
    ack.timeoutMs = response.timeoutMs;
    return ack;
}

void oversizedFileRangeFailsWhenLargeDataIsUnavailable()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    network::ChannelRegistry clientChannels(clipboardCapabilities());

    ClipboardPolicy policy;
    policy.maxInlineBytes = 3;
    policy.maxFileRangeBytes = 32;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    ClipboardAgentModule agent(ClipboardModuleDependencies{nullptr, policy});
    start(client, runtime(100, clientRouter, &clientChannels));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle()));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());

    assert(clientRouter.sent.size() == 2);
    assert(clientRouter.sent.back().messageKind == protocol::MessageKind::Error);
    assert(clientRouter.sent.back().responseStatus ==
           protocol::ResponseStatus::ChannelUnavailable);

    agent.handlePacket(clientRouter.sent.back());
    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastFileRangeResult.has_value());
    assert(snapshot.lastFileRangeResult->status ==
           protocol::ResponseStatus::ChannelUnavailable);
}

void oversizedFileRangeUsesReadyLargeData()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    network::ChannelRegistry clientChannels(clipboardCapabilities());
    registerReadyChannel(clientChannels, network::defaultLargeDataChannelSpec());

    ClipboardPolicy policy;
    policy.maxInlineBytes = 3;
    policy.maxFileRangeBytes = 32;
    ClipboardClientModule client(ClipboardModuleDependencies{nullptr, policy});
    ClipboardAgentModule agent(ClipboardModuleDependencies{nullptr, policy});
    start(client, runtime(100, clientRouter, &clientChannels));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle()));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());

    assert(clientRouter.sent.size() == 2);
    const protocol::PacketEnvelope& response = clientRouter.sent.back();
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.channelId ==
           static_cast<protocol::ChannelId>(
               protocol::ChannelIdValue::LargeData));
    assert(client.snapshot().fileRangeLargeDataResponsesSent == 1);

    agent.handlePacket(response);
    const ClipboardModuleSnapshot snapshot = agent.snapshot();
    assert(snapshot.lastFileRangeResult.has_value());
    assert(snapshot.lastFileRangeResult->status == protocol::ResponseStatus::Ok);
    assert(snapshot.lastFileRangeResult->bytes == bytes("hello world"));
}

void largeDataWindowBackPressuresUntilAcknowledged()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    network::ChannelRegistry clientChannels(clipboardCapabilities());
    registerReadyChannel(clientChannels, network::defaultLargeDataChannelSpec());

    auto window = std::make_shared<ClipboardLargeDataWindow>(4096);

    ClipboardPolicy policy;
    policy.maxInlineBytes = 3;
    policy.maxFileRangeBytes = 32;
    ClipboardModuleDependencies clientDependencies;
    clientDependencies.policy = policy;
    clientDependencies.largeDataWindow = window;
    ClipboardClientModule client(clientDependencies);
    ClipboardAgentModule agent(ClipboardModuleDependencies{nullptr, policy});
    start(client, runtime(100, clientRouter, &clientChannels));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle()));
    agent.handlePacket(clientRouter.sent.back());

    ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());

    const protocol::PacketEnvelope firstResponse = clientRouter.sent.back();
    assert(firstResponse.messageKind == protocol::MessageKind::Response);
    assert(firstResponse.channelId ==
           static_cast<protocol::ChannelId>(
               protocol::ChannelIdValue::LargeData));
    const std::uint64_t firstReservation =
        window->snapshot().inFlightBytes;
    assert(firstReservation == firstResponse.payload.size());
    agent.handlePacket(firstResponse);
    const protocol::PacketEnvelope firstAck = agentRouter.sent.back();
    assert(firstAck.messageKind == protocol::MessageKind::Ack);
    assert(firstAck.packetType == protocol::PacketType::Clipboard);
    assert(firstAck.responseTo == firstResponse.messageId);
    assert(agent.snapshot().largeDataAcksSent == 1);
    assert(client.snapshot().pendingLargeDataResponses == 1);

    window->setMaxInFlightBytes(firstReservation);
    dispatched = agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                                     1000,
                                                     1001000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());
    assert(clientRouter.sent.back().messageKind ==
           protocol::MessageKind::Error);
    assert(clientRouter.sent.back().responseStatus ==
           protocol::ResponseStatus::BackPressure);
    assert(client.snapshot().backPressureFailures == 1);
    agent.handlePacket(clientRouter.sent.back());

    client.handlePacket(firstAck);
    assert(window->snapshot().inFlightBytes == 0);
    assert(client.snapshot().largeDataAcksReceived == 1);
    assert(client.snapshot().pendingLargeDataResponses == 0);
    dispatched = agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                                     1000,
                                                     1002000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());
    const protocol::PacketEnvelope thirdResponse = clientRouter.sent.back();
    assert(thirdResponse.messageKind == protocol::MessageKind::Response);
    assert(thirdResponse.channelId ==
           static_cast<protocol::ChannelId>(
               protocol::ChannelIdValue::LargeData));
    assert(window->snapshot().inFlightBytes == thirdResponse.payload.size());
}

void reconnectReleasesPendingLargeDataReservations()
{
    FakeRouter clientRouter;
    FakeRouter agentRouter;
    network::ChannelRegistry clientChannels(clipboardCapabilities());
    registerReadyChannel(clientChannels, network::defaultLargeDataChannelSpec());

    auto window = std::make_shared<ClipboardLargeDataWindow>(4096);

    ClipboardPolicy policy;
    policy.maxInlineBytes = 3;
    policy.maxFileRangeBytes = 32;
    ClipboardModuleDependencies clientDependencies;
    clientDependencies.policy = policy;
    clientDependencies.largeDataWindow = window;
    ClipboardClientModule client(clientDependencies);
    ClipboardAgentModule agent(ClipboardModuleDependencies{nullptr, policy});
    start(client, runtime(100, clientRouter, &clientChannels));
    start(agent, runtime(200, agentRouter));

    assert(client.announceLocalBundle(fileContentBundle()));
    agent.handlePacket(clientRouter.sent.back());

    const ClipboardRemoteReadDispatchResult dispatched =
        agent.requestRemoteFileRangeTracked(fileRangeReadRequest(),
                                            1000,
                                            1000000);
    assert(dispatched.dispatched);
    client.handlePacket(agentRouter.sent.back());

    const protocol::PacketEnvelope response = clientRouter.sent.back();
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.channelId ==
           static_cast<protocol::ChannelId>(
               protocol::ChannelIdValue::LargeData));
    assert(window->snapshot().inFlightBytes == response.payload.size());
    assert(client.snapshot().pendingLargeDataResponses == 1);

    module::ModuleReconnectOptions reconnect;
    reconnect.affectedChannels = {clipboardLargeDataChannelKey()};
    client.pauseForReconnect(reconnect);

    assert(window->snapshot().inFlightBytes == 0);
    assert(client.snapshot().pendingLargeDataResponses == 0);
}

void moduleIngressRoutesLargeDataAckToReleaseWindow()
{
    network::NetworkRouter router;
    network::ChannelRegistry channels(clipboardCapabilities());
    std::shared_ptr<RecordingChannel> smallDataChannel;
    std::shared_ptr<RecordingChannel> largeDataChannel;
    network::ChannelSpec smallDataSpec = network::defaultMvpChannelSpecs()[1];
    registerReadyRecordingChannel(router,
                                  channels,
                                  smallDataSpec,
                                  &smallDataChannel);
    registerReadyRecordingChannel(router,
                                  channels,
                                  network::defaultLargeDataChannelSpec(),
                                  &largeDataChannel);

    auto window = std::make_shared<ClipboardLargeDataWindow>(4096);
    ClipboardPolicy policy;
    policy.maxInlineBytes = 3;
    policy.maxFileRangeBytes = 32;
    ClipboardModuleDependencies dependencies;
    dependencies.policy = policy;
    dependencies.largeDataWindow = window;
    auto client = std::make_shared<ClipboardClientModule>(dependencies);

    module::ModuleRuntime moduleRuntime;
    moduleRuntime.session.sessionId = 100;
    moduleRuntime.session.traceId = 1100;
    moduleRuntime.session.role = session::SessionRole::Client;
    moduleRuntime.session.localPlatform = "windows";
    moduleRuntime.network = &router;
    moduleRuntime.channels = &channels;

    policy::StaticPolicyEngine policyEngine(protocol::FeatureSet{
        protocol::feature::Clipboard});
    module::ModuleHost host(moduleRuntime, &policyEngine);
    assert(host.addModule(client));
    const std::vector<module::ModuleStartReport> reports =
        host.startAllowedModules();
    assert(reports.size() == 1);
    assert(reports.front().started);

    assert(client->announceLocalBundle(fileContentBundle()));
    router.submitIncoming(fileRangeRequestPacket());

    assert(!largeDataChannel->sent.empty());
    const protocol::PacketEnvelope response = largeDataChannel->sent.back();
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(window->snapshot().inFlightBytes == response.payload.size());
    assert(client->snapshot().pendingLargeDataResponses == 1);

    router.submitIncoming(largeDataAckFor(response));

    assert(window->snapshot().inFlightBytes == 0);
    assert(client->snapshot().largeDataAcksReceived == 1);
    assert(client->snapshot().pendingLargeDataResponses == 0);
}

} // namespace

int main()
{
    oversizedFileRangeFailsWhenLargeDataIsUnavailable();
    oversizedFileRangeUsesReadyLargeData();
    largeDataWindowBackPressuresUntilAcknowledged();
    reconnectReleasesPendingLargeDataReservations();
    moduleIngressRoutesLargeDataAckToReleaseWindow();
    return 0;
}
