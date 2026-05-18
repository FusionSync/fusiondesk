#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include "fusiondesk/modules/clipboard/fdcl_codec.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/modules/input/input_modules.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;

namespace {

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
                                 protocol::MessageKind::Event};
    return capabilities;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "user-a";
    options.context.tenantId = "tenant-a";
    options.context.localPlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

bool hasChannelSpec(const std::vector<network::ChannelSpec>& specs,
                    network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : specs) {
        if (spec.key == key)
            return true;
    }
    return false;
}

void initializesRuntimeAndCreatesSession()
{
    runtime::RuntimeHost host;
    assert(host.state() == runtime::RuntimeState::Created);
    assert(host.initialize());
    assert(host.state() == runtime::RuntimeState::Initialized);
    assert(host.profile().profileId == "remote-desktop-display-mvp");
    assert(!host.profile().requiredModules.empty());
    assert(host.profile().minimumChannels.size() == 3);
    assert(host.profile().displayCodecPolicy.codecPreference.size() == 3);
    assert(!host.profile().displayCodecPolicy.enableWindowsMediaFoundationH264);
    assert(!host.diagnostics().events().empty());

    protocol::SessionId id = host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* created = host.sessions().find(id);
    assert(created != nullptr);
    assert(created->network()->registry().snapshots().size() == host.profile().minimumChannels.size());
    assert(created->start());
    assert(created->state() == session::SessionState::Running);
}

void keepsProductDisplayCodecPolicy()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-codec-policy";
    options.profile.defaultFeatures.bits =
        protocol::feature::Display | protocol::feature::Screen;
    options.profile.requiredModules = {"display.screen"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    options.profile.displayCodecPolicy.codecPreference = {
        runtime::display::DisplayCodecId::H264,
        runtime::display::DisplayCodecId::RawBgra};
    options.profile.displayCodecPolicy.enableWindowsMediaFoundationH264 = true;
    options.profile.displayCodecPolicy.selectWindowsMediaFoundationH264 = true;
    options.profile.displayCodecPolicy.enableH264DeltaFrames = true;
    options.profile.displayCodecPolicy.selectionMode = "production";

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    assert(host.profile().displayCodecPolicy.enableWindowsMediaFoundationH264);
    assert(host.profile().displayCodecPolicy.selectWindowsMediaFoundationH264);
    assert(host.profile().displayCodecPolicy.enableH264DeltaFrames);
    assert(host.profile().displayCodecPolicy.selectionMode == "production");
    assert(host.profile().displayCodecPolicy.codecPreference.size() == 2);
}

void keepsProductClipboardPolicy()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-clipboard-policy-profile";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    options.profile.clipboardPolicy.modulePolicy.allowDrag = false;
    options.profile.clipboardPolicy.modulePolicy.allowFileContents = false;
    options.profile.clipboardPolicy.modulePolicy.maxFileRangeBytes = 1234;
    options.profile.clipboardPolicy.runtimeRules.auditAllowed = true;
    options.profile.clipboardPolicy.runtimeRules.allowRemoteFormatRead = false;

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    assert(!host.profile().clipboardPolicy.modulePolicy.allowDrag);
    assert(!host.profile().clipboardPolicy.modulePolicy.allowFileContents);
    assert(host.profile().clipboardPolicy.modulePolicy.maxFileRangeBytes == 1234);
    assert(host.profile().clipboardPolicy.runtimeRules.auditAllowed);
    assert(!host.profile().clipboardPolicy.runtimeRules.allowRemoteFormatRead);
}

void shutsDownRuntime()
{
    runtime::RuntimeHost host;
    assert(host.initialize());
    host.shutdown("test shutdown");
    assert(host.state() == runtime::RuntimeState::Stopped);
}

void mountsFeatureFamilySkeletonsFromProductProfile()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-feature-framework";
    options.profile.defaultFeatures.bits =
        protocol::feature::Audio |
        protocol::feature::Filesystem |
        protocol::feature::Mouse |
        protocol::feature::Camera;
    options.profile.requiredModules = {
        "audio.desktop",
        "filesystem.redirect",
        "input.mouse",
        "camera.redirect",
    };
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    const protocol::SessionId id =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, runtime::DisplayMvpDependencies{});
    assert(report.ok());
    assert(report.mountedModules.size() == options.profile.requiredModules.size());
    assert(session->moduleHost()->module("audio.desktop") != nullptr);
    assert(session->moduleHost()->module("filesystem.redirect") != nullptr);
    assert(session->moduleHost()->module("input.mouse.client") != nullptr);
    assert(session->moduleHost()->module("camera.redirect") != nullptr);
}

void mountsClipboardRedirectThroughRuntimeHost()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-clipboard";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    assert(hasChannelSpec(
        host.profile().minimumChannels,
        network::ChannelKey{
            static_cast<protocol::ChannelId>(
                protocol::ChannelIdValue::LargeData),
            protocol::ChannelType::Standard}));
    const protocol::SessionId id =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, runtime::DisplayMvpDependencies{});
    assert(report.ok());
    assert(report.mountedModules.size() == 1);
    assert(session->moduleHost()->module("clipboard.redirect.client") != nullptr);

    auto* clipboardModule = dynamic_cast<modules::clipboard::ClipboardClientModule*>(
        session->moduleHost()->module("clipboard.redirect.client"));
    assert(clipboardModule != nullptr);
}

class FakeClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IRemoteDragCoordinateSink
{
public:
    modules::clipboard::ClipboardSnapshot snapshot() override
    {
        return {};
    }

    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override
    {
        publishedBundle = request.bundle;
        ++publishes;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId) override
    {
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus dragStart(
        const modules::clipboard::DragSessionStart& start) override
    {
        lastStart = start;
        ++dragStarts;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus dragMove(
        modules::clipboard::DragSessionId,
        const modules::clipboard::DragSurfaceCoordinate&,
        modules::clipboard::TransferAction) override
    {
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus dragDrop(
        modules::clipboard::DragSessionId,
        const modules::clipboard::DragSurfaceCoordinate&,
        modules::clipboard::TransferAction) override
    {
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus dragCancel(
        modules::clipboard::DragSessionId,
        modules::clipboard::DragCancelReason) override
    {
        return protocol::ResponseStatus::Ok;
    }

    modules::clipboard::TransferSourceBundle publishedBundle;
    modules::clipboard::DragSessionStart lastStart;
    int publishes = 0;
    int dragStarts = 0;
};

protocol::PacketEnvelope clipboardEvent(protocol::ByteBuffer payload)
{
    protocol::PacketEnvelope packet;
    packet.packetType = protocol::PacketType::Clipboard;
    packet.messageKind = protocol::MessageKind::Event;
    packet.payload = std::move(payload);
    return packet;
}

modules::clipboard::FdclFormatList runtimeHostDragFormatList()
{
    modules::clipboard::FdclFormatRecord record;
    record.sourceId = 7;
    record.itemIndex = 0;
    record.formatId = 11;
    record.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    record.nativeFormatName = "CF_UNICODETEXT";
    record.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;

    modules::clipboard::FdclFormatList list;
    list.bundleId = 101;
    list.offerId = 202;
    list.ownerEpoch = 303;
    list.sequence = 404;
    list.origin = modules::clipboard::TransferOrigin::Clipboard;
    list.side = modules::clipboard::TransferSide::Local;
    list.originSessionId = 999;
    list.formats = {record};
    return list;
}

void wiresClipboardEndpointAsRuntimeHostDragSink()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-clipboard-drag";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    const protocol::SessionId id =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardEndpoint = endpoint;
    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, dependencies);
    assert(report.ok());

    auto* clipboardModule =
        dynamic_cast<modules::clipboard::ClipboardClientModule*>(
            session->moduleHost()->module("clipboard.redirect.client"));
    assert(clipboardModule != nullptr);
    assert(clipboardModule->start({}));

    clipboardModule->handlePacket(
        clipboardEvent(modules::clipboard::encodeFdclFormatList(
            runtimeHostDragFormatList())));
    assert(endpoint->publishes == 1);

    modules::clipboard::FdclDragStart start;
    start.start.dragSessionId = 7001;
    start.start.bundleId = 101;
    start.start.offerId = 202;
    start.start.ownerEpoch = 303;
    start.start.allowedActions = modules::clipboard::transfer_action::Copy;
    start.start.preferredAction = modules::clipboard::TransferAction::Copy;
    start.start.start.x = 12;
    start.start.start.y = 34;
    start.start.start.surfaceWidth = 1280;
    start.start.start.surfaceHeight = 720;
    clipboardModule->handlePacket(
        clipboardEvent(modules::clipboard::encodeFdclDragStart(start)));

    assert(endpoint->dragStarts == 1);
    assert(endpoint->lastStart.dragSessionId == 7001);
    assert(endpoint->lastStart.start.x == 12);
}

void injectsClipboardPolicyThroughRuntimeHost()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-clipboard-policy";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    const protocol::SessionId id =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    auto policy = std::make_shared<modules::clipboard::ClipboardPolicy>();
    policy->allowDrag = false;
    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardEndpoint = endpoint;
    dependencies.clipboardPolicy = policy;
    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, dependencies);
    assert(report.ok());

    auto* clipboardModule =
        dynamic_cast<modules::clipboard::ClipboardClientModule*>(
            session->moduleHost()->module("clipboard.redirect.client"));
    assert(clipboardModule != nullptr);
    assert(clipboardModule->start({}));

    clipboardModule->handlePacket(
        clipboardEvent(modules::clipboard::encodeFdclFormatList(
            runtimeHostDragFormatList())));
    assert(endpoint->publishes == 1);

    modules::clipboard::FdclDragStart start;
    start.start.dragSessionId = 7001;
    start.start.bundleId = 101;
    start.start.offerId = 202;
    start.start.ownerEpoch = 303;
    start.start.allowedActions = modules::clipboard::transfer_action::Copy;
    start.start.preferredAction = modules::clipboard::TransferAction::Copy;
    start.start.start.surfaceWidth = 1280;
    start.start.start.surfaceHeight = 720;
    clipboardModule->handlePacket(
        clipboardEvent(modules::clipboard::encodeFdclDragStart(start)));

    const modules::clipboard::ClipboardModuleSnapshot snapshot =
        clipboardModule->snapshot();
    assert(endpoint->dragStarts == 0);
    assert(snapshot.formatListsReceived == 1);
    assert(snapshot.policyDenials == 1);
}

void injectsProductClipboardPolicyThroughRuntimeHost()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-product-clipboard-policy";
    options.profile.defaultFeatures.bits = protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    options.profile.clipboardPolicy.modulePolicy.allowDrag = false;

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    const protocol::SessionId id =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    auto endpoint = std::make_shared<FakeClipboardEndpoint>();
    runtime::DisplayMvpDependencies dependencies;
    dependencies.clipboardEndpoint = endpoint;
    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, dependencies);
    assert(report.ok());

    auto* clipboardModule =
        dynamic_cast<modules::clipboard::ClipboardClientModule*>(
            session->moduleHost()->module("clipboard.redirect.client"));
    assert(clipboardModule != nullptr);
    assert(clipboardModule->start({}));

    clipboardModule->handlePacket(
        clipboardEvent(modules::clipboard::encodeFdclFormatList(
            runtimeHostDragFormatList())));
    assert(endpoint->publishes == 1);

    modules::clipboard::FdclDragStart start;
    start.start.dragSessionId = 7002;
    start.start.bundleId = 101;
    start.start.offerId = 202;
    start.start.ownerEpoch = 303;
    start.start.allowedActions = modules::clipboard::transfer_action::Copy;
    start.start.preferredAction = modules::clipboard::TransferAction::Copy;
    start.start.start.surfaceWidth = 1280;
    start.start.start.surfaceHeight = 720;
    clipboardModule->handlePacket(
        clipboardEvent(modules::clipboard::encodeFdclDragStart(start)));

    const modules::clipboard::ClipboardModuleSnapshot snapshot =
        clipboardModule->snapshot();
    assert(endpoint->dragStarts == 0);
    assert(snapshot.policyDenials == 1);
}

class FakeInputInjector : public modules::input::IInputInjector
{
public:
    bool injectMouse(const modules::input::MouseInputEvent& event) override
    {
        lastMouse = event;
        ++mouseEvents;
        return true;
    }

    int mouseEvents = 0;
    modules::input::MouseInputEvent lastMouse;
};

void injectsFeatureAdapterDependenciesThroughRuntimeHost()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-feature-dependencies";
    options.profile.defaultFeatures.bits =
        protocol::feature::Mouse;
    options.profile.requiredModules = {
        "input.mouse",
    };
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    const protocol::SessionId id =
        host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    auto injector = std::make_shared<FakeInputInjector>();
    runtime::DisplayMvpDependencies dependencies;
    dependencies.inputInjector = injector;
    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, dependencies);
    assert(report.ok());

    auto* inputModule = dynamic_cast<modules::input::InputAgentModule*>(
        session->moduleHost()->module("input.mouse.agent"));
    assert(inputModule != nullptr);
}

void reportsProfileModuleVersionFailures()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "remote-desktop-version-gate";
    options.profile.defaultFeatures.bits = protocol::feature::Mouse;
    options.profile.requiredModules = {"input.mouse"};
    options.profile.moduleVersionConstraints.push_back(module::ModuleVersionConstraint{
        "input.mouse.client",
        module::ModuleVersion{2, 0, 0},
        module::ModuleVersion{2, 9, 99}});
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();

    runtime::RuntimeHost host;
    assert(host.initialize(options));
    const protocol::SessionId id =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(id);
    assert(session != nullptr);
    assert(session->start());
    assert(session->moduleHost() != nullptr);

    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, runtime::DisplayMvpDependencies{});
    assert(!report.ok());
    assert(report.mountedModules.empty());
    assert(report.versionFailures.size() == 1);
    assert(session->moduleHost()->module("input.mouse.client") == nullptr);
}

} // namespace

int main()
{
    initializesRuntimeAndCreatesSession();
    keepsProductDisplayCodecPolicy();
    keepsProductClipboardPolicy();
    shutsDownRuntime();
    mountsFeatureFamilySkeletonsFromProductProfile();
    mountsClipboardRedirectThroughRuntimeHost();
    wiresClipboardEndpointAsRuntimeHostDragSink();
    injectsClipboardPolicyThroughRuntimeHost();
    injectsProductClipboardPolicyThroughRuntimeHost();
    injectsFeatureAdapterDependenciesThroughRuntimeHost();
    reportsProfileModuleVersionFailures();
    return 0;
}
