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

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

class MonitoredClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IClipboardChangeMonitor
{
public:
    modules::clipboard::ClipboardSnapshot snapshot() override
    {
        ++snapshotCalls;
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
        pending = true;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId offerId) override
    {
        if (bundle.offerId == offerId)
            bundle = {};
        pending = true;
        return protocol::ResponseStatus::Ok;
    }

    bool hasPendingClipboardChange() const override
    {
        return pending;
    }

    void markClipboardChangeConsumed() override
    {
        pending = false;
        ++consumed;
    }

    modules::clipboard::TransferSourceBundle bundle;
    bool pending = false;
    int snapshotCalls = 0;
    int consumed = 0;
};

modules::clipboard::TransferSourceBundle textBundle()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.formatId = 55;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;

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
    options.profile.profileId = "clipboard-runtime-change-monitor-smoke";
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

std::shared_ptr<RecordingChannel> bindSmallData(session::Session& session)
{
    const network::ChannelKey smallData{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
    auto channel = std::make_shared<RecordingChannel>(smallData);
    assert(session.network()->bindChannel(channel).ok);
    network::ChannelReadyInfo ready;
    ready.endpoint = "clipboard-runtime-monitor-smoke";
    assert(session.network()->markReady(smallData, ready).ok);
    return channel;
}

session::Session* createClient(
    runtime::RuntimeHost& host,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
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
    const std::vector<module::ModuleStartReport> reports =
        client->moduleHost()->startAllowedModules();
    assert(!reports.empty());
    for (const module::ModuleStartReport& report : reports)
        assert(report.started);
    return client;
}

void runtimeSkipsEndpointSnapshotWhenMonitorIsIdle()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto endpoint = std::make_shared<MonitoredClipboardEndpoint>();
    endpoint->bundle = textBundle();
    std::shared_ptr<RecordingChannel> channel;
    session::Session* client = createClient(host, endpoint, &channel);

    runtime::feature::ClipboardRuntimeServiceOptions options;
    options.session = client;
    options.endpoint = endpoint;
    runtime::feature::ClipboardRuntimeService service(options);
    assert(service.start().ok);

    runtime::feature::ClipboardRuntimePumpResult pump = service.pumpOnce();
    assert(pump.idlePolls == 1);
    assert(endpoint->snapshotCalls == 0);
    assert(channel->sentPackets.empty());

    endpoint->pending = true;
    pump = service.pumpOnce();
    assert(pump.announcementsSent == 1);
    assert(endpoint->snapshotCalls == 1);
    assert(endpoint->consumed == 1);
    assert(!endpoint->pending);
    assert(channel->sentPackets.size() == 1);

    pump = service.pumpOnce();
    assert(pump.idlePolls == 1);
    assert(endpoint->snapshotCalls == 1);
    assert(channel->sentPackets.size() == 1);

    const runtime::feature::ClipboardRuntimeServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.idlePolls == 2);
    assert(snapshot.announcementsSent == 1);
}

} // namespace

int main()
{
    runtimeSkipsEndpointSnapshotWhenMonitorIsIdle();
    return 0;
}
