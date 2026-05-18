#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/session/session_manager.h"
#include "fusiondesk/modules/display/display_frame_codec.h"
#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/runtime/display/display_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;

namespace {

class CountingChannel final : public network::IChannel
{
public:
    explicit CountingChannel(network::ChannelKey key)
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
        lastSequence = packet.sequence;
        ++sentPackets;
        return network::SendResult::sent();
    }

    int sentPackets = 0;
    std::uint64_t lastSequence = 0;

private:
    network::ChannelKey key_;
};

class RuntimeCapture final : public modules::display::IDisplayCapture
{
public:
    explicit RuntimeCapture(std::string backendId = "runtime.capture")
        : backendId_(std::move(backendId))
    {
    }

    bool open(const modules::display::DisplayCaptureOpenOptions& options) override
    {
        options_ = options;
        ++openCalls;
        if (failNextOpen) {
            failNextOpen = false;
            opened = false;
            ++captureErrors_;
            lastStatus_.code = modules::display::DisplayCaptureStatusCode::NotOpen;
            lastStatus_.recoverable = true;
            lastStatus_.message = "runtime-capture-open-failed";
            return false;
        }

        opened = true;
        return true;
    }

    void close() override
    {
        opened = false;
        ++closeCalls;
    }

    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        if (!opened) {
            ++captureErrors_;
            lastStatus_.code = modules::display::DisplayCaptureStatusCode::NotOpen;
            lastStatus_.recoverable = true;
            lastStatus_.message = "runtime-capture-not-open";
            return {};
        }

        if (failNextFrameWithHotplug) {
            failNextFrameWithHotplug = false;
            ++captureErrors_;
            lastStatus_.code = modules::display::DisplayCaptureStatusCode::SourceHotplug;
            lastStatus_.recoverable = true;
            lastStatus_.message = "runtime-capture-hotplug";
            return {};
        }

        if (failNextFrameWithDeviceLost) {
            failNextFrameWithDeviceLost = false;
            ++captureErrors_;
            lastStatus_.code = modules::display::DisplayCaptureStatusCode::DeviceLost;
            lastStatus_.recoverable = false;
            lastStatus_.message = "runtime-capture-device-lost";
            return {};
        }

        lastStatus_.code = modules::display::DisplayCaptureStatusCode::Ok;
        lastStatus_.message = "runtime-capture-ok";
        modules::display::CapturedFrame frame;
        frame.frameId = ++capturedFrames;
        frame.sourceId = options_.sourceId;
        frame.keyFrame = keyFrame;
        frame.width = options_.targetWidth == 0 ? 4 : options_.targetWidth;
        frame.height = options_.targetHeight == 0 ? 2 : options_.targetHeight;
        frame.strideBytes = frame.width * 4;
        frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
        frame.pixels.assign(static_cast<std::size_t>(frame.strideBytes) * frame.height, 7);
        return frame;
    }

    modules::display::DisplayCaptureStatus lastStatus() const override
    {
        return lastStatus_;
    }

    std::string backendId() const override
    {
        return backendId_;
    }

    int captureErrors() const override
    {
        return captureErrors_;
    }

    bool opened = false;
    bool failNextOpen = false;
    bool failNextFrameWithHotplug = false;
    bool failNextFrameWithDeviceLost = false;
    int openCalls = 0;
    int closeCalls = 0;
    std::uint64_t capturedFrames = 0;
    modules::display::DisplayCaptureOpenOptions options_;
    modules::display::DisplayCaptureStatus lastStatus_;

private:
    std::string backendId_;
    int captureErrors_ = 0;
};

class RuntimeDecoder final : public modules::display::IVideoDecoder
{
public:
    modules::display::DecodedFrame decode(
        const modules::display::EncodedFrame& frame) override
    {
        modules::display::DecodedFrame decoded;
        decoded.frameId = frame.frameId;
        decoded.keyFrame = frame.keyFrame;
        decoded.width = frame.width;
        decoded.height = frame.height;
        decoded.strideBytes = frame.strideBytes;
        decoded.pixelFormat = frame.pixelFormat;
        decoded.monotonicTimestampUsec = frame.monotonicTimestampUsec;
        decoded.pixels = frame.payload;
        return decoded;
    }
};

class RuntimeRenderer final : public modules::display::IDisplayRenderer
{
public:
    bool render(const modules::display::DecodedFrame& frame) override
    {
        ++renderedFrames;
        lastFrameId = frame.frameId;
        return true;
    }

    int renderedFrames = 0;
    std::uint64_t lastFrameId = 0;
};

runtime::display::DisplayCaptureBackendCapability runtimeCapability(
    std::string adapterId,
    runtime::display::DisplayCaptureBackendKind backend,
    bool fallback,
    int priority)
{
    runtime::display::DisplayCaptureBackendCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend = backend;
    capability.sourceTypes = {runtime::display::DisplayCaptureSourceType::Monitor};
    capability.memoryTypes = {runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.fallback = fallback;
    capability.priority = priority;
    return capability;
}

class RuntimeCaptureBackendFactory final
    : public runtime::display::IDisplayCaptureBackendFactory
{
public:
    std::vector<runtime::display::DisplayCaptureBackendCapability>
    capabilities() const override
    {
        return {
            runtimeCapability("runtime.capture",
                              runtime::display::DisplayCaptureBackendKind::
                                  WindowsDxgiDesktopDuplication,
                              false,
                              90),
            runtimeCapability("runtime.gdi",
                              runtime::display::DisplayCaptureBackendKind::
                                  WindowsGdi,
                              true,
                              10)};
    }

    std::shared_ptr<modules::display::IDisplayCapture> createCapture(
        const runtime::display::DisplayCaptureBackendCapability& selected) const override
    {
        return std::make_shared<RuntimeCapture>(selected.adapterId);
    }
};

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::Video, protocol::PacketType::PayloadAck};
    capabilities.messageKinds = {protocol::MessageKind::Event,
                                 protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error};
    return capabilities;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "display-runtime-user";
    options.context.tenantId = "display-runtime-tenant";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
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

protocol::PacketEnvelope makeVideoPacket(protocol::SessionId sessionId,
                                         std::uint64_t frameId,
                                         bool keyFrame)
{
    modules::display::CapturedFrame frame;
    frame.frameId = frameId;
    frame.keyFrame = keyFrame;
    frame.width = 4;
    frame.height = 2;
    frame.strideBytes = frame.width * 4;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.pixels.assign(
        static_cast<std::size_t>(frame.strideBytes) * frame.height,
        9);

    modules::display::RawFrameEncoder encoder;
    const modules::display::EncodedFrame encoded = encoder.encode(frame);

    protocol::PacketEnvelope packet;
    packet.sessionId = sessionId;
    packet.channelId = screenKey().channelId;
    packet.channelType = screenKey().channelType;
    packet.packetType = protocol::PacketType::Video;
    packet.messageKind = protocol::MessageKind::Event;
    packet.flags = protocol::PacketFlagNoResponseRequired;
    if (keyFrame)
        packet.flags |= protocol::PacketFlagKeyFrame;
    packet.sequence = encoded.frameId;
    packet.monotonicTimestampUsec = encoded.monotonicTimestampUsec;
    packet.payload = encoded.payload;
    return packet;
}

protocol::PacketEnvelope makeKeyframeFailureResponse(protocol::MessageId requestId)
{
    protocol::PacketEnvelope packet;
    packet.channelId = smallDataKey().channelId;
    packet.channelType = smallDataKey().channelType;
    packet.packetType = protocol::PacketType::PayloadAck;
    packet.messageKind = protocol::MessageKind::Error;
    packet.responseStatus = protocol::ResponseStatus::Failed;
    packet.responseTo = requestId;
    packet.correlationId = requestId;
    return packet;
}

} // namespace

int main()
{
    runtime::RuntimeHost host;
    assert(host.initialize());
    const protocol::SessionId sessionId =
        host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* session = host.sessions().find(sessionId);
    assert(session != nullptr);
    assert(session->start());

    auto channel = std::make_shared<CountingChannel>(screenKey());
    auto controlChannel = std::make_shared<CountingChannel>(smallDataKey());
    assert(session->network()->bindChannel(channel).ok);
    assert(session->network()->bindChannel(controlChannel).ok);
    assert(session->network()->markReady(screenKey(), {}).ok);
    assert(session->network()->markReady(smallDataKey(), {}).ok);

    auto capture = std::make_shared<RuntimeCapture>();
    runtime::DisplayMvpDependencies dependencies;
    dependencies.capture = capture;
    dependencies.encoder = std::make_shared<modules::display::RawFrameEncoder>();
    dependencies.captureOptions.sourceId = 2;
    dependencies.captureOptions.targetWidth = 8;
    dependencies.captureOptions.targetHeight = 4;
    dependencies.captureOptions.scaleMode = modules::display::DisplayScaleMode::Stretch;
    dependencies.captureOptions.includeCursor = false;
    runtime::ProfileMountReport mounted = host.mountProfileModules(*session, dependencies);
    assert(mounted.ok());
    assert(session->moduleHost() != nullptr);

    const std::vector<module::ModuleStartReport> reports =
        session->moduleHost()->startAllowedModules();
    assert(reports.size() == 1);
    assert(reports.front().started);
    assert(capture->opened);
    assert(capture->capturedFrames == 1);

    auto* agent = dynamic_cast<modules::display::DisplayAgentModule*>(
        session->moduleHost()->module("display.screen.agent"));
    assert(agent != nullptr);
    assert(agent->snapshot().sentFrames == 1);
    assert(agent->snapshot().sourceId == 2);
    assert(agent->snapshot().targetWidth == 8);
    assert(agent->snapshot().targetHeight == 4);
    assert(agent->snapshot().scaleMode == modules::display::DisplayScaleMode::Stretch);
    assert(!agent->snapshot().includeCursor);
    assert(agent->snapshot().lastCaptureStatus.code ==
           modules::display::DisplayCaptureStatusCode::Ok);
    assert(agent->snapshot().captureBackendId == "runtime.capture");
    assert(agent->snapshot().captureErrors == 0);
    assert(agent->snapshot().capturedPixelBytes == 128);
    assert(agent->snapshot().encodedPayloadBytes == 168);
    assert(agent->snapshot().sentPayloadBytes == 168);
    assert(agent->snapshot().lastSentPayloadBytes == 168);

    RuntimeCaptureBackendFactory captureBackendFactory;
    runtime::display::DisplayRuntimeServiceOptions serviceOptions;
    serviceOptions.session = session;
    serviceOptions.targetFps = 10;
    serviceOptions.captureBackendFactory = &captureBackendFactory;
    serviceOptions.captureBackendSelectionRequest.platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    serviceOptions.captureBackendSelectionRequest.sourceType =
        runtime::display::DisplayCaptureSourceType::Monitor;
    runtime::display::DisplayRuntimeService service(serviceOptions);
    const runtime::display::DisplayRuntimeServiceStartResult started = service.start();
    assert(started.ok);

    runtime::display::DisplayRuntimePumpResult pumped = service.pumpOnce(1000000);
    assert(pumped.active);
    assert(pumped.frameAttempted);
    assert(pumped.frameSent);
    assert(agent->snapshot().sentFrames == 2);

    pumped = service.pumpOnce(1000001);
    assert(pumped.active);
    assert(pumped.skippedByFrameRate);
    assert(agent->snapshot().sentFrames == 2);
    runtime::display::DisplayRuntimeServiceSnapshot skippedSnapshot =
        service.snapshot();
    assert(skippedSnapshot.pumpCount == 2);
    assert(skippedSnapshot.frameAttempts == 1);
    assert(skippedSnapshot.lastPumpUsec == 1000001);
    assert(skippedSnapshot.lastFrameAttemptUsec == 1000000);
    assert(skippedSnapshot.firstFrameSentUsec == 1000000);
    assert(skippedSnapshot.lastFrameSentUsec == 1000000);
    assert(skippedSnapshot.lastFrameAgeUsec == 1);
    assert(skippedSnapshot.consecutiveFrameMisses == 0);

    pumped = service.pumpOnce(1100000);
    assert(pumped.frameAttempted);
    assert(pumped.frameSent);
    assert(agent->snapshot().sentFrames == 3);

    const runtime::display::DisplayRuntimeServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.active);
    assert(snapshot.targetFps == 10);
    assert(snapshot.pumpCount == 3);
    assert(snapshot.frameAttempts == 2);
    assert(snapshot.lastPumpUsec == 1100000);
    assert(snapshot.lastFrameAttemptUsec == 1100000);
    assert(snapshot.firstFrameSentUsec == 1000000);
    assert(snapshot.lastFrameSentUsec == 1100000);
    assert(snapshot.lastFrameAgeUsec == 0);
    assert(snapshot.effectiveFpsX1000 == 10000);
    assert(snapshot.capturedPixelBytes == 256);
    assert(snapshot.encodedPayloadBytes == 336);
    assert(snapshot.sentPayloadBytes == 336);
    assert(snapshot.lastCapturedPixelBytes == 128);
    assert(snapshot.lastEncodedPayloadBytes == 168);
    assert(snapshot.lastSentPayloadBytes == 168);
    assert(snapshot.effectiveBitrateKbps > 0);
    assert(snapshot.consecutiveFrameMisses == 0);
    assert(snapshot.framesSent == 2);
    assert(snapshot.skippedByFrameRate == 1);
    assert(snapshot.captureBackendId == "runtime.capture");
    assert(snapshot.lastCaptureStatus.message == "runtime-capture-ok");
    assert(snapshot.captureErrors == 0);
    assert(snapshot.lastCaptureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::None);
    assert(channel->sentPackets == 3);

    capture->failNextFrameWithHotplug = true;
    pumped = service.pumpOnce(1200000);
    assert(pumped.frameAttempted);
    assert(!pumped.frameSent);
    assert(pumped.encodeFailures == 1);
    assert(pumped.captureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::ReopenCapture);
    assert(pumped.captureRecoveryAttempted);
    assert(pumped.captureRecoverySucceeded);
    assert(pumped.captureRecoveryFramesSent == 1);
    assert(capture->openCalls == 2);
    assert(capture->closeCalls == 1);
    assert(agent->snapshot().sentFrames == 4);

    const runtime::display::DisplayRuntimeServiceSnapshot recoveredSnapshot =
        service.snapshot();
    assert(recoveredSnapshot.framesSent == 3);
    assert(recoveredSnapshot.frameAttempts == 3);
    assert(recoveredSnapshot.lastFrameSentUsec == 1200000);
    assert(recoveredSnapshot.effectiveFpsX1000 == 10000);
    assert(recoveredSnapshot.sentPayloadBytes == 504);
    assert(recoveredSnapshot.lastSentPayloadBytes == 168);
    assert(recoveredSnapshot.effectiveBitrateKbps > 0);
    assert(recoveredSnapshot.consecutiveFrameMisses == 0);
    assert(recoveredSnapshot.captureRecoveryAttempts == 1);
    assert(recoveredSnapshot.captureRecoverySuccesses == 1);
    assert(recoveredSnapshot.captureRecoveryFailures == 0);
    assert(recoveredSnapshot.captureRecoverySameBackendAttempts == 1);
    assert(recoveredSnapshot.lastCaptureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::ReopenCapture);
    assert(recoveredSnapshot.lastCaptureStatus.code ==
           modules::display::DisplayCaptureStatusCode::Ok);
    assert(recoveredSnapshot.captureErrors == 1);
    assert(channel->sentPackets == 4);

    capture->failNextOpen = true;
    capture->failNextFrameWithHotplug = true;
    pumped = service.pumpOnce(1300000);
    assert(pumped.frameAttempted);
    assert(!pumped.frameSent);
    assert(pumped.encodeFailures == 1);
    assert(pumped.captureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::ReopenCapture);
    assert(pumped.captureRecoveryAttempted);
    assert(!pumped.captureRecoverySucceeded);
    assert(pumped.captureRecoveryFramesSent == 0);
    assert(capture->openCalls == 3);
    assert(capture->closeCalls == 2);
    assert(agent->snapshot().captureOpenFailures == 1);
    assert(agent->snapshot().sentFrames == 4);

    const runtime::display::DisplayRuntimeServiceSnapshot failedRecoverySnapshot =
        service.snapshot();
    assert(failedRecoverySnapshot.framesSent == 3);
    assert(failedRecoverySnapshot.lastFrameAgeUsec == 100000);
    assert(failedRecoverySnapshot.consecutiveFrameMisses == 1);
    assert(failedRecoverySnapshot.captureRecoveryAttempts == 2);
    assert(failedRecoverySnapshot.captureRecoverySuccesses == 1);
    assert(failedRecoverySnapshot.captureRecoveryFailures == 1);
    assert(failedRecoverySnapshot.captureRecoveryCooldownRemainingUsec == 250000);
    assert(failedRecoverySnapshot.captureRecoverySameBackendAttempts == 2);
    assert(failedRecoverySnapshot.captureErrors == 3);
    assert(channel->sentPackets == 4);

    pumped = service.pumpOnce(1400000);
    assert(pumped.frameAttempted);
    assert(!pumped.frameSent);
    assert(pumped.encodeFailures == 1);
    assert(pumped.captureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::RetryNextFrame);
    assert(pumped.captureRecoveryBlockedByCooldown);
    assert(pumped.captureRecoveryCooldownRemainingUsec == 150000);
    assert(!pumped.captureRecoveryAttempted);
    assert(agent->snapshot().sentFrames == 4);

    const runtime::display::DisplayRuntimeServiceSnapshot cooldownSnapshot =
        service.snapshot();
    assert(cooldownSnapshot.captureRecoveryAttempts == 2);
    assert(cooldownSnapshot.captureRecoveryFailures == 1);
    assert(cooldownSnapshot.captureRecoveryCooldownBlocks == 1);
    assert(cooldownSnapshot.captureRecoveryCooldownRemainingUsec == 150000);
    assert(cooldownSnapshot.lastFrameAgeUsec == 200000);
    assert(cooldownSnapshot.consecutiveFrameMisses == 2);
    assert(cooldownSnapshot.lastCaptureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::RetryNextFrame);
    assert(cooldownSnapshot.captureErrors == 4);
    assert(channel->sentPackets == 4);

    pumped = service.pumpOnce(1600000);
    assert(pumped.frameAttempted);
    assert(!pumped.frameSent);
    assert(pumped.encodeFailures == 1);
    assert(pumped.captureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::SwitchBackend);
    assert(pumped.captureRecoveryPromotedToSwitchBackend);
    assert(pumped.captureRecoveryAttempted);
    assert(pumped.captureRecoverySucceeded);
    assert(pumped.captureRecoveryFramesSent == 1);
    assert(agent->snapshot().captureBackendId == "runtime.gdi");
    assert(agent->snapshot().sentFrames == 5);

    const runtime::display::DisplayRuntimeServiceSnapshot switchedSnapshot =
        service.snapshot();
    assert(switchedSnapshot.framesSent == 4);
    assert(switchedSnapshot.lastFrameSentUsec == 1600000);
    assert(switchedSnapshot.lastFrameAgeUsec == 0);
    assert(switchedSnapshot.consecutiveFrameMisses == 0);
    assert(switchedSnapshot.captureRecoveryAttempts == 3);
    assert(switchedSnapshot.captureRecoverySuccesses == 2);
    assert(switchedSnapshot.captureRecoveryFailures == 1);
    assert(switchedSnapshot.captureRecoveryCooldownBlocks == 1);
    assert(switchedSnapshot.captureRecoverySwitchPromotions == 1);
    assert(switchedSnapshot.captureRecoverySameBackendAttempts == 0);
    assert(switchedSnapshot.lastCaptureRecoveryAction ==
           runtime::display::DisplayCaptureRecoveryAction::SwitchBackend);
    assert(switchedSnapshot.captureBackendId == "runtime.gdi");
    assert(switchedSnapshot.lastCaptureStatus.code ==
           modules::display::DisplayCaptureStatusCode::Ok);
    assert(switchedSnapshot.captureErrors == 0);
    assert(channel->sentPackets == 5);

    service.stop();
    assert(!service.snapshot().active);

    const protocol::SessionId clientSessionId =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientSessionId);
    assert(clientSession != nullptr);
    assert(clientSession->start());

    auto clientVideoChannel = std::make_shared<CountingChannel>(screenKey());
    auto clientControlChannel = std::make_shared<CountingChannel>(smallDataKey());
    assert(clientSession->network()->bindChannel(clientVideoChannel).ok);
    assert(clientSession->network()->bindChannel(clientControlChannel).ok);
    assert(clientSession->network()->markReady(screenKey(), {}).ok);
    assert(clientSession->network()->markReady(smallDataKey(), {}).ok);

    runtime::DisplayMvpDependencies clientDependencies;
    clientDependencies.decoder = std::make_shared<RuntimeDecoder>();
    clientDependencies.renderer = std::make_shared<RuntimeRenderer>();
    runtime::ProfileMountReport clientMounted =
        host.mountProfileModules(*clientSession, clientDependencies);
    assert(clientMounted.ok());
    const std::vector<module::ModuleStartReport> clientReports =
        clientSession->moduleHost()->startAllowedModules();
    assert(clientReports.size() == 1);
    assert(clientReports.front().started);

    auto* clientModule = dynamic_cast<modules::display::DisplayClientModule*>(
        clientSession->moduleHost()->module("display.screen.client"));
    assert(clientModule != nullptr);

    runtime::display::DisplayRuntimeServiceOptions clientServiceOptions;
    clientServiceOptions.session = clientSession;
    clientServiceOptions.pumpAgentFrames = false;
    clientServiceOptions.targetFps = 10;
    clientServiceOptions.firstFrameTimeoutUsec = 500000;
    runtime::display::DisplayRuntimeService clientService(clientServiceOptions);
    const runtime::display::DisplayRuntimeServiceStartResult clientStarted =
        clientService.start();
    assert(clientStarted.ok);

    runtime::display::DisplayRuntimePumpResult clientPump =
        clientService.pumpOnce(2000000);
    assert(clientPump.active);
    assert(!clientPump.firstFrameTimedOut);
    assert(clientControlChannel->sentPackets == 0);

    clientPump = clientService.pumpOnce(2600000);
    assert(clientPump.active);
    assert(clientPump.firstFrameTimedOut);
    assert(clientPump.keyframeRequestSent);
    assert(clientPump.keyframeRequestFailures == 0);
    assert(clientModule->snapshot().pendingKeyframeRequests == 1);
    assert(clientModule->snapshot().lastKeyframeRequestId != 0);
    assert(clientControlChannel->sentPackets == 1);

    const runtime::display::DisplayRuntimeServiceSnapshot clientSnapshot =
        clientService.snapshot();
    assert(clientSnapshot.firstFrameTimeouts == 1);
    assert(clientSnapshot.keyframeRequestsSent == 1);
    assert(clientSnapshot.keyframeRequestFailures == 0);
    assert(clientSnapshot.missingClientModules == 0);

    clientPump = clientService.pumpOnce(2700000);
    assert(clientPump.active);
    assert(!clientPump.firstFrameTimedOut);
    assert(clientControlChannel->sentPackets == 1);

    clientModule->handlePacket(
        makeKeyframeFailureResponse(
            clientModule->snapshot().lastKeyframeRequestId));
    assert(clientModule->snapshot().pendingKeyframeRequests == 0);
    clientModule->handlePacket(makeVideoPacket(clientSessionId, 1, true));
    assert(clientModule->snapshot().renderedFrames == 1);

    module::ModuleReconnectOptions reconnectOptions;
    reconnectOptions.requestFreshState = true;
    reconnectOptions.affectedChannels = {screenKey()};
    clientModule->resumeAfterReconnect(reconnectOptions);
    assert(clientModule->snapshot().reconnectResumes == 1);
    assert(clientModule->snapshot().reconnectRenderedFramesBaseline == 1);
    assert(clientControlChannel->sentPackets == 2);

    clientModule->handlePacket(
        makeKeyframeFailureResponse(
            clientModule->snapshot().lastKeyframeRequestId));
    assert(clientModule->snapshot().pendingKeyframeRequests == 0);

    clientPump = clientService.pumpOnce(3000000);
    assert(clientPump.active);
    assert(!clientPump.reconnectFrameTimedOut);
    assert(clientControlChannel->sentPackets == 2);

    clientPump = clientService.pumpOnce(3600000);
    assert(clientPump.active);
    assert(clientPump.reconnectFrameTimedOut);
    assert(clientPump.keyframeRequestSent);
    assert(clientControlChannel->sentPackets == 3);

    const runtime::display::DisplayRuntimeServiceSnapshot reconnectSnapshot =
        clientService.snapshot();
    assert(reconnectSnapshot.reconnectFrameTimeouts == 1);
    assert(reconnectSnapshot.keyframeRequestsSent == 2);
    assert(reconnectSnapshot.keyframeRequestFailures == 0);

    clientService.stop();
    assert(!clientService.snapshot().active);
    return 0;
}
