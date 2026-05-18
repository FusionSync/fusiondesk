#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/diagnostics/diagnostics_sink.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"
#include "fusiondesk/modules/display/display_factory.h"
#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;

namespace {

class BridgeChannel : public network::IChannel
{
public:
    BridgeChannel(network::ChannelKey key, network::NetworkRouter* peer)
        : key_(key),
          peer_(peer)
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
        if (peer_ == nullptr)
            return {network::SendStatus::ChannelClosed, "peer router is missing"};

        sentPackets.push_back(packet);
        peer_->submitIncoming(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    network::ChannelKey key_;
    network::NetworkRouter* peer_ = nullptr;
};

class FakeCapture : public modules::display::IDisplayCapture
{
public:
    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        modules::display::CapturedFrame frame;
        frame.frameId = ++frames_;
        frame.keyFrame = keyFrame;
        frame.pixels = {static_cast<std::uint8_t>(frame.frameId), 42};
        return frame;
    }

private:
    std::uint64_t frames_ = 0;
};

class TrackingCapture : public FakeCapture
{
public:
    bool open(const modules::display::DisplayCaptureOpenOptions& options) override
    {
        (void)options;
        opened_ = true;
        ++opens_;
        return true;
    }

    void close() override
    {
        opened_ = false;
        ++closes_;
    }

    bool opened() const
    {
        return opened_;
    }

    int opens() const
    {
        return opens_;
    }

    int closes() const
    {
        return closes_;
    }

private:
    bool opened_ = false;
    int opens_ = 0;
    int closes_ = 0;
};

class ResizingCapture : public modules::display::IDisplayCapture
{
public:
    void setSize(std::uint32_t width, std::uint32_t height)
    {
        width_ = width;
        height_ = height;
    }

    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        modules::display::CapturedFrame frame;
        frame.frameId = ++frames_;
        frame.sourceId = 0;
        frame.keyFrame = keyFrame;
        frame.width = width_;
        frame.height = height_;
        frame.strideBytes = width_ * 4;
        frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
        frame.pixels = {static_cast<std::uint8_t>(frame.frameId), 42};
        return frame;
    }

private:
    std::uint64_t frames_ = 0;
    std::uint32_t width_ = 320;
    std::uint32_t height_ = 180;
};

class FakeEncoder : public modules::display::IVideoEncoder
{
public:
    modules::display::EncodedFrame encode(const modules::display::CapturedFrame& frame) override
    {
        modules::display::EncodedFrame encoded;
        encoded.frameId = frame.frameId;
        encoded.keyFrame = frame.keyFrame;
        encoded.width = frame.width;
        encoded.height = frame.height;
        encoded.strideBytes = frame.strideBytes;
        encoded.pixelFormat = frame.pixelFormat;
        encoded.monotonicTimestampUsec = frame.monotonicTimestampUsec;
        encoded.payload = frame.pixels;
        return encoded;
    }
};

class EmptyEncoder : public modules::display::IVideoEncoder
{
public:
    modules::display::EncodedFrame encode(const modules::display::CapturedFrame& frame) override
    {
        modules::display::EncodedFrame encoded;
        encoded.frameId = frame.frameId;
        encoded.keyFrame = frame.keyFrame;
        return encoded;
    }
};

class FakeDecoder : public modules::display::IVideoDecoder
{
public:
    modules::display::DecodedFrame decode(const modules::display::EncodedFrame& frame) override
    {
        modules::display::DecodedFrame decoded;
        decoded.frameId = frame.frameId;
        decoded.keyFrame = frame.keyFrame;
        decoded.pixels = frame.payload;
        return decoded;
    }
};

class BrokenDecoder : public modules::display::IVideoDecoder
{
public:
    modules::display::DecodedFrame decode(const modules::display::EncodedFrame&) override
    {
        return {};
    }
};

class OneFrameDelayedDecoder : public modules::display::IVideoDecoder
{
public:
    modules::display::DecodedFrame decode(
        const modules::display::EncodedFrame& frame) override
    {
        modules::display::DecodedFrame next;
        next.frameId = frame.frameId;
        next.keyFrame = frame.keyFrame;
        next.pixels = frame.payload;

        if (!hasPending_) {
            pending_ = next;
            hasPending_ = true;
            modules::display::DecodedFrame wait;
            wait.decodeStatus =
                modules::display::DisplayDecodeStatus::NeedsMoreInput;
            return wait;
        }

        modules::display::DecodedFrame output = pending_;
        pending_ = next;
        return output;
    }

private:
    bool hasPending_ = false;
    modules::display::DecodedFrame pending_;
};

class FakeRenderer : public modules::display::IDisplayRenderer
{
public:
    bool render(const modules::display::DecodedFrame& frame) override
    {
        lastFrameId_ = frame.frameId;
        lastKeyFrame_ = frame.keyFrame;
        ++renderedFrames_;
        return true;
    }

    int renderedFrames() const
    {
        return renderedFrames_;
    }

    std::uint64_t lastFrameId() const
    {
        return lastFrameId_;
    }

    bool lastKeyFrame() const
    {
        return lastKeyFrame_;
    }

private:
    int renderedFrames_ = 0;
    std::uint64_t lastFrameId_ = 0;
    bool lastKeyFrame_ = false;
};

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::Video,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::CursorChange,
                                protocol::PacketType::Watermark};
    capabilities.messageKinds = {protocol::MessageKind::Event,
                                 protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error};
    return capabilities;
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

network::ChannelKey smallDataKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
                               protocol::ChannelType::Standard};
}

network::ChannelSpec specForKey(network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : network::defaultMvpChannelSpecs()) {
        if (spec.key == key)
            return spec;
    }
    assert(false);
    return network::ChannelSpec{};
}

std::shared_ptr<BridgeChannel> prepareChannel(network::NetworkRouter& localRouter,
                                              network::NetworkRouter& peerRouter,
                                              network::ChannelRegistry& registry,
                                              network::ChannelKey key)
{
    network::ChannelSpec spec = specForKey(key);
    const network::ChannelRegistryResult specResult = registry.registerSpec(spec);
    assert(specResult.ok || specResult.status == network::ChannelRegistryStatus::AlreadyRegistered);

    std::shared_ptr<BridgeChannel> channel = std::make_shared<BridgeChannel>(key, &peerRouter);
    assert(localRouter.registerChannel(channel));
    assert(registry.bind(key, channel).ok);
    assert(registry.markReady(key, {}).ok);
    return channel;
}

void prepareDisplayEndpoint(network::NetworkRouter& localRouter,
                            network::NetworkRouter& peerRouter,
                            network::ChannelRegistry& registry,
                            std::shared_ptr<BridgeChannel>* screenChannel = nullptr,
                            std::shared_ptr<BridgeChannel>* smallDataChannel = nullptr)
{
    std::shared_ptr<BridgeChannel> screen =
        prepareChannel(localRouter, peerRouter, registry, screenKey());
    std::shared_ptr<BridgeChannel> smallData =
        prepareChannel(localRouter, peerRouter, registry, smallDataKey());

    if (screenChannel != nullptr)
        *screenChannel = screen;
    if (smallDataChannel != nullptr)
        *smallDataChannel = smallData;
}

module::ModuleRuntime makeRuntime(session::SessionRole role,
                                  network::ChannelRegistry* registry,
                                  network::INetworkRouter* router,
                                  diagnostics::DiagnosticsSink* diagnostics)
{
    module::ModuleRuntime runtime;
    runtime.session.sessionId = role == session::SessionRole::Agent ? 100 : 200;
    runtime.session.role = role;
    runtime.session.localPlatform = "windows";
    runtime.session.allowedFeatures.bits = protocol::feature::Screen;
    runtime.channels = registry;
    runtime.network = router;
    runtime.diagnostics = diagnostics;
    return runtime;
}

policy::StaticPolicyEngine makePolicy()
{
    protocol::FeatureSet features;
    features.bits = protocol::feature::Screen;
    return policy::StaticPolicyEngine(features);
}

bool hasEvent(const diagnostics::DiagnosticsSink& diagnostics, const std::string& code)
{
    for (const diagnostics::DiagnosticEvent& event : diagnostics.events()) {
        if (event.code == code)
            return true;
    }
    return false;
}

bool packetIsKeyFrame(const protocol::PacketEnvelope& packet)
{
    return (packet.flags & protocol::PacketFlagKeyFrame) != 0;
}

bool containsString(const std::vector<std::string>& values, const std::string& expected)
{
    for (const std::string& value : values) {
        if (value == expected)
            return true;
    }
    return false;
}

protocol::PacketEnvelope makeVideoPacket(protocol::SessionId sessionId,
                                         std::uint64_t sequence,
                                         bool keyFrame)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = sessionId;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    packet.priority = protocol::PacketPriority::Realtime;
    packet.sequence = sequence;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    if (keyFrame)
        packet.flags |= protocol::PacketFlagKeyFrame;
    packet.payload = {static_cast<std::uint8_t>(sequence), 42};
    return packet;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "display-user";
    options.context.tenantId = "display-tenant";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

void firstFrameAndKeyframeRecoveryRouteThroughModuleHost()
{
    network::NetworkRouter agentRouter;
    network::NetworkRouter clientRouter;
    network::ChannelRegistry agentRegistry(makeNegotiated());
    network::ChannelRegistry clientRegistry(makeNegotiated());
    std::shared_ptr<BridgeChannel> agentScreenChannel;
    std::shared_ptr<BridgeChannel> agentSmallDataChannel;
    std::shared_ptr<BridgeChannel> clientScreenChannel;
    std::shared_ptr<BridgeChannel> clientSmallDataChannel;
    prepareDisplayEndpoint(agentRouter,
                           clientRouter,
                           agentRegistry,
                           &agentScreenChannel,
                           &agentSmallDataChannel);
    prepareDisplayEndpoint(clientRouter,
                           agentRouter,
                           clientRegistry,
                           &clientScreenChannel,
                           &clientSmallDataChannel);
    diagnostics::DiagnosticsSink agentDiagnostics;
    diagnostics::DiagnosticsSink clientDiagnostics;

    policy::StaticPolicyEngine agentPolicy = makePolicy();
    policy::StaticPolicyEngine clientPolicy = makePolicy();
    module::ModuleHost clientHost(makeRuntime(session::SessionRole::Client,
                                             &clientRegistry,
                                             &clientRouter,
                                             &clientDiagnostics),
                                  &clientPolicy);
    module::ModuleHost agentHost(makeRuntime(session::SessionRole::Agent,
                                            &agentRegistry,
                                            &agentRouter,
                                            &agentDiagnostics),
                                 &agentPolicy);

    auto capture = std::make_shared<FakeCapture>();
    auto encoder = std::make_shared<FakeEncoder>();
    auto decoder = std::make_shared<FakeDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();
    auto agent = std::make_shared<modules::display::DisplayAgentModule>(capture, encoder);
    auto client = std::make_shared<modules::display::DisplayClientModule>(decoder, renderer);

    assert(clientHost.addModule(client));
    const std::vector<module::ModuleStartReport> clientReports = clientHost.startAllowedModules();
    assert(clientReports.size() == 1);
    assert(clientReports.front().started);
    assert(renderer->renderedFrames() == 0);

    assert(agentHost.addModule(agent));
    const std::vector<module::ModuleStartReport> agentReports = agentHost.startAllowedModules();
    assert(agentReports.size() == 1);
    assert(agentReports.front().started);
    assert(agent->sentFrames() == 1);
    assert(client->receivedFrames() == 1);
    assert(client->renderedFrames() == 1);
    assert(renderer->renderedFrames() == 1);
    assert(renderer->lastFrameId() == 1);
    assert(renderer->lastKeyFrame());
    modules::display::DisplayAgentSnapshot agentSnapshot = agent->snapshot();
    assert(agentSnapshot.state == module::ModuleState::Running);
    assert(agentSnapshot.capturedFrames == 1);
    assert(agentSnapshot.encodedFrames == 1);
    assert(agentSnapshot.sentFrames == 1);
    assert(agentSnapshot.capturedPixelBytes == 2);
    assert(agentSnapshot.encodedPayloadBytes == 2);
    assert(agentSnapshot.sentPayloadBytes == 2);
    assert(agentSnapshot.lastCapturedPixelBytes == 2);
    assert(agentSnapshot.lastEncodedPayloadBytes == 2);
    assert(agentSnapshot.lastSentPayloadBytes == 2);
    assert(agentSnapshot.droppedFrames == 0);
    assert(agentSnapshot.captureErrors == 0);
    modules::display::DisplayClientSnapshot clientSnapshot = client->snapshot();
    assert(clientSnapshot.state == module::ModuleState::Running);
    assert(clientSnapshot.receivedFrames == 1);
    assert(clientSnapshot.decodedFrames == 1);
    assert(clientSnapshot.renderedFrames == 1);
    assert(clientSnapshot.receivedPayloadBytes == 2);
    assert(clientSnapshot.decodedPixelBytes == 2);
    assert(clientSnapshot.renderedPixelBytes == 2);
    assert(clientSnapshot.lastReceivedPayloadBytes == 2);
    assert(clientSnapshot.lastDecodedPixelBytes == 2);
    assert(clientSnapshot.lastRenderedPixelBytes == 2);
    assert(clientSnapshot.renderErrors == 0);
    assert(hasEvent(agentDiagnostics, "display.first_frame_sent"));
    assert(hasEvent(clientDiagnostics, "display.first_frame_rendered"));

    network::ChannelPressure pressure;
    pressure.level = network::PressureLevel::Congested;
    pressure.queuedPackets = 8;
    pressure.queuedBytes = 4096;
    assert(agentRegistry.updatePressure(screenKey(), pressure).ok);
    assert(!agent->sendDeltaFrame());
    assert(agent->droppedFrames() == 1);
    assert(agent->sentFrames() == 1);
    assert(client->renderedFrames() == 1);
    agentSnapshot = agent->snapshot();
    assert(agentSnapshot.capturedFrames == 1);
    assert(agentSnapshot.encodedFrames == 1);
    assert(agentSnapshot.sentFrames == 1);
    assert(agentSnapshot.droppedFrames == 1);
    assert(hasEvent(agentDiagnostics, "display.frame_dropped"));

    assert(client->requestKeyFrame());
    assert(client->lastKeyframeRequestId() != 0);
    assert(clientSmallDataChannel->sentPackets.size() == 1);
    assert(clientSmallDataChannel->sentPackets.back().channelId == smallDataKey().channelId);
    assert(clientSmallDataChannel->sentPackets.back().channelType == smallDataKey().channelType);
    assert(clientSmallDataChannel->sentPackets.back().packetType == protocol::PacketType::PayloadAck);
    assert(agentSmallDataChannel->sentPackets.size() == 1);
    assert(agentSmallDataChannel->sentPackets.back().channelId == smallDataKey().channelId);
    assert(agentSmallDataChannel->sentPackets.back().channelType == smallDataKey().channelType);
    assert(agentSmallDataChannel->sentPackets.back().packetType == protocol::PacketType::PayloadAck);
    assert(agent->keyframeRequests() == 1);
    assert(agent->responsesSent() == 1);
    assert(agent->sentFrames() == 2);
    assert(client->keyframeResponses() == 1);
    assert(client->receivedFrames() == 2);
    assert(client->renderedFrames() == 2);
    assert(renderer->lastFrameId() == 2);
    assert(renderer->lastKeyFrame());
    agentSnapshot = agent->snapshot();
    assert(agentSnapshot.capturedFrames == 2);
    assert(agentSnapshot.encodedFrames == 2);
    assert(agentSnapshot.sentFrames == 2);
    assert(agentSnapshot.capturedPixelBytes == 4);
    assert(agentSnapshot.encodedPayloadBytes == 4);
    assert(agentSnapshot.sentPayloadBytes == 4);
    assert(agentSnapshot.droppedFrames == 1);
    assert(agentSnapshot.keyframeRequests == 1);
    assert(agentSnapshot.responsesSent == 1);
    clientSnapshot = client->snapshot();
    assert(clientSnapshot.receivedFrames == 2);
    assert(clientSnapshot.decodedFrames == 2);
    assert(clientSnapshot.renderedFrames == 2);
    assert(clientSnapshot.receivedPayloadBytes == 4);
    assert(clientSnapshot.decodedPixelBytes == 4);
    assert(clientSnapshot.renderedPixelBytes == 4);
    assert(clientSnapshot.renderErrors == 0);
    assert(clientSnapshot.keyframeResponses == 1);
    assert(clientSnapshot.lastKeyframeRequestId == client->lastKeyframeRequestId());
    assert(hasEvent(agentDiagnostics, "display.keyframe_requested"));
    assert(hasEvent(agentDiagnostics, "display.keyframe_response_sent"));
    assert(hasEvent(agentDiagnostics, "display.keyframe_sent"));
    assert(hasEvent(clientDiagnostics, "display.keyframe_response_received"));
    assert(hasEvent(clientDiagnostics, "display.keyframe_rendered"));

    const protocol::MessageId previousKeyframeRequest = client->lastKeyframeRequestId();
    module::ModuleReconnectOptions reconnectOptions;
    reconnectOptions.reason = "video channel rebound";
    reconnectOptions.affectedChannels = {screenKey()};
    reconnectOptions.reconnectCount = 1;
    reconnectOptions.requestFreshState = true;
    client->pauseForReconnect(reconnectOptions);
    client->resumeAfterReconnect(reconnectOptions);
    assert(client->lastKeyframeRequestId() != previousKeyframeRequest);
    assert(agent->keyframeRequests() == 2);
    assert(agent->responsesSent() == 2);
    assert(agent->sentFrames() == 3);
    assert(client->keyframeResponses() == 2);
    assert(client->receivedFrames() == 3);
    assert(client->renderedFrames() == 3);
    clientSnapshot = client->snapshot();
    assert(clientSnapshot.reconnectPauses == 1);
    assert(clientSnapshot.reconnectResumes == 1);
    assert(clientSnapshot.keyframeResponses == 2);
    agentSnapshot = agent->snapshot();
    assert(agentSnapshot.keyframeRequests == 2);
    assert(agentSnapshot.responsesSent == 2);
    assert(hasEvent(clientDiagnostics, "display.reconnect_paused"));
    assert(hasEvent(clientDiagnostics, "display.reconnect_resumed"));
}

void agentPromotesCaptureGeometryChangeToKeyframe()
{
    network::NetworkRouter agentRouter;
    network::NetworkRouter clientRouter;
    network::ChannelRegistry agentRegistry(makeNegotiated());
    std::shared_ptr<BridgeChannel> agentScreenChannel;
    prepareDisplayEndpoint(agentRouter,
                           clientRouter,
                           agentRegistry,
                           &agentScreenChannel,
                           nullptr);
    diagnostics::DiagnosticsSink diagnostics;

    auto capture = std::make_shared<ResizingCapture>();
    auto encoder = std::make_shared<FakeEncoder>();
    auto agent = std::make_shared<modules::display::DisplayAgentModule>(capture, encoder);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Agent,
                                                &agentRegistry,
                                                &agentRouter,
                                                &diagnostics);

    assert(agent->attach(runtime));
    assert(agent->start(module::ModuleStartOptions{}));
    assert(agent->sentFrames() == 1);
    assert(agentScreenChannel->sentPackets.size() == 1);
    assert(packetIsKeyFrame(agentScreenChannel->sentPackets.back()));
    modules::display::DisplayAgentSnapshot snapshot = agent->snapshot();
    assert(snapshot.captureGeometryOrFormatChanges == 0);
    assert(snapshot.captureErrors == 0);

    assert(agent->sendDeltaFrame());
    assert(agent->sentFrames() == 2);
    assert(agentScreenChannel->sentPackets.size() == 2);
    assert(!packetIsKeyFrame(agentScreenChannel->sentPackets.back()));
    snapshot = agent->snapshot();
    assert(snapshot.captureGeometryOrFormatChanges == 0);

    capture->setSize(640, 360);
    assert(agent->sendDeltaFrame());
    assert(agent->sentFrames() == 3);
    assert(agentScreenChannel->sentPackets.size() == 3);
    assert(packetIsKeyFrame(agentScreenChannel->sentPackets.back()));
    snapshot = agent->snapshot();
    assert(snapshot.captureGeometryOrFormatChanges == 1);
    assert(snapshot.captureErrors == 0);
    assert(hasEvent(diagnostics, "display.capture_geometry_or_format_changed"));
}

void profileDrivenMountCreatesRoleSpecificDisplayModules()
{
    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());
    assert(clientSession->moduleHost() != nullptr);
    assert(agentSession->moduleHost() != nullptr);

    prepareDisplayEndpoint(agentSession->network()->router(),
                           clientSession->network()->router(),
                           agentSession->network()->registry());
    prepareDisplayEndpoint(clientSession->network()->router(),
                           agentSession->network()->router(),
                           clientSession->network()->registry());

    auto capture = std::make_shared<FakeCapture>();
    auto encoder = std::make_shared<FakeEncoder>();
    auto decoder = std::make_shared<FakeDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();

    runtime::DisplayMvpDependencies clientDependencies;
    clientDependencies.decoder = decoder;
    clientDependencies.renderer = renderer;
    const runtime::ProfileMountReport clientReport =
        host.mountProfileModules(*clientSession, clientDependencies);
    assert(clientReport.ok());
    assert(containsString(clientReport.mountedModules, "display.screen.client"));
    assert(clientSession->moduleHost()->module("display.screen.client") != nullptr);

    runtime::DisplayMvpDependencies agentDependencies;
    agentDependencies.capture = capture;
    agentDependencies.encoder = encoder;
    const runtime::ProfileMountReport agentReport =
        host.mountProfileModules(*agentSession, agentDependencies);
    assert(agentReport.ok());
    assert(containsString(agentReport.mountedModules, "display.screen.agent"));
    assert(agentSession->moduleHost()->module("display.screen.agent") != nullptr);

    const std::vector<module::ModuleStartReport> clientReports =
        clientSession->moduleHost()->startAllowedModules();
    assert(clientReports.size() == 1);
    assert(clientReports.front().started);

    const std::vector<module::ModuleStartReport> agentReports =
        agentSession->moduleHost()->startAllowedModules();
    assert(agentReports.size() == 1);
    assert(agentReports.front().started);

    auto* client = dynamic_cast<modules::display::DisplayClientModule*>(
        clientSession->moduleHost()->module("display.screen.client"));
    auto* agent = dynamic_cast<modules::display::DisplayAgentModule*>(
        agentSession->moduleHost()->module("display.screen.agent"));
    assert(client != nullptr);
    assert(agent != nullptr);
    assert(agent->sentFrames() == 1);
    assert(client->renderedFrames() == 1);
    assert(renderer->renderedFrames() == 1);
    const modules::display::DisplayAgentSnapshot agentDisplaySnapshot = agent->snapshot();
    assert(agentDisplaySnapshot.capturedFrames == 1);
    assert(agentDisplaySnapshot.encodedFrames == 1);
    assert(agentDisplaySnapshot.sentFrames == 1);
    const modules::display::DisplayClientSnapshot clientDisplaySnapshot = client->snapshot();
    assert(clientDisplaySnapshot.receivedFrames == 1);
    assert(clientDisplaySnapshot.decodedFrames == 1);
    assert(clientDisplaySnapshot.renderedFrames == 1);
    assert(clientDisplaySnapshot.renderErrors == 0);

    const session::SessionSnapshot clientSnapshot = clientSession->snapshot();
    assert(clientSnapshot.moduleSnapshots.size() == 1);
    assert(clientSnapshot.moduleSnapshots.front().moduleId == "display.screen.client");
    assert(clientSnapshot.moduleSnapshots.front().state == module::ModuleState::Running);
    assert(clientSnapshot.moduleSnapshots.front().manifest.moduleId == "display.screen.client");
}

void displayFactoryResolvesAliasBySessionRole()
{
    modules::display::DisplayModuleDependencies dependencies;
    dependencies.capture = std::make_shared<FakeCapture>();
    dependencies.encoder = std::make_shared<FakeEncoder>();
    dependencies.decoder = std::make_shared<FakeDecoder>();
    dependencies.renderer = std::make_shared<FakeRenderer>();

    auto factory = std::make_shared<modules::display::DisplayModuleFactory>(dependencies);
    module::ModuleComposer composer;
    composer.addFactory(factory);

    module::ModuleCompositionRequest agentRequest;
    agentRequest.requiredModules = {"display.screen"};
    agentRequest.createOptions.role = session::SessionRole::Agent;
    agentRequest.createOptions.localPlatform = "windows";
    const module::ModuleCompositionResult agent = composer.compose(agentRequest);
    assert(agent.ok());
    assert(agent.manifests.size() == 1);
    assert(agent.manifests.front().moduleId == "display.screen.agent");
    assert(agent.modules.front()->manifest().moduleId == "display.screen.agent");

    module::ModuleCompositionRequest clientRequest;
    clientRequest.requiredModules = {"display.screen"};
    clientRequest.createOptions.role = session::SessionRole::Client;
    clientRequest.createOptions.localPlatform = "windows";
    const module::ModuleCompositionResult client = composer.compose(clientRequest);
    assert(client.ok());
    assert(client.manifests.size() == 1);
    assert(client.manifests.front().moduleId == "display.screen.client");
    assert(client.modules.front()->manifest().moduleId == "display.screen.client");

    module::ModuleCompositionRequest mismatchRequest;
    mismatchRequest.requiredModules = {"display.screen.agent"};
    mismatchRequest.createOptions.role = session::SessionRole::Client;
    mismatchRequest.createOptions.localPlatform = "windows";
    const module::ModuleCompositionResult mismatch = composer.compose(mismatchRequest);
    assert(!mismatch.ok());
    assert(mismatch.modules.empty());
    assert(containsString(mismatch.missingModules, "display.screen.agent"));
}

void profileDrivenMountReportsMissingDisplayDependencies()
{
    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());

    const runtime::ProfileMountReport clientReport =
        host.mountProfileModules(*clientSession, runtime::DisplayMvpDependencies{});
    assert(!clientReport.ok());
    assert(clientReport.mountedModules.empty());
    assert(containsString(clientReport.missingModules, "display.screen.client"));

    const runtime::ProfileMountReport agentReport =
        host.mountProfileModules(*agentSession, runtime::DisplayMvpDependencies{});
    assert(!agentReport.ok());
    assert(agentReport.mountedModules.empty());
    assert(containsString(agentReport.missingModules, "display.screen.agent"));
}

void agentStartFailureRollsBackCaptureState()
{
    network::NetworkRouter agentRouter;
    network::NetworkRouter clientRouter;
    network::ChannelRegistry agentRegistry(makeNegotiated());
    prepareDisplayEndpoint(agentRouter, clientRouter, agentRegistry);
    diagnostics::DiagnosticsSink diagnostics;

    auto capture = std::make_shared<TrackingCapture>();
    auto encoder = std::make_shared<EmptyEncoder>();
    auto agent = std::make_shared<modules::display::DisplayAgentModule>(capture, encoder);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Agent,
                                                &agentRegistry,
                                                &agentRouter,
                                                &diagnostics);

    assert(agent->attach(runtime));
    assert(!agent->start(module::ModuleStartOptions{}));
    assert(!capture->opened());
    assert(capture->opens() == 1);
    assert(capture->closes() == 1);
    const modules::display::DisplayAgentSnapshot snapshot = agent->snapshot();
    assert(snapshot.state == module::ModuleState::Failed);
    assert(snapshot.capturedFrames == 1);
    assert(snapshot.encodedFrames == 0);
    assert(snapshot.encodeFailures == 1);
    assert(snapshot.sentFrames == 0);
    assert(hasEvent(diagnostics, "display.frame_encode_failed"));
}

void invalidControlRequestDoesNotTriggerKeyframe()
{
    network::NetworkRouter agentRouter;
    network::NetworkRouter clientRouter;
    network::ChannelRegistry agentRegistry(makeNegotiated());
    prepareDisplayEndpoint(agentRouter, clientRouter, agentRegistry);
    diagnostics::DiagnosticsSink diagnostics;

    auto capture = std::make_shared<FakeCapture>();
    auto encoder = std::make_shared<FakeEncoder>();
    auto agent = std::make_shared<modules::display::DisplayAgentModule>(capture, encoder);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Agent,
                                                &agentRegistry,
                                                &agentRouter,
                                                &diagnostics);

    assert(agent->attach(runtime));
    assert(agent->start(module::ModuleStartOptions{}));
    assert(agent->sentFrames() == 1);

    protocol::PacketEnvelope request;
    request.sessionId = 100;
    request.channelId = smallDataKey().channelId;
    request.channelType = smallDataKey().channelType;
    request.packetType = protocol::PacketType::PayloadAck;
    request.messageKind = protocol::MessageKind::Request;
    request.priority = protocol::PacketPriority::Interactive;
    request.messageId = 77;
    request.correlationId = 77;
    request.timeoutMs = 1000;
    request.flags = protocol::PacketFlagResponseRequired;
    request.payload = {'k', 'e', 'y', 'f', 'r', 'a', 'm', 'e'};
    agent->handlePacket(request);

    const modules::display::DisplayAgentSnapshot snapshot = agent->snapshot();
    assert(snapshot.invalidControlRequests == 1);
    assert(snapshot.keyframeRequests == 0);
    assert(snapshot.sentFrames == 1);
    assert(snapshot.responsesSent == 1);
    assert(hasEvent(diagnostics, "display.control_request_invalid"));
}

void clientFrameGapRequestsKeyframeAndTimesOut()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    prepareDisplayEndpoint(clientRouter, agentRouter, clientRegistry);
    diagnostics::DiagnosticsSink diagnostics;

    auto decoder = std::make_shared<FakeDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();
    auto client = std::make_shared<modules::display::DisplayClientModule>(decoder, renderer);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Client,
                                                &clientRegistry,
                                                &clientRouter,
                                                &diagnostics);

    assert(client->attach(runtime));
    assert(client->start(module::ModuleStartOptions{}));
    client->handlePacket(makeVideoPacket(200, 1, true));
    assert(client->renderedFrames() == 1);

    client->handlePacket(makeVideoPacket(200, 3, false));
    modules::display::DisplayClientSnapshot snapshot = client->snapshot();
    assert(snapshot.receivedFrames == 2);
    assert(snapshot.renderedFrames == 1);
    assert(snapshot.droppedFrames == 1);
    assert(snapshot.frameGaps == 1);
    assert(snapshot.lastRenderedFrameId == 1);
    assert(snapshot.decoderRecoveries == 1);
    assert(snapshot.pendingKeyframeRequests == 1);
    assert(client->lastKeyframeRequestId() != 0);
    assert(hasEvent(diagnostics, "display.frame_gap_detected"));

    assert(client->expireKeyframeRequests(std::numeric_limits<std::uint64_t>::max()) == 1);
    snapshot = client->snapshot();
    assert(snapshot.pendingKeyframeRequests == 0);
    assert(snapshot.keyframeRequestTimeouts == 1);
    assert(snapshot.keyframeRequestFailures == 1);
}

void clientDropsStaleFramesWithoutRenderRollback()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    prepareDisplayEndpoint(clientRouter, agentRouter, clientRegistry);
    diagnostics::DiagnosticsSink diagnostics;

    auto decoder = std::make_shared<FakeDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();
    auto client = std::make_shared<modules::display::DisplayClientModule>(decoder, renderer);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Client,
                                                &clientRegistry,
                                                &clientRouter,
                                                &diagnostics);

    assert(client->attach(runtime));
    assert(client->start(module::ModuleStartOptions{}));
    client->handlePacket(makeVideoPacket(200, 1, true));
    client->handlePacket(makeVideoPacket(200, 2, false));
    assert(renderer->lastFrameId() == 2);

    client->handlePacket(makeVideoPacket(200, 1, false));
    const modules::display::DisplayClientSnapshot snapshot = client->snapshot();
    assert(snapshot.receivedFrames == 3);
    assert(snapshot.decodedFrames == 2);
    assert(snapshot.renderedFrames == 2);
    assert(snapshot.droppedFrames == 1);
    assert(snapshot.frameGaps == 0);
    assert(snapshot.decoderRecoveries == 0);
    assert(snapshot.pendingKeyframeRequests == 0);
    assert(snapshot.lastRenderedFrameId == 2);
    assert(renderer->lastFrameId() == 2);
    assert(hasEvent(diagnostics, "display.frame_stale_dropped"));
}

void clientDecodeFailureRequestsRecovery()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    prepareDisplayEndpoint(clientRouter, agentRouter, clientRegistry);
    diagnostics::DiagnosticsSink diagnostics;

    auto decoder = std::make_shared<BrokenDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();
    auto client = std::make_shared<modules::display::DisplayClientModule>(decoder, renderer);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Client,
                                                &clientRegistry,
                                                &clientRouter,
                                                &diagnostics);

    assert(client->attach(runtime));
    assert(client->start(module::ModuleStartOptions{}));
    client->handlePacket(makeVideoPacket(200, 1, true));

    const modules::display::DisplayClientSnapshot snapshot = client->snapshot();
    assert(snapshot.receivedFrames == 1);
    assert(snapshot.decodedFrames == 0);
    assert(snapshot.renderedFrames == 0);
    assert(snapshot.droppedFrames == 1);
    assert(snapshot.decodeErrors == 1);
    assert(snapshot.decoderRecoveries == 1);
    assert(snapshot.pendingKeyframeRequests == 1);
    assert(renderer->renderedFrames() == 0);
    assert(hasEvent(diagnostics, "display.frame_decode_failed"));
}

void clientAllowsDelayedDecoderOutputForPFrameCodecs()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    prepareDisplayEndpoint(clientRouter, agentRouter, clientRegistry);
    diagnostics::DiagnosticsSink diagnostics;

    auto decoder = std::make_shared<OneFrameDelayedDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();
    auto client =
        std::make_shared<modules::display::DisplayClientModule>(decoder,
                                                                renderer);
    module::ModuleRuntime runtime = makeRuntime(session::SessionRole::Client,
                                                &clientRegistry,
                                                &clientRouter,
                                                &diagnostics);

    assert(client->attach(runtime));
    assert(client->start(module::ModuleStartOptions{}));

    client->handlePacket(makeVideoPacket(200, 1, true));
    modules::display::DisplayClientSnapshot snapshot = client->snapshot();
    assert(snapshot.receivedFrames == 1);
    assert(snapshot.decodedFrames == 0);
    assert(snapshot.renderedFrames == 0);
    assert(snapshot.droppedFrames == 0);
    assert(snapshot.decodeErrors == 0);
    assert(snapshot.decoderPendingFrames == 1);
    assert(snapshot.pendingKeyframeRequests == 0);
    assert(hasEvent(diagnostics, "display.decoder_needs_more_input"));

    client->handlePacket(makeVideoPacket(200, 2, false));
    snapshot = client->snapshot();
    assert(snapshot.receivedFrames == 2);
    assert(snapshot.decodedFrames == 1);
    assert(snapshot.renderedFrames == 1);
    assert(snapshot.droppedFrames == 0);
    assert(snapshot.decodeErrors == 0);
    assert(snapshot.decoderRecoveries == 0);
    assert(snapshot.delayedDecodedFrames == 1);
    assert(snapshot.lastRenderedFrameId == 1);
    assert(snapshot.pendingKeyframeRequests == 0);
    assert(renderer->lastFrameId() == 1);
    assert(renderer->lastKeyFrame());

    client->handlePacket(makeVideoPacket(200, 3, false));
    snapshot = client->snapshot();
    assert(snapshot.decodedFrames == 2);
    assert(snapshot.renderedFrames == 2);
    assert(snapshot.frameGaps == 0);
    assert(snapshot.delayedDecodedFrames == 2);
    assert(snapshot.lastRenderedFrameId == 2);
    assert(renderer->lastFrameId() == 2);
}

} // namespace

int main()
{
    firstFrameAndKeyframeRecoveryRouteThroughModuleHost();
    agentPromotesCaptureGeometryChangeToKeyframe();
    profileDrivenMountCreatesRoleSpecificDisplayModules();
    displayFactoryResolvesAliasBySessionRole();
    profileDrivenMountReportsMissingDisplayDependencies();
    agentStartFailureRollsBackCaptureState();
    invalidControlRequestDoesNotTriggerKeyframe();
    clientFrameGapRequestsKeyframeAndTimesOut();
    clientDropsStaleFramesWithoutRenderRollback();
    clientDecodeFailureRequestsRecovery();
    clientAllowsDelayedDecoderOutputForPFrameCodecs();
    return 0;
}
