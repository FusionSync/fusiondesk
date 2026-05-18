#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;

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
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    network::ChannelKey key_;
};

class FakeClipboardEndpoint final : public modules::clipboard::IClipboardEndpoint
{
public:
    modules::clipboard::ClipboardSnapshot snapshot() override
    {
        modules::clipboard::ClipboardSnapshot result;
        result.bundle = bundle;
        result.ownerEpoch = bundle.ownerEpoch;
        result.sequence = bundle.sequence;
        return result;
    }

    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override
    {
        bundle = request.bundle;
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
};

class ImmediateObjectLockResponsePump final
    : public runtime::feature::IClipboardRuntimeReadPump
{
public:
    ImmediateObjectLockResponsePump(
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
        assert(decoded.operation == modules::clipboard::FdclOperation::LockObject ||
               decoded.operation == modules::clipboard::FdclOperation::UnlockObject);

        modules::clipboard::FdclObjectLock payload = decoded.objectLock;
        if (decoded.operation == modules::clipboard::FdclOperation::LockObject)
            payload.lockId = 99001;
        payload.leaseUsec = payload.leaseUsec == 0 ? 30000000 : payload.leaseUsec;

        protocol::PacketEnvelope response = requestPacket;
        response.messageKind = protocol::MessageKind::Response;
        response.responseStatus = protocol::ResponseStatus::Ok;
        response.responseTo = requestPacket.messageId;
        response.messageId = requestPacket.messageId + 1000;
        response.payload =
            decoded.operation == modules::clipboard::FdclOperation::UnlockObject
                ? modules::clipboard::encodeFdclUnlockObject(payload)
                : modules::clipboard::encodeFdclLockObject(payload);
        module_.handlePacket(response);
        respondedMessageIds.push_back(requestPacket.messageId);
    }

    std::uint64_t monotonicNowUsec() const override
    {
        return nowUsec;
    }

    modules::clipboard::ClipboardModuleBase& module_;
    RecordingChannel& channel_;
    std::vector<protocol::MessageId> respondedMessageIds;
    std::uint64_t nowUsec = 1000000;
    int pumps = 0;
};

class DenyObjectLockPolicy final
    : public runtime::feature::IClipboardRuntimePolicy
{
public:
    runtime::feature::ClipboardRuntimePolicyDecision authorize(
        const runtime::feature::ClipboardRuntimePolicyContext& context) override
    {
        if (context.operation ==
            runtime::feature::ClipboardRuntimeOperation::RemoteObjectLock) {
            return runtime::feature::ClipboardRuntimePolicyDecision::deny(
                protocol::ResponseStatus::DeniedByPolicy,
                "object lock denied");
        }
        return runtime::feature::ClipboardRuntimePolicyDecision::allow(false);
    }

    void audit(const runtime::feature::ClipboardRuntimeAuditEvent& event) override
    {
        auditEvents.push_back(event);
    }

    std::vector<runtime::feature::ClipboardRuntimeAuditEvent> auditEvents;
};

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
    options.profile.profileId = "clipboard-runtime-object-lock-smoke";
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
    ready.endpoint = "clipboard-runtime-object-lock-smoke";
    assert(session.network()->markReady(smallData, ready).ok);
    return channel;
}

session::Session* createClient(runtime::RuntimeHost& host,
                               std::shared_ptr<RecordingChannel>* channel)
{
    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    assert(client != nullptr);
    assert(client->start());
    *channel = bindSmallData(*client);

    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardEndpoint = std::make_shared<FakeClipboardEndpoint>();
    assert(host.mountProfileModules(*client, dependencies).ok());
    assert(allStarted(client->moduleHost()->startAllowedModules()));
    return client;
}

modules::clipboard::TransferSourceBundle remoteFileObjectBundle()
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

void installRemoteOffer(modules::clipboard::ClipboardModuleBase& module)
{
    const modules::clipboard::TransferSourceBundle bundle =
        remoteFileObjectBundle();
    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Event;
    packet.payload = modules::clipboard::encodeFdclFormatList(
        modules::clipboard::makeFormatListFromBundle(bundle));
    module.handlePacket(packet);
    assert(module.snapshot().remoteBundle.offerId == bundle.offerId);
}

modules::clipboard::TransferObjectLockRequest objectLockRequest()
{
    modules::clipboard::TransferObjectLockRequest request;
    request.bundleId = 11;
    request.offerId = 22;
    request.ownerEpoch = 33;
    request.sourceId = 77;
    request.objectId = 909;
    request.fileIndex = 0;
    request.leaseUsec = 30000000;
    return request;
}

void runtimeRemoteReaderLocksAndUnlocksRemoteObject()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);
    installRemoteOffer(*module);

    ImmediateObjectLockResponsePump pump(*module, *channel);
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    modules::clipboard::TransferObjectLockRequest request =
        objectLockRequest();
    const modules::clipboard::TransferObjectLockResult lock =
        reader.lockRemoteObject(request, 100);
    assert(lock.ok());
    assert(lock.lockId == 99001);
    assert(lock.leaseUsec == 30000000);
    assert(module->snapshot().objectLockRequestsSent == 1);
    assert(module->snapshot().objectLockResponsesReceived == 1);

    request.lockId = lock.lockId;
    const modules::clipboard::TransferObjectLockResult unlock =
        reader.unlockRemoteObject(request, 100);
    assert(unlock.ok());
    assert(unlock.lockId == lock.lockId);
    assert(module->snapshot().objectUnlockRequestsSent == 1);
    assert(module->snapshot().objectUnlockResponsesReceived == 1);
    assert(channel->sentPackets.size() == 2);
}

void runtimePolicyCanDenyObjectLock()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, &channel);

    auto* module = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        client->moduleHost()->module("clipboard.redirect.client"));
    assert(module != nullptr);
    installRemoteOffer(*module);

    ImmediateObjectLockResponsePump pump(*module, *channel);
    auto policy = std::make_shared<DenyObjectLockPolicy>();
    runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
    readerOptions.session = client;
    readerOptions.pump = &pump;
    readerOptions.policy = policy;
    runtime::feature::ClipboardRuntimeRemoteReader reader(readerOptions);

    const modules::clipboard::TransferObjectLockResult lock =
        reader.lockRemoteObject(objectLockRequest(), 100);
    assert(lock.status == protocol::ResponseStatus::DeniedByPolicy);
    assert(lock.message == "object lock denied");
    assert(channel->sentPackets.empty());
    assert(policy->auditEvents.size() == 1);
    assert(policy->auditEvents.front().context.operation ==
           runtime::feature::ClipboardRuntimeOperation::RemoteObjectLock);
}

} // namespace

int main()
{
    runtimeRemoteReaderLocksAndUnlocksRemoteObject();
    runtimePolicyCanDenyObjectLock();
    return 0;
}
