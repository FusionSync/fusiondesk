#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/module/module_catalog.h"
#include "fusiondesk/modules/display/display_interfaces.h"
#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/modules/input/input_factory.h"
#include "fusiondesk/modules/input/input_interfaces.h"
#include "fusiondesk/runtime/connection/module_inventory_exchange.h"
#include "fusiondesk/runtime/session/display_product_health_presenter.h"
#include "fusiondesk/runtime/session/session_mainline.h"
#include "fusiondesk/runtime/session/session_runtime_diagnostics.h"

using namespace fusiondesk;

namespace {

class OpenChannel : public network::IChannel
{
public:
    explicit OpenChannel(network::ChannelKey key)
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

class FakeCapture : public modules::display::IDisplayCapture
{
public:
    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        lastStatus_.code = modules::display::DisplayCaptureStatusCode::Ok;
        lastStatus_.message = "session-mainline-capture-ok";
        modules::display::CapturedFrame frame;
        frame.frameId = ++frames;
        frame.keyFrame = keyFrame;
        frame.pixels = {1, 2, 3, 4};
        return frame;
    }

    modules::display::DisplayCaptureStatus lastStatus() const override
    {
        return lastStatus_;
    }

    std::string backendId() const override
    {
        return "session.fake.capture";
    }

    int captureErrors() const override
    {
        return captureErrorCount;
    }

    std::uint64_t frames = 0;
    int captureErrorCount = 2;
    modules::display::DisplayCaptureStatus lastStatus_;
};

class FakeEncoder : public modules::display::IVideoEncoder
{
public:
    modules::display::EncodedFrame encode(
        const modules::display::CapturedFrame& frame) override
    {
        modules::display::EncodedFrame encoded;
        encoded.frameId = frame.frameId;
        encoded.keyFrame = frame.keyFrame;
        encoded.payload = frame.pixels;
        return encoded;
    }
};

class FakeInputInjector : public modules::input::IInputInjector
{
public:
    bool injectMouse(const modules::input::MouseInputEvent& event) override
    {
        lastMouse = event;
        ++mouseEvents;
        return true;
    }

    bool injectKeyboard(const modules::input::KeyboardInputEvent& event) override
    {
        lastKeyboard = event;
        ++keyboardEvents;
        return true;
    }

    int mouseEvents = 0;
    int keyboardEvents = 0;
    modules::input::MouseInputEvent lastMouse;
    modules::input::KeyboardInputEvent lastKeyboard;
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
                                protocol::PacketType::Video,
                                protocol::PacketType::Mouse,
                                protocol::PacketType::Keyboard};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Ack,
                                 protocol::MessageKind::Error};
    return capabilities;
}

runtime::RuntimeOptions makeRuntimeOptions()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "minimal-session-mainline";
    options.profile.defaultFeatures.bits =
        protocol::feature::Display |
        protocol::feature::Screen |
        protocol::feature::Mouse |
        protocol::feature::Keyboard;
    options.profile.requiredModules = {
        "display.screen",
        "input.mouse",
        "input.keyboard",
    };
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    return options;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "mainline-user";
    options.context.tenantId = "mainline-tenant";
    options.context.clientDeviceId = "mainline-client";
    options.context.agentDeviceId = "mainline-agent";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

network::ChannelKey controlKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey smallDataKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
}

network::ChannelKey mainScreenKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
}

runtime::SessionMainlineChannel readyChannel(network::ChannelKey key)
{
    runtime::SessionMainlineChannel channel;
    channel.channel = std::make_shared<OpenChannel>(key);
    channel.ready.endpoint = "session-mainline-smoke";
    return channel;
}

bool contains(const std::vector<std::string>& values, const std::string& expected)
{
    for (const std::string& value : values) {
        if (value == expected)
            return true;
    }
    return false;
}

modules::display::DisplayCodecRuntimeInfo fakeRawCodecInfo(
    const std::string& fallbackReason = {})
{
    modules::display::DisplayCodecRuntimeInfo info;
    info.selected = true;
    info.adapterId = "session.fake.raw";
    info.codec = "raw_bgra";
    info.backend = "raw_frame";
    info.fallback = true;
    info.lowLatency = true;
    info.selectionMode = "test";
    info.fallbackReason = fallbackReason;
    return info;
}

module::ModuleManifest peerManifestForAgentModule(const std::string& agentModuleId)
{
    if (agentModuleId == "display.screen.agent")
        return module::catalog::displayScreenClient();
    if (agentModuleId == "input.mouse.agent")
        return modules::input::inputClientManifest(modules::input::InputModuleKind::Mouse);
    if (agentModuleId == "input.keyboard.agent")
        return modules::input::inputClientManifest(modules::input::InputModuleKind::Keyboard);
    return {};
}

module::ModuleStartOptions compatibleAgentStartOptions()
{
    const runtime::connection::ModuleInventory peerInventory =
        runtime::connection::makeModuleInventory(
            5101,
            {peerManifestForAgentModule("display.screen.agent"),
             peerManifestForAgentModule("input.mouse.agent"),
             peerManifestForAgentModule("input.keyboard.agent")});
    module::ModuleStartOptions options;
    options.peerVersions =
        runtime::connection::peerVersionsFromModuleInventory(peerInventory);
    return options;
}

void startsAgentMainlineWithNetworkAndMultipleModules()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    auto capture = std::make_shared<FakeCapture>();
    auto encoder = std::make_shared<FakeEncoder>();
    auto injector = std::make_shared<FakeInputInjector>();

    runtime::SessionMainlineOptions options;
    options.host = &host;
    options.role = session::SessionRole::Agent;
    options.sessionOptions = makeSessionOptions(host);
    options.moduleDependencies.capture = capture;
    options.moduleDependencies.encoder = encoder;
    options.moduleDependencies.encoderCodec =
        fakeRawCodecInfo("test H.264 backend unavailable");
    options.moduleDependencies.inputInjector = injector;
    options.channels = {
        readyChannel(controlKey()),
        readyChannel(smallDataKey()),
        readyChannel(mainScreenKey()),
    };

    const runtime::SessionMainlineReport report =
        runtime::SessionMainline::start(options);
    assert(report.ok);
    assert(report.session != nullptr);
    assert(report.sessionState == session::SessionState::Running);
    assert(report.channels.size() == 3);
    assert(report.mount.ok());
    assert(report.mount.mountedModules.size() == 3);
    assert(report.moduleStarts.size() == 3);
    assert(report.blockedModules.empty());
    assert(contains(report.startedModules, "display.screen.agent"));
    assert(contains(report.startedModules, "input.mouse.agent"));
    assert(contains(report.startedModules, "input.keyboard.agent"));

    auto* display = dynamic_cast<modules::display::DisplayAgentModule*>(
        report.session->moduleHost()->module("display.screen.agent"));
    assert(display != nullptr);
    assert(display->snapshot().state == module::ModuleState::Running);
    assert(display->snapshot().sentFrames == 1);
    assert(capture->frames == 1);

    const runtime::SessionRuntimeDiagnosticsSnapshot diagnostics =
        runtime::buildSessionRuntimeDiagnostics(host, report.sessionId);
    assert(diagnostics.ok);
    assert(diagnostics.sessionId == report.sessionId);
    assert(diagnostics.sessionState == session::SessionState::Running);
    assert(diagnostics.linkReady);
    assert(diagnostics.blockedChannels == 0);
    assert(diagnostics.mountedModules == 3);
    assert(diagnostics.runningModules == 3);
    assert(diagnostics.displayCaptureStatuses.size() == 1);
    assert(diagnostics.displayCaptureStatuses.front().moduleId ==
           "display.screen.agent");
    assert(diagnostics.displayCaptureStatuses.front().backendId ==
           "session.fake.capture");
    assert(diagnostics.displayCaptureStatuses.front().includeCursor);
    assert(diagnostics.displayCaptureStatuses.front().status.message ==
           "session-mainline-capture-ok");
    assert(diagnostics.displayCaptureStatuses.front().captureErrors == 2);
    assert(diagnostics.displayCodecStatuses.size() == 1);
    assert(diagnostics.displayCodecStatuses.front().moduleId ==
           "display.screen.agent");
    assert(diagnostics.displayCodecStatuses.front().direction == "encode");
    assert(diagnostics.displayCodecStatuses.front().codec.adapterId ==
           "session.fake.raw");
    assert(diagnostics.displayCodecStatuses.front().codec.fallback);
    const runtime::DisplayProductDiagnosticsSnapshot health =
        runtime::buildDisplayProductDiagnostics(diagnostics);
    assert(health.usable);
    assert(health.health == runtime::DisplayProductHealthLevel::Degraded);
    assert(health.displayModules == 1);
    assert(health.runningDisplayModules == 1);
    assert(health.captureBackendId == "session.fake.capture");
    assert(health.codecAdapterId == "session.fake.raw");
    assert(health.codecFallback);
    assert(health.codecFallbackReason == "test H.264 backend unavailable");
    assert(health.codecPayloadBytes == 4);
    const runtime::DisplayProductHealthPresentation presentation =
        runtime::buildDisplayProductHealthPresentation(health);
    assert(presentation.usable);
    assert(presentation.healthName == "degraded");
    assert(presentation.statusCode == "display.capture_degraded");
    assert(presentation.primaryActionCode == "capture.recover_or_failover");
    assert(presentation.captureState == "capture.ok.session.fake.capture");
    assert(presentation.codecState == "codec.selected.session.fake.raw.test");
    assert(presentation.showCodecFallbackWarning);
    assert(!presentation.showCodecLatencyWarning);
    assert(!diagnostics.diagnostics.empty());
}

void reportsBlockedModuleChannelsBeforeStart()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    runtime::SessionMainlineOptions options;
    options.host = &host;
    options.role = session::SessionRole::Agent;
    options.sessionOptions = makeSessionOptions(host);
    options.moduleDependencies.capture = std::make_shared<FakeCapture>();
    options.moduleDependencies.encoder = std::make_shared<FakeEncoder>();
    options.moduleDependencies.encoderCodec = fakeRawCodecInfo();
    options.moduleDependencies.inputInjector = std::make_shared<FakeInputInjector>();
    options.channels = {
        readyChannel(controlKey()),
        readyChannel(smallDataKey()),
    };

    const runtime::SessionMainlineReport report =
        runtime::SessionMainline::start(options);
    assert(!report.ok);
    assert(report.session != nullptr);
    assert(report.mount.ok());
    assert(!report.linkChannels.ok);
    assert(contains(report.blockedModules, "display.screen.agent"));

    bool screenBlocked = false;
    for (const runtime::LinkChannelBindingItem& item : report.linkChannels.channels) {
        if (item.key == mainScreenKey()) {
            screenBlocked = item.blocked;
            assert(item.moduleRequired);
            assert(!item.ready);
        }
    }
    assert(screenBlocked);

    const runtime::SessionRuntimeDiagnosticsSnapshot diagnostics =
        runtime::buildSessionRuntimeDiagnostics(host, report.sessionId);
    assert(diagnostics.ok);
    assert(!diagnostics.linkReady);
    assert(diagnostics.blockedChannels > 0);
    assert(diagnostics.mountedModules == 3);
    assert(diagnostics.runningModules == 0);
    assert(diagnostics.displayCaptureStatuses.size() == 1);
    assert(diagnostics.displayCaptureStatuses.front().backendId ==
           "session.fake.capture");
    assert(diagnostics.displayCaptureStatuses.front().status.code ==
           modules::display::DisplayCaptureStatusCode::NotOpen);
    assert(diagnostics.displayCaptureStatuses.front().captureErrors == 2);
    const runtime::DisplayProductDiagnosticsSnapshot health =
        runtime::buildDisplayProductDiagnostics(diagnostics);
    assert(!health.usable);
    assert(health.health == runtime::DisplayProductHealthLevel::Blocked);
    assert(!health.linkReady);
    assert(health.displayModules == 1);
    assert(health.runningDisplayModules == 0);
    const runtime::DisplayProductHealthPresentation presentation =
        runtime::buildDisplayProductHealthPresentation(health);
    assert(!presentation.usable);
    assert(presentation.healthName == "blocked");
    assert(presentation.statusCode == "display.channel_blocked");
    assert(presentation.primaryActionCode == "network.bind_required_channels");
}

void continuesStartedSessionAfterExternalChannelBinding()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    runtime::SessionMainlineOptions createOptions;
    createOptions.host = &host;
    createOptions.role = session::SessionRole::Agent;
    createOptions.sessionOptions = makeSessionOptions(host);
    createOptions.mountProfileModules = false;
    createOptions.startModules = false;

    const runtime::SessionMainlineReport created =
        runtime::SessionMainline::start(createOptions);
    assert(created.ok);
    assert(created.session != nullptr);
    assert(created.sessionState == session::SessionState::Running);

    network::NetworkManager* network = created.session->network();
    assert(network != nullptr);
    for (const network::ChannelKey& key :
         {controlKey(), smallDataKey(), mainScreenKey()}) {
        const network::ChannelRegistryResult bound =
            network->bindChannel(std::make_shared<OpenChannel>(key));
        assert(bound.ok);
        network::ChannelReadyInfo ready;
        ready.endpoint = "session-mainline-continuation";
        const network::ChannelRegistryResult marked =
            network->markReady(key, ready);
        assert(marked.ok);
    }

    runtime::SessionMainlineModuleOptions moduleOptions;
    moduleOptions.host = &host;
    moduleOptions.session = created.session;
    moduleOptions.moduleDependencies.capture = std::make_shared<FakeCapture>();
    moduleOptions.moduleDependencies.encoder = std::make_shared<FakeEncoder>();
    moduleOptions.moduleDependencies.inputInjector =
        std::make_shared<FakeInputInjector>();

    const runtime::SessionMainlineReport started =
        runtime::SessionMainline::mountAndStart(moduleOptions);
    assert(started.ok);
    assert(started.session == created.session);
    assert(started.linkChannels.ok);
    assert(started.mount.mountedModules.size() == 3);
    assert(started.moduleStarts.size() == 3);
    assert(started.blockedModules.empty());
    assert(contains(started.startedModules, "display.screen.agent"));
    assert(contains(started.startedModules, "input.mouse.agent"));
    assert(contains(started.startedModules, "input.keyboard.agent"));
}

void passesPeerVersionsIntoContinuationModuleStart()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    runtime::SessionMainlineOptions createOptions;
    createOptions.host = &host;
    createOptions.role = session::SessionRole::Agent;
    createOptions.sessionOptions = makeSessionOptions(host);
    createOptions.mountProfileModules = false;
    createOptions.startModules = false;

    const runtime::SessionMainlineReport created =
        runtime::SessionMainline::start(createOptions);
    assert(created.ok);
    assert(created.session != nullptr);

    network::NetworkManager* network = created.session->network();
    assert(network != nullptr);
    for (const network::ChannelKey& key :
         {controlKey(), smallDataKey(), mainScreenKey()}) {
        assert(network->bindChannel(std::make_shared<OpenChannel>(key)).ok);
        network::ChannelReadyInfo ready;
        ready.endpoint = "session-mainline-peer-version";
        assert(network->markReady(key, ready).ok);
    }

    runtime::SessionMainlineModuleOptions mountOptions;
    mountOptions.host = &host;
    mountOptions.session = created.session;
    mountOptions.moduleDependencies.capture = std::make_shared<FakeCapture>();
    mountOptions.moduleDependencies.encoder = std::make_shared<FakeEncoder>();
    mountOptions.moduleDependencies.inputInjector =
        std::make_shared<FakeInputInjector>();
    mountOptions.mountProfileModules = true;
    mountOptions.startModules = false;
    const runtime::SessionMainlineReport mounted =
        runtime::SessionMainline::mountAndStart(mountOptions);
    assert(mounted.mount.ok());
    assert(mounted.moduleStarts.empty());

    module::ModuleStartOptions startOptions = compatibleAgentStartOptions();
    for (module::ModulePeerVersion& peer : startOptions.peerVersions) {
        if (peer.moduleId == "display.screen.client")
            peer.version = module::ModuleVersion{2, 0, 0};
    }

    runtime::SessionMainlineModuleOptions startOnlyOptions;
    startOnlyOptions.host = &host;
    startOnlyOptions.session = created.session;
    startOnlyOptions.mountProfileModules = false;
    startOnlyOptions.startModules = true;
    startOnlyOptions.moduleStartOptions = startOptions;

    const runtime::SessionMainlineReport started =
        runtime::SessionMainline::mountAndStart(startOnlyOptions);
    assert(!started.ok);
    assert(started.linkChannels.ok);
    assert(started.moduleStarts.size() == 3);

    bool displayBlocked = false;
    bool inputSkipped = false;
    for (const module::ModuleStartReport& report : started.moduleStarts) {
        if (report.moduleId == "display.screen.agent") {
            displayBlocked = !report.started &&
                              report.decision.reason == policy::DenyReason::ModuleVersionMismatch;
        }
        if (report.moduleId == "input.mouse.agent")
            inputSkipped = !report.started &&
                           report.decision.reason == policy::DenyReason::RuntimeHealthBlocked;
    }
    assert(displayBlocked);
    assert(inputSkipped);
}

void persistsRemoteModuleInventoryInSessionDiagnostics()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    runtime::SessionMainlineOptions createOptions;
    createOptions.host = &host;
    createOptions.role = session::SessionRole::Client;
    createOptions.sessionOptions = makeSessionOptions(host);
    createOptions.mountProfileModules = false;
    createOptions.startModules = false;

    const runtime::SessionMainlineReport created =
        runtime::SessionMainline::start(createOptions);
    assert(created.ok);
    assert(created.session != nullptr);

    const std::vector<module::ModuleManifest> remoteModules = {
        module::catalog::displayScreenAgent(),
        modules::input::inputAgentManifest(modules::input::InputModuleKind::Mouse),
    };
    created.session->updateRemoteModuleInventory(6201, remoteModules);

    const session::SessionSnapshot sessionSnapshot = created.session->snapshot();
    assert(sessionSnapshot.remoteModuleInventory.peerSessionId == 6201);
    assert(sessionSnapshot.remoteModuleInventory.manifests.size() == 2);
    assert(sessionSnapshot.remoteModuleInventory.manifests.front().moduleId ==
           "display.screen.agent");

    const runtime::SessionRuntimeDiagnosticsSnapshot diagnostics =
        runtime::buildSessionRuntimeDiagnostics(host, created.sessionId);
    assert(diagnostics.ok);
    assert(diagnostics.remoteModuleInventory.peerSessionId == 6201);
    assert(diagnostics.remoteModules == 2);
    assert(diagnostics.remoteModuleInventory.manifests.back().moduleId ==
           "input.mouse.agent");
    assert(diagnostics.session.remoteModuleInventory.manifests.size() == 2);
}

} // namespace

int main()
{
    startsAgentMainlineWithNetworkAndMultipleModules();
    reportsBlockedModuleChannelsBeforeStart();
    continuesStartedSessionAfterExternalChannelBinding();
    passesPeerVersionsIntoContinuationModuleStart();
    persistsRemoteModuleInventoryInSessionDiagnostics();
    return 0;
}
