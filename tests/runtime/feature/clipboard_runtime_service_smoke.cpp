#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/runtime/feature/clipboard_product_presenter.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;

namespace {

class RecordingChannel : public network::IChannel
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
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    network::ChannelKey key_;
};

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

class ImmediateReadResponsePump
    : public runtime::feature::IClipboardRuntimeReadPump
{
public:
    ImmediateReadResponsePump(
        modules::clipboard::ClipboardModuleBase& module,
        RecordingChannel& channel)
        : module_(module),
          channel_(channel)
    {
    }

    void pumpOnce() override
    {
        ++pumps;
        nowUsec += 1000;
        if (responded || channel_.sentPackets.empty())
            return;

        const protocol::PacketEnvelope requestPacket =
            channel_.sentPackets.back();
        const modules::clipboard::FdclDecodeResult decoded =
            modules::clipboard::decodeFdclPayload(requestPacket.payload);
        assert(decoded.ok);
        assert(decoded.operation ==
               modules::clipboard::FdclOperation::ReadFormatRequest);

        modules::clipboard::FdclReadFormatResponse payload;
        payload.bundleId = decoded.readRequest.bundleId;
        payload.offerId = decoded.readRequest.offerId;
        payload.ownerEpoch = decoded.readRequest.ownerEpoch;
        payload.sourceId = decoded.readRequest.sourceId;
        payload.itemIndex = decoded.readRequest.itemIndex;
        payload.formatId = decoded.readRequest.formatId;
        payload.canonicalFormat = decoded.readRequest.canonicalFormat;
        payload.encoding = decoded.readRequest.requestedEncoding;
        payload.bytes = bytes("remote response");

        protocol::PacketEnvelope response = requestPacket;
        response.messageKind = protocol::MessageKind::Response;
        response.responseStatus = protocol::ResponseStatus::Ok;
        response.responseTo = requestPacket.messageId;
        response.messageId = requestPacket.messageId + 1000;
        response.payload =
            modules::clipboard::encodeFdclReadFormatResponse(payload);
        module_.handlePacket(response);
        responded = true;
    }

    std::uint64_t monotonicNowUsec() const override
    {
        return nowUsec;
    }

    modules::clipboard::ClipboardModuleBase& module_;
    RecordingChannel& channel_;
    std::uint64_t nowUsec = 1000000;
    int pumps = 0;
    bool responded = false;
};

class ImmediateFileRangeResponsePump
    : public runtime::feature::IClipboardRuntimeReadPump
{
public:
    ImmediateFileRangeResponsePump(
        modules::clipboard::ClipboardModuleBase& module,
        RecordingChannel& channel)
        : module_(module),
          channel_(channel)
    {
    }

    void pumpOnce() override
    {
        ++pumps;
        nowUsec += 1000;
        if (responded || channel_.sentPackets.empty())
            return;

        const protocol::PacketEnvelope requestPacket =
            channel_.sentPackets.back();
        const modules::clipboard::FdclDecodeResult decoded =
            modules::clipboard::decodeFdclPayload(requestPacket.payload);
        assert(decoded.ok);
        assert(decoded.operation ==
               modules::clipboard::FdclOperation::FileRangeRequest);

        modules::clipboard::FdclFileRangeResponse payload;
        payload.bundleId = decoded.fileRangeRequest.bundleId;
        payload.offerId = decoded.fileRangeRequest.offerId;
        payload.ownerEpoch = decoded.fileRangeRequest.ownerEpoch;
        payload.sourceId = decoded.fileRangeRequest.sourceId;
        payload.objectId = decoded.fileRangeRequest.objectId;
        payload.fileIndex = decoded.fileRangeRequest.fileIndex;
        payload.offset = decoded.fileRangeRequest.offset;
        payload.endOfFile = true;
        payload.bytes = bytes("range");

        protocol::PacketEnvelope response = requestPacket;
        response.messageKind = protocol::MessageKind::Response;
        response.responseStatus = protocol::ResponseStatus::Ok;
        response.responseTo = requestPacket.messageId;
        response.messageId = requestPacket.messageId + 1000;
        response.payload =
            modules::clipboard::encodeFdclFileRangeResponse(payload);
        module_.handlePacket(response);
        responded = true;
    }

    std::uint64_t monotonicNowUsec() const override
    {
        return nowUsec;
    }

    modules::clipboard::ClipboardModuleBase& module_;
    RecordingChannel& channel_;
    std::uint64_t nowUsec = 1000000;
    int pumps = 0;
    bool responded = false;
};

class ChunkedFileRangeResponsePump
    : public runtime::feature::IClipboardRuntimeReadPump
{
public:
    ChunkedFileRangeResponsePump(
        modules::clipboard::ClipboardModuleBase& module,
        RecordingChannel& channel,
        protocol::ByteBuffer content)
        : module_(module),
          channel_(channel),
          content_(content)
    {
    }

    void pumpOnce() override
    {
        ++pumps;
        nowUsec += 1000;
        if (channel_.sentPackets.empty())
            return;

        const protocol::PacketEnvelope requestPacket =
            channel_.sentPackets.back();
        if (std::find(respondedMessageIds.begin(),
                      respondedMessageIds.end(),
                      requestPacket.messageId) != respondedMessageIds.end()) {
            return;
        }

        const modules::clipboard::FdclDecodeResult decoded =
            modules::clipboard::decodeFdclPayload(requestPacket.payload);
        assert(decoded.ok);
        assert(decoded.operation ==
               modules::clipboard::FdclOperation::FileRangeRequest);

        offsets.push_back(decoded.fileRangeRequest.offset);
        requestedBytes.push_back(decoded.fileRangeRequest.requestedBytes);

        modules::clipboard::FdclFileRangeResponse payload;
        payload.bundleId = decoded.fileRangeRequest.bundleId;
        payload.offerId = decoded.fileRangeRequest.offerId;
        payload.ownerEpoch = decoded.fileRangeRequest.ownerEpoch;
        payload.sourceId = decoded.fileRangeRequest.sourceId;
        payload.objectId = decoded.fileRangeRequest.objectId;
        payload.fileIndex = decoded.fileRangeRequest.fileIndex;
        payload.offset = decoded.fileRangeRequest.offset;

        if (decoded.fileRangeRequest.offset >= content_.size()) {
            payload.endOfFile = true;
        } else {
            const std::size_t offset =
                static_cast<std::size_t>(decoded.fileRangeRequest.offset);
            const std::size_t requested =
                static_cast<std::size_t>(
                    decoded.fileRangeRequest.requestedBytes);
            const std::size_t count =
                std::min(requested, content_.size() - offset);
            payload.bytes.insert(
                payload.bytes.end(),
                content_.begin() + static_cast<std::ptrdiff_t>(offset),
                content_.begin() +
                    static_cast<std::ptrdiff_t>(offset + count));
            payload.endOfFile = offset + count >= content_.size();
        }

        protocol::PacketEnvelope response = requestPacket;
        response.messageKind = protocol::MessageKind::Response;
        response.responseStatus = protocol::ResponseStatus::Ok;
        response.responseTo = requestPacket.messageId;
        response.messageId = requestPacket.messageId + 1000;
        response.payload =
            modules::clipboard::encodeFdclFileRangeResponse(payload);
        module_.handlePacket(response);
        respondedMessageIds.push_back(requestPacket.messageId);
    }

    std::uint64_t monotonicNowUsec() const override
    {
        return nowUsec;
    }

    modules::clipboard::ClipboardModuleBase& module_;
    RecordingChannel& channel_;
    protocol::ByteBuffer content_;
    std::uint64_t nowUsec = 1000000;
    int pumps = 0;
    std::vector<protocol::MessageId> respondedMessageIds;
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> requestedBytes;
};

class TimeoutReadPump : public runtime::feature::IClipboardRuntimeReadPump
{
public:
    void pumpOnce() override
    {
        ++pumps;
        nowUsec += 2000;
    }

    std::uint64_t monotonicNowUsec() const override
    {
        return nowUsec;
    }

    std::uint64_t nowUsec = 1000000;
    int pumps = 0;
};

class FakeClipboardEndpoint : public modules::clipboard::IClipboardEndpoint
{
public:
    modules::clipboard::ClipboardSnapshot snapshot() override
    {
        modules::clipboard::ClipboardSnapshot result;
        result.ownerEpoch = bundle.ownerEpoch;
        result.sequence = bundle.sequence;
        result.bundle = bundle;
        return result;
    }

    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override
    {
        bundle = request.bundle;
        ++published;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId offerId) override
    {
        if (bundle.offerId == offerId)
            bundle = {};
        return protocol::ResponseStatus::Ok;
    }

    modules::clipboard::TransferSourceBundle bundle;
    int published = 0;
};

class RuntimeMemoryRangeSink
    : public modules::clipboard::ITransferFileRangeSink
{
public:
    modules::clipboard::TransferFileDrainSinkResult writeRange(
        const modules::clipboard::TransferFileRangeRequest& request,
        const protocol::ByteBuffer& bytes,
        bool endOfFile) override
    {
        offsets.push_back(request.offset);
        eofFlags.push_back(endOfFile);
        content.insert(content.end(), bytes.begin(), bytes.end());
        return {};
    }

    protocol::ByteBuffer content;
    std::vector<std::uint64_t> offsets;
    std::vector<bool> eofFlags;
};

class DenyClipboardRuntimePolicy
    : public runtime::feature::IClipboardRuntimePolicy
{
public:
    runtime::feature::ClipboardRuntimePolicyDecision authorize(
        const runtime::feature::ClipboardRuntimePolicyContext& context) override
    {
        if (context.operation ==
            runtime::feature::ClipboardRuntimeOperation::LocalSnapshotAnnounce) {
            return runtime::feature::ClipboardRuntimePolicyDecision::deny(
                protocol::ResponseStatus::DeniedByPolicy,
                "clipboard announce denied");
        }
        return runtime::feature::ClipboardRuntimePolicyDecision::allow(false);
    }

    void audit(const runtime::feature::ClipboardRuntimeAuditEvent& event) override
    {
        auditEvents.push_back(event);
    }

    std::vector<runtime::feature::ClipboardRuntimeAuditEvent> auditEvents;
};

modules::clipboard::TransferSourceBundle textBundle()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.formatId = 55;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::NativePassthrough;

    modules::clipboard::MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("hello");

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.origin = modules::clipboard::TransferOrigin::Clipboard;
    bundle.side = modules::clipboard::TransferSide::Local;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            77,
            std::vector<modules::clipboard::MaterializedTransferEntry>{entry}));
    return bundle;
}

modules::clipboard::TransferSourceBundle remoteFileRangeBundle()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::FdclFileListFormat;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.canStream = true;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.origin = modules::clipboard::TransferOrigin::Clipboard;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.originSessionId = 999;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::RemoteFdclTransferSource>(
            77,
            std::vector<modules::clipboard::TransferFormatDescriptor>{
                descriptor}));
    return bundle;
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
                                 protocol::MessageKind::Cancel};
    return capabilities;
}

runtime::RuntimeOptions makeRuntimeOptions()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "clipboard-runtime-service-smoke";
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

std::shared_ptr<RecordingChannel> bindSmallData(session::Session& session)
{
    const network::ChannelKey smallData{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
    std::shared_ptr<RecordingChannel> channel =
        std::make_shared<RecordingChannel>(smallData);
    assert(session.network()->bindChannel(channel).ok);
    network::ChannelReadyInfo ready;
    ready.endpoint = "clipboard-runtime-smoke";
    assert(session.network()->markReady(smallData, ready).ok);
    return channel;
}

session::Session* createClient(runtime::RuntimeHost& host,
                               std::shared_ptr<FakeClipboardEndpoint> endpoint,
                               std::shared_ptr<RecordingChannel>* channel)
{
    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    assert(client != nullptr);
    assert(client->start());
    *channel = bindSmallData(*client);

    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardEndpoint = endpoint;
    assert(host.mountProfileModules(*client, dependencies).ok());
    assert(allStarted(client->moduleHost()->startAllowedModules()));
    return client;
}

void installRemoteOffer(modules::clipboard::ClipboardModuleBase& module,
                        modules::clipboard::TransferSourceBundle bundle)
{
    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Event;
    packet.payload = modules::clipboard::encodeFdclFormatList(
        modules::clipboard::makeFormatListFromBundle(bundle));
    module.handlePacket(packet);
    assert(module.snapshot().remoteBundle.offerId == bundle.offerId);
}

void runtimeServiceAnnouncesEndpointSnapshots()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    endpoint->bundle = textBundle();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    runtime::feature::ClipboardRuntimeServiceOptions options;
    options.session = client;
    options.endpoint = endpoint;
    runtime::feature::ClipboardRuntimeService service(options);
    assert(service.start().ok);

    runtime::feature::ClipboardRuntimePumpResult pump = service.pumpOnce();
    assert(pump.active);
    assert(pump.missingEndpoints == 0);
    assert(pump.missingModules == 0);
    assert(pump.policyDenials == 0);
    assert(pump.sendFailures == 0);
    assert(pump.announcementsSent == 1);
    assert(channel->sentPackets.size() == 1);
    assert(channel->sentPackets.back().packetType ==
           protocol::PacketType::Clipboard);

    pump = service.pumpOnce();
    assert(pump.duplicateSnapshots == 1);
    assert(channel->sentPackets.size() == 1);

    const runtime::feature::ClipboardRuntimeServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.announcementsSent == 1);
    assert(snapshot.duplicateSnapshots == 1);
    assert(snapshot.lastOfferId == 22);
}

void runtimeServiceAppliesPolicyBeforeAnnounce()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    endpoint->bundle = textBundle();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto policy = std::make_shared<DenyClipboardRuntimePolicy>();
    runtime::feature::ClipboardRuntimeServiceOptions options;
    options.session = client;
    options.endpoint = endpoint;
    options.policy = policy;
    runtime::feature::ClipboardRuntimeService service(options);
    assert(service.start().ok);

    const runtime::feature::ClipboardRuntimePumpResult pump =
        service.pumpOnce();
    assert(pump.policyDenials == 1);
    assert(channel->sentPackets.empty());
    assert(policy->auditEvents.size() == 1);
    assert(policy->auditEvents.front().context.offerId == 22);
}

void configurableRuntimePolicyAuditsAllowedAnnounce()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    endpoint->bundle = textBundle();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    runtime::feature::ClipboardRuntimePolicyRules rules;
    rules.auditAllowed = true;
    auto policy =
        std::make_shared<runtime::feature::ConfigurableClipboardRuntimePolicy>(
            rules);
    runtime::feature::ClipboardRuntimeServiceOptions options;
    options.session = client;
    options.endpoint = endpoint;
    options.policy = policy;
    runtime::feature::ClipboardRuntimeService service(options);
    assert(service.start().ok);

    const runtime::feature::ClipboardRuntimePumpResult pump =
        service.pumpOnce();
    assert(pump.announcementsSent == 1);
    assert(service.snapshot().auditEvents == 1);

    const runtime::feature::ClipboardRuntimePolicySnapshot snapshot =
        policy->snapshot();
    assert(snapshot.authorizeCalls == 1);
    assert(snapshot.allowed == 1);
    assert(snapshot.denied == 0);
    assert(snapshot.auditEvents == 1);
    assert(snapshot.auditedAllowed == 1);
    assert(snapshot.lastAllowed);
    assert(snapshot.lastOperation ==
           runtime::feature::ClipboardRuntimeOperation::LocalSnapshotAnnounce);
    assert(snapshot.lastOfferId == 22);
}

void configurableRuntimePolicyKeepsBoundedRecentAuditEvents()
{
    runtime::feature::ClipboardRuntimePolicyRules rules;
    rules.auditAllowed = true;
    rules.maxRecentAuditEvents = 2;
    runtime::feature::ConfigurableClipboardRuntimePolicy policy(rules);

    for (std::uint64_t index = 0; index < 3; ++index) {
        runtime::feature::ClipboardRuntimePolicyContext context;
        context.sessionId = 8100 + index;
        context.traceId = 9100 + index;
        context.role = session::SessionRole::Client;
        context.moduleId = "clipboard.redirect.client";
        context.policyVersion = "test-policy";
        context.operation =
            runtime::feature::ClipboardRuntimeOperation::RemoteFileRangeRead;
        context.bundleId = 100 + index;
        context.offerId = 200 + index;
        context.ownerEpoch = 300 + index;
        context.sequence = 400 + index;
        context.objectId = 500 + index;
        context.requestedBytes = 600 + index;
        context.fileIndex = static_cast<std::uint32_t>(index);
        context.formatCount = 1;
        context.canonicalFormat = modules::clipboard::FdclFileListFormat;

        const runtime::feature::ClipboardRuntimePolicyDecision decision =
            policy.authorize(context);
        assert(decision.allowed);
        assert(decision.auditRequired);

        runtime::feature::ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = decision.allowed;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        policy.audit(event);
    }

    runtime::feature::ClipboardRuntimePolicySnapshot snapshot =
        policy.snapshot();
    assert(snapshot.authorizeCalls == 3);
    assert(snapshot.auditEvents == 3);
    assert(snapshot.auditedAllowed == 3);
    assert(snapshot.recentAuditEvents.size() == 2);
    assert(snapshot.recentAuditEvents.front().context.offerId == 201);
    assert(snapshot.recentAuditEvents.front().context.requestedBytes == 601);
    assert(snapshot.recentAuditEvents.back().context.offerId == 202);
    assert(snapshot.recentAuditEvents.back().context.canonicalFormat ==
           modules::clipboard::FdclFileListFormat);

    runtime::feature::ClipboardRuntimePolicyRules noRecentRules;
    noRecentRules.auditAllowed = true;
    noRecentRules.maxRecentAuditEvents = 0;
    runtime::feature::ConfigurableClipboardRuntimePolicy noRecentPolicy(
        noRecentRules);

    runtime::feature::ClipboardRuntimePolicyContext context;
    context.operation =
        runtime::feature::ClipboardRuntimeOperation::LocalSnapshotAnnounce;
    const runtime::feature::ClipboardRuntimePolicyDecision decision =
        noRecentPolicy.authorize(context);
    runtime::feature::ClipboardRuntimeAuditEvent event;
    event.context = context;
    event.allowed = decision.allowed;
    event.responseStatus = decision.responseStatus;
    event.reason = decision.reason;
    noRecentPolicy.audit(event);
    snapshot = noRecentPolicy.snapshot();
    assert(snapshot.auditEvents == 1);
    assert(snapshot.recentAuditEvents.empty());
}

void configurableRuntimePolicyDeniesRemoteFileRangeBeforeDispatch()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);
    installRemoteOffer(*module, remoteFileRangeBundle());

    TimeoutReadPump pump;
    runtime::feature::ClipboardRuntimePolicyRules rules;
    rules.allowRemoteFileRangeRead = false;
    auto policy =
        std::make_shared<runtime::feature::ConfigurableClipboardRuntimePolicy>(
            rules);
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    readerOptions.policy = policy;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    modules::clipboard::TransferFileRangeRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.objectId = 909;
    request.fileIndex = 0;
    request.offset = 4;
    request.requestedBytes = 5;

    const modules::clipboard::TransferFileRangeResult result =
        reader.readRemoteFileRange(request, 100);
    assert(result.status == protocol::ResponseStatus::DeniedByPolicy);
    assert(channel->sentPackets.empty());

    const runtime::feature::ClipboardRuntimePolicySnapshot snapshot =
        policy->snapshot();
    assert(snapshot.authorizeCalls == 1);
    assert(snapshot.denied == 1);
    assert(snapshot.auditEvents == 1);
    assert(snapshot.auditedDenied == 1);
    assert(!snapshot.lastAllowed);
    assert(snapshot.lastOperation ==
           runtime::feature::ClipboardRuntimeOperation::RemoteFileRangeRead);
    assert(snapshot.lastOfferId == 22);
    assert(snapshot.lastObjectId == 909);
    assert(snapshot.lastRequestedBytes == 5);
    assert(snapshot.lastCanonicalFormat ==
           modules::clipboard::FdclFileListFormat);
}

void clipboardProductPresenterReportsStableStates()
{
    runtime::feature::ClipboardRuntimeServiceSnapshot runtimeSnapshot;
    runtime::feature::ClipboardProductHealthPresentation presentation =
        runtime::feature::buildClipboardProductHealthPresentation(
            runtimeSnapshot);
    assert(!presentation.usable);
    assert(presentation.health ==
           runtime::feature::ClipboardProductHealthLevel::Blocked);
    assert(presentation.statusCode == "clipboard.runtime_stopped");
    assert(presentation.primaryActionCode == "clipboard.start_runtime");
    assert(presentation.runtimeState == "runtime.stopped");
    assert(presentation.policyState == "policy.none");

    runtimeSnapshot.active = true;
    runtimeSnapshot.endpointAttached = true;
    runtimeSnapshot.announcementsSent = 1;
    runtimeSnapshot.lastOfferId = 22;
    runtime::feature::ClipboardRuntimePolicySnapshot policySnapshot;
    policySnapshot.authorizeCalls = 2;
    policySnapshot.denied = 1;
    policySnapshot.auditEvents = 1;
    policySnapshot.lastAllowed = false;
    policySnapshot.lastOperation =
        runtime::feature::ClipboardRuntimeOperation::RemoteFileRangeRead;
    policySnapshot.lastReason = "file range disabled";

    presentation =
        runtime::feature::buildClipboardProductHealthPresentation(
            runtimeSnapshot,
            policySnapshot);
    assert(presentation.usable);
    assert(presentation.health ==
           runtime::feature::ClipboardProductHealthLevel::Warning);
    assert(presentation.statusCode == "clipboard.policy_limited");
    assert(presentation.primaryActionCode ==
           "policy.review_clipboard_rules");
    assert(presentation.runtimeState == "runtime.announced");
    assert(presentation.policyState ==
           "policy.denied.remote_file_range_read");
    assert(presentation.showPolicyDenialWarning);
    assert(presentation.showAuditIndicator);
    assert(!presentation.detailMessages.empty());

    runtimeSnapshot.policyDenials = 0;
    policySnapshot.denied = 0;
    runtimeSnapshot.sendFailures = 1;
    presentation =
        runtime::feature::buildClipboardProductHealthPresentation(
            runtimeSnapshot,
            policySnapshot);
    assert(presentation.health ==
           runtime::feature::ClipboardProductHealthLevel::Degraded);
    assert(presentation.statusCode == "clipboard.send_degraded");
    assert(presentation.primaryActionCode == "clipboard.inspect_transport");
    assert(presentation.showTransferWarning);
}

void clipboardProductPresenterReportsPolicyStates()
{
    runtime::ProductClipboardPolicy policy;
    runtime::feature::ClipboardProductPolicyPresentation presentation =
        runtime::feature::buildClipboardProductPolicyPresentation(policy);
    assert(presentation.usable);
    assert(presentation.modeCode == "clipboard.policy.open");
    assert(presentation.primaryActionCode == "none");
    assert(presentation.directionState == "direction.bidirectional");
    assert(presentation.contentState == "content.standard");
    assert(presentation.fileState == "file.contents_enabled");
    assert(presentation.dragState == "drag.enabled");
    assert(presentation.runtimeState == "runtime.open");
    assert(presentation.auditState == "audit.disabled");
    assert(presentation.allowPlainText);
    assert(presentation.allowRichText);
    assert(presentation.allowImage);
    assert(presentation.allowFiles);
    assert(presentation.allowFileContents);
    assert(presentation.allowDrag);
    assert(!presentation.allowCustomFormats);
    assert(!presentation.showRestrictionWarning);

    policy.modulePolicy.allowFileContents = false;
    policy.modulePolicy.allowImage = false;
    policy.runtimeRules.auditAllowed = true;
    policy.runtimeRules.allowRemoteFormatRead = false;
    policy.runtimeRules.denialReason = "enterprise";
    presentation =
        runtime::feature::buildClipboardProductPolicyPresentation(policy);
    assert(presentation.usable);
    assert(presentation.modeCode == "clipboard.policy.restricted");
    assert(presentation.primaryActionCode == "policy.review_formats");
    assert(presentation.contentState == "content.restricted");
    assert(presentation.fileState == "file.metadata_only");
    assert(presentation.runtimeState == "runtime.clipboard_limited");
    assert(presentation.auditState == "audit.enabled");
    assert(!presentation.allowImage);
    assert(!presentation.allowFileContents);
    assert(presentation.auditEnabled);
    assert(presentation.showRestrictionWarning);
    assert(presentation.showFileTransferWarning);
    assert(presentation.showAuditIndicator);
    assert(!presentation.detailMessages.empty());

    policy.modulePolicy.allowAnnounce = false;
    policy.modulePolicy.allowReceive = false;
    policy.modulePolicy.allowSendContent = false;
    policy.modulePolicy.allowWriteLocal = false;
    presentation =
        runtime::feature::buildClipboardProductPolicyPresentation(policy);
    assert(!presentation.usable);
    assert(presentation.modeCode == "clipboard.policy.blocked");
    assert(presentation.primaryActionCode == "policy.enable_clipboard");
    assert(presentation.directionState == "direction.blocked");
}

void runtimeServiceExpiresModulePendingReads()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    endpoint->bundle = textBundle();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);

    modules::clipboard::FdclReadFormatRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.itemIndex = 0;
    request.formatId = 55;
    request.localFormatToken = 0;
    request.acceptedMaxBytes = 1024;
    request.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    assert(module->requestRemoteFormat(request, 10, 1000000));
    assert(module->snapshot().pendingReads == 1);

    runtime::feature::ClipboardRuntimeServiceOptions options;
    options.session = client;
    options.endpoint = endpoint;
    runtime::feature::ClipboardRuntimeService service(options);
    assert(service.start().ok);
    const runtime::feature::ClipboardRuntimeExpiryResult expired =
        service.expirePendingReads(1010001);
    assert(expired.expiredReads == 1);
    assert(module->snapshot().pendingReads == 0);
}

void runtimeRemoteReaderReturnsModuleReadResponse()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);

    ImmediateReadResponsePump pump(*module, *channel);
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    modules::clipboard::TransferReadRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.itemIndex = 0;
    request.formatId = 55;
    request.localFormatToken = 0;
    request.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    request.acceptedMaxBytes = 1024;
    request.requestedEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;

    const modules::clipboard::TransferReadResult result =
        reader.readRemoteFormat(request, 100);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.canonicalFormat == modules::clipboard::TextPlainUtf8Format);
    assert(result.encoding ==
           modules::clipboard::TransferEncodingMode::CanonicalBytes);
    assert(result.bytes == bytes("remote response"));
    assert(pump.responded);
    assert(pump.pumps > 0);
    assert(module->snapshot().pendingReads == 0);
    assert(module->snapshot().lastReadResponseTo != 0);
}

void runtimeRemoteReaderSendsCancelOnTimeout()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);

    TimeoutReadPump pump;
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    modules::clipboard::TransferReadRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.itemIndex = 0;
    request.formatId = 55;
    request.localFormatToken = 0;
    request.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    request.acceptedMaxBytes = 1024;
    request.requestedEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;

    const modules::clipboard::TransferReadResult result =
        reader.readRemoteFormat(request, 1);
    assert(result.status == protocol::ResponseStatus::Timeout);
    assert(channel->sentPackets.size() >= 2);

    const protocol::PacketEnvelope& requestPacket =
        channel->sentPackets.front();
    const protocol::PacketEnvelope& cancelPacket =
        channel->sentPackets.back();
    assert(cancelPacket.messageKind == protocol::MessageKind::Cancel);
    assert(cancelPacket.correlationId == requestPacket.messageId);

    const modules::clipboard::FdclDecodeResult decoded =
        modules::clipboard::decodeFdclPayload(cancelPacket.payload);
    assert(decoded.ok);
    assert(decoded.operation == modules::clipboard::FdclOperation::Cancel);
    assert(decoded.cancel.correlationId == requestPacket.messageId);
    assert(decoded.cancel.reason ==
           modules::clipboard::FdclCancelReason::Timeout);
    assert(module->snapshot().cancelsSent == 1);
}

void runtimeRemoteReaderReturnsModuleFileRangeResponse()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);
    installRemoteOffer(*module, remoteFileRangeBundle());

    ImmediateFileRangeResponsePump pump(*module, *channel);
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    modules::clipboard::TransferFileRangeRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.objectId = 909;
    request.fileIndex = 0;
    request.offset = 4;
    request.requestedBytes = 5;

    const modules::clipboard::TransferFileRangeResult result =
        reader.readRemoteFileRange(request, 100);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytes == bytes("range"));
    assert(result.endOfFile);
    assert(pump.responded);
    assert(pump.pumps > 0);
    assert(module->snapshot().pendingReads == 0);
    assert(module->snapshot().lastFileRangeResponseTo != 0);
}

void runtimeRemoteReaderDrainsFileRangesThroughModule()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);
    installRemoteOffer(*module, remoteFileRangeBundle());

    ChunkedFileRangeResponsePump pump(*module,
                                      *channel,
                                      bytes("abcdefghijkl"));
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    modules::clipboard::TransferFileRangeRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.objectId = 909;
    request.fileIndex = 0;

    modules::clipboard::TransferFileDrainOptions options;
    options.chunkBytes = 5;
    options.timeoutMs = 100;

    RuntimeMemoryRangeSink sink;
    const modules::clipboard::TransferFileDrainResult result =
        modules::clipboard::drainRemoteFileRange(reader,
                                                 request,
                                                 sink,
                                                 options);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytesWritten == 12);
    assert(result.chunksWritten == 3);
    assert(result.endOfFile);
    assert(sink.content == bytes("abcdefghijkl"));
    assert(pump.respondedMessageIds.size() == 3);
    assert(pump.offsets.size() == 3);
    assert(pump.offsets[0] == 0);
    assert(pump.offsets[1] == 5);
    assert(pump.offsets[2] == 10);
    assert(pump.requestedBytes[0] == 5);
    assert(pump.requestedBytes[1] == 5);
    assert(pump.requestedBytes[2] == 5);
    const modules::clipboard::ClipboardModuleSnapshot snapshot =
        module->snapshot();
    assert(snapshot.pendingReads == 0);
    assert(snapshot.fileRangeResponsesReceived == 3);
    assert(snapshot.fileRangeBytesReceived == 12);
}

} // namespace

int main()
{
    runtimeServiceAnnouncesEndpointSnapshots();
    runtimeServiceAppliesPolicyBeforeAnnounce();
    configurableRuntimePolicyAuditsAllowedAnnounce();
    configurableRuntimePolicyKeepsBoundedRecentAuditEvents();
    configurableRuntimePolicyDeniesRemoteFileRangeBeforeDispatch();
    clipboardProductPresenterReportsStableStates();
    clipboardProductPresenterReportsPolicyStates();
    runtimeServiceExpiresModulePendingReads();
    runtimeRemoteReaderReturnsModuleReadResponse();
    runtimeRemoteReaderSendsCancelOnTimeout();
    runtimeRemoteReaderReturnsModuleFileRangeResponse();
    runtimeRemoteReaderDrainsFileRangesThroughModule();
    return 0;
}
