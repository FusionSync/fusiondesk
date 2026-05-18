#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
#include <QApplication>
#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"
#else
#include <QCoreApplication>
#endif

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_manager.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/modules/display/display_frame_codec.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"
#include "fusiondesk/runtime/display/display_codec_peer_profile.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_policy.h"
#include "pc_product_session_controller.h"

namespace {

class RecordingChannel final : public fusiondesk::network::IChannel
{
public:
    explicit RecordingChannel(fusiondesk::network::ChannelKey key)
        : key_(key)
    {
    }

    fusiondesk::protocol::ChannelId id() const override
    {
        return key_.channelId;
    }

    fusiondesk::protocol::ChannelType type() const override
    {
        return key_.channelType;
    }

    bool isOpen() const override
    {
        return true;
    }

    fusiondesk::network::SendResult send(
        const fusiondesk::protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return fusiondesk::network::SendResult::sent();
    }

    std::vector<fusiondesk::protocol::PacketEnvelope> sentPackets;

private:
    fusiondesk::network::ChannelKey key_;
};

class FakeDisplayCapture final
    : public fusiondesk::modules::display::IDisplayCapture
{
public:
    fusiondesk::modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        fusiondesk::modules::display::CapturedFrame frame;
        frame.width = 2;
        frame.height = 2;
        frame.strideBytes = 8;
        frame.pixelFormat =
            fusiondesk::modules::display::DisplayPixelFormat::Bgra32;
        frame.frameId = ++frameId_;
        frame.keyFrame = keyFrame;
        frame.pixels.assign(16, 0xff);
        return frame;
    }

    std::string backendId() const override
    {
        return "test.capture";
    }

private:
    std::uint64_t frameId_ = 0;
};

class FakeClipboardEndpoint final
    : public fusiondesk::modules::clipboard::IClipboardEndpoint
{
public:
    fusiondesk::modules::clipboard::ClipboardSnapshot snapshot() override
    {
        fusiondesk::modules::clipboard::ClipboardSnapshot result;
        result.ownerEpoch = bundle.ownerEpoch;
        result.sequence = bundle.sequence;
        result.bundle = bundle;
        return result;
    }

    fusiondesk::protocol::ResponseStatus publishBundle(
        const fusiondesk::modules::clipboard::ClipboardPublishRequest& request) override
    {
        bundle = request.bundle;
        ++published;
        return fusiondesk::protocol::ResponseStatus::Ok;
    }

    fusiondesk::protocol::ResponseStatus clearPublishedBundle(
        fusiondesk::modules::clipboard::TransferOfferId offerId) override
    {
        if (bundle.offerId == offerId)
            bundle = {};
        return fusiondesk::protocol::ResponseStatus::Ok;
    }

    fusiondesk::modules::clipboard::TransferSourceBundle bundle;
    int published = 0;
};

fusiondesk::protocol::ByteBuffer bytes(const std::string& value)
{
    return fusiondesk::protocol::ByteBuffer(value.begin(), value.end());
}

fusiondesk::modules::clipboard::TransferSourceBundle clipboardTextBundle()
{
    using namespace fusiondesk::modules::clipboard;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.formatId = 55;
    descriptor.estimatedBytes = 5;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("hello");

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            77,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

fusiondesk::runtime::RuntimeOptions clipboardRuntimeOptions()
{
    fusiondesk::runtime::RuntimeOptions options;
    options.profile.profileId = "pc-product-clipboard-controller-smoke";
    options.profile.defaultFeatures.bits =
        fusiondesk::protocol::feature::Clipboard;
    options.profile.requiredModules = {"clipboard.redirect"};
    options.profile.minimumChannels =
        fusiondesk::network::defaultMvpChannelSpecs();
    options.profile.clipboardPolicy.runtimeRules.auditAllowed = true;
    return options;
}

std::shared_ptr<RecordingChannel> bindSmallData(
    fusiondesk::session::Session& session)
{
    const fusiondesk::network::ChannelKey smallData{
        static_cast<fusiondesk::protocol::ChannelId>(
            fusiondesk::protocol::ChannelIdValue::SmallData),
        fusiondesk::protocol::ChannelType::Standard};
    auto channel = std::make_shared<RecordingChannel>(smallData);
    assert(session.network()->bindChannel(channel).ok);

    fusiondesk::network::ChannelReadyInfo ready;
    ready.endpoint = "pc-product-clipboard-controller-smoke";
    assert(session.network()->markReady(smallData, ready).ok);
    return channel;
}

fusiondesk::runtime::display::DisplayCodecCapability rawCodecCapability(
    std::string adapterId)
{
    fusiondesk::runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform =
        fusiondesk::runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend =
        fusiondesk::runtime::display::DisplayCodecBackendKind::RawFrame;
    capability.codec =
        fusiondesk::runtime::display::DisplayCodecId::RawBgra;
    capability.pixelFormats = {
        fusiondesk::modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        fusiondesk::runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        fusiondesk::runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.fallback = true;
    capability.lowLatency = true;
    capability.priority = 5;
    return capability;
}

fusiondesk::runtime::display::DisplayCodecCapability h264CodecCapability(
    std::string adapterId)
{
    fusiondesk::runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform =
        fusiondesk::runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend =
        fusiondesk::runtime::display::DisplayCodecBackendKind::
            WindowsMediaFoundation;
    capability.codec = fusiondesk::runtime::display::DisplayCodecId::H264;
    capability.pixelFormats = {
        fusiondesk::modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        fusiondesk::runtime::display::DisplayCodecMemoryType::D3DTexture,
        fusiondesk::runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        fusiondesk::runtime::display::DisplayCodecMemoryType::D3DTexture,
        fusiondesk::runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.hardwareAccelerated = true;
    capability.zeroCopy = true;
    capability.lowLatency = true;
    capability.requiresHardwareDevice = true;
    capability.maxWidth = 8192;
    capability.maxHeight = 8192;
    capability.priority = 90;
    return capability;
}

fusiondesk::runtime::display::DisplayCodecSelectionRequest codecRequest(
    fusiondesk::runtime::display::DisplayCodecDirection direction,
    std::vector<fusiondesk::runtime::display::DisplayCodecCapability> candidates)
{
    fusiondesk::runtime::display::DisplayCodecSelectionRequest request;
    request.platform =
        fusiondesk::runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = direction;
    request.codecPreference = {
        fusiondesk::runtime::display::DisplayCodecId::H264,
        fusiondesk::runtime::display::DisplayCodecId::RawBgra};
    request.acceptedInputMemoryTypes =
        fusiondesk::runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.acceptedOutputMemoryTypes =
        fusiondesk::runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.architecture =
        fusiondesk::runtime::display::DisplayTargetArchitecture::X86_64;
    request.socProfile =
        fusiondesk::runtime::display::DisplayTargetSocProfile::Generic;
    request.width = 1920;
    request.height = 1080;
    request.candidates = std::move(candidates);
    return request;
}

fusiondesk::runtime::qt::QtPeerProfileRuntimeServiceSnapshot
snapshotWithDisplayCodecCompletion(
    const fusiondesk::runtime::connection::PeerProfileExchangeResult& exchange)
{
    fusiondesk::runtime::qt::QtPeerProfileRuntimeServiceSnapshot snapshot;
    fusiondesk::runtime::connection::PeerProfileRuntimeCompletion completion;
    completion.terminal = true;
    completion.ok = true;
    completion.exchange = exchange;
    snapshot.runtime.completions.push_back(std::move(completion));
    return snapshot;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    QApplication application(argc, argv);
#else
    QCoreApplication application(argc, argv);
#endif

    fusiondesk::apps::pc::PcProductSessionController controller;
    fusiondesk::apps::pc::PcProductSessionStartOptions startOptions;
    startOptions.role = fusiondesk::apps::pc::PcShellRole::Agent;

    const fusiondesk::apps::pc::PcProductSessionOperationResult started =
        controller.start(startOptions);
    assert(started.ok);
    assert(controller.active());
    assert(controller.sessionId() != 0);
    assert(controller.session() != nullptr);
    assert(controller.transportManager() != nullptr);
    assert(controller.reconnectService() != nullptr);

    fusiondesk::apps::pc::PcProductPeerProfileStartOptions peerStart;
    peerStart.runtime.startResponder = false;
    peerStart.runtime.subscribeResponses = true;
    peerStart.startTimer = false;
    peerStart.firstMessageId = 31000;
    const fusiondesk::apps::pc::PcProductPeerProfileResult peerStarted =
        controller.startPeerProfileService(peerStart);
    assert(peerStarted.ok);
    assert(controller.peerProfileService() != nullptr);
    assert(controller.snapshot().peerProfileActive);

    fusiondesk::runtime::connection::PeerProfileExchangeRequest peerRequest;
    const fusiondesk::network::ChannelSpec screenSpec =
        fusiondesk::network::defaultMvpChannelSpecs().back();
    peerRequest.connectionPlan.knownSpecs =
        fusiondesk::network::defaultMvpChannelSpecs();
    peerRequest.connectionPlan.channels = {
        fusiondesk::runtime::connection::PeerConnectionChannelRequest{
            screenSpec.key,
            "127.0.0.1:9",
            "client-screen-ready-from-controller",
            "agent-screen-ready-from-controller"}};
    peerRequest.clientSessionId = controller.sessionId();
    peerRequest.agentSessionId = controller.sessionId();
    fusiondesk::runtime::connection::PeerProfileRuntimeExchangeOptions peerOptions;
    peerOptions.wire.messageId = 0;
    peerOptions.wire.timeoutMs = 100;
    peerOptions.wire.monotonicTimestampUsec = 1000;
    const fusiondesk::apps::pc::PcProductPeerProfileResult peerRequested =
        controller.requestPeerProfile(peerRequest, peerOptions);
    assert(!peerRequested.ok);
    assert(peerRequested.code == 13);
    assert(peerRequested.request.messageId == 31000);

    fusiondesk::runtime::display::StaticDisplayCodecBackendFactory
        h264ClientFactory({rawCodecCapability("windows.raw_frame"),
                           h264CodecCapability("windows.media_foundation.h264")});
    fusiondesk::apps::pc::PcProductDisplayCodecInventoryOptions
        codecInventoryOptions;
    codecInventoryOptions.useControllerProductPolicy = false;
    codecInventoryOptions.policy.codecPreference = {
        fusiondesk::runtime::display::DisplayCodecId::H264,
        fusiondesk::runtime::display::DisplayCodecId::RawBgra};
    codecInventoryOptions.architecture =
        fusiondesk::runtime::display::DisplayTargetArchitecture::X86_64;
    codecInventoryOptions.socProfile =
        fusiondesk::runtime::display::DisplayTargetSocProfile::Generic;
    codecInventoryOptions.width = 1920;
    codecInventoryOptions.height = 1080;
    const fusiondesk::apps::pc::PcProductDisplayCodecInventoryResult
        decoderInventory =
            controller.buildDisplayCodecInventoryRequest(
                fusiondesk::runtime::display::DisplayCodecDirection::Decode,
                h264ClientFactory,
                codecInventoryOptions);
    assert(decoderInventory.ok);
    assert(decoderInventory.request.direction ==
           fusiondesk::runtime::display::DisplayCodecDirection::Decode);
    assert(decoderInventory.request.candidates.size() == 2);

    fusiondesk::runtime::connection::PeerProfileExchangeRequest codecPeerRequest;
    const fusiondesk::apps::pc::PcProductDisplayCodecPeerProfileResult
        codecRequestAdded =
            controller.appendDisplayCodecPeerProfileDecoderRequest(
                codecPeerRequest,
                decoderInventory.request);
    assert(codecRequestAdded.ok);
    assert(codecRequestAdded.extensionAdded);
    assert(codecPeerRequest.extensions.size() == 1);
    const fusiondesk::runtime::display::DisplayCodecPeerProfileDecodeResult
        decodedCodecRequest =
            fusiondesk::runtime::display::decodeDisplayCodecPeerProfilePayload(
                codecPeerRequest.extensions.front().payload);
    assert(decodedCodecRequest.ok);
    assert(decodedCodecRequest.payload.hasDecoderRequest);
    assert(decodedCodecRequest.payload.decoderRequest.direction ==
           fusiondesk::runtime::display::DisplayCodecDirection::Decode);

    fusiondesk::runtime::connection::PeerProfileExchangeResult codecExchange;
    codecExchange.ok = true;
    fusiondesk::runtime::display::StaticDisplayCodecBackendFactory
        h264AgentFactory({rawCodecCapability("windows.raw_frame"),
                          h264CodecCapability("windows.media_foundation.h264")});
    const fusiondesk::apps::pc::PcProductDisplayCodecInventoryResult
        encoderInventory =
            controller.buildDisplayCodecInventoryRequest(
                fusiondesk::runtime::display::DisplayCodecDirection::Encode,
                h264AgentFactory,
                codecInventoryOptions);
    assert(encoderInventory.ok);
    const fusiondesk::apps::pc::PcProductDisplayCodecPeerProfileResult
        codecAgentHandled =
            controller.handleDisplayCodecPeerProfileRequest(
                codecPeerRequest,
                codecExchange,
                encoderInventory.request);
    assert(codecAgentHandled.ok);
    assert(codecAgentHandled.extensionAdded);
    assert(codecExchange.ok);
    assert(codecExchange.extensions.size() == 1);
    assert(codecAgentHandled.negotiation.codec ==
           fusiondesk::runtime::display::DisplayCodecId::H264);
    assert(codecAgentHandled.encoderRequest.requestedAdapterId ==
           "windows.media_foundation.h264");
    assert(codecAgentHandled.decoderRequest.requestedAdapterId ==
           "windows.media_foundation.h264");
    const fusiondesk::apps::pc::PcProductDisplayCodecInventoryResult
        pinnedAgentInventory =
            controller.pinDisplayCodecInventoryRequest(
                fusiondesk::apps::pc::PcShellRole::Agent,
                encoderInventory.request,
                codecAgentHandled.negotiation);
    assert(pinnedAgentInventory.ok);
    assert(pinnedAgentInventory.request.requestedAdapterId ==
           "windows.media_foundation.h264");
    const fusiondesk::apps::pc::PcProductDisplayCodecInventoryResult
        pinnedClientInventory =
            controller.pinDisplayCodecInventoryRequest(
                fusiondesk::apps::pc::PcShellRole::Client,
                decoderInventory.request,
                codecAgentHandled.negotiation);
    assert(pinnedClientInventory.ok);
    assert(pinnedClientInventory.request.requestedAdapterId ==
           "windows.media_foundation.h264");

    const fusiondesk::apps::pc::PcProductDisplayCodecPeerProfileResult
        codecClientRead =
            controller.readDisplayCodecPeerProfileCompletion(
                snapshotWithDisplayCodecCompletion(codecExchange));
    assert(codecClientRead.ok);
    assert(codecClientRead.negotiation.codec ==
           fusiondesk::runtime::display::DisplayCodecId::H264);

    fusiondesk::runtime::connection::PeerProfileExchangeRequest rawPeerRequest;
    const fusiondesk::apps::pc::PcProductDisplayCodecPeerProfileResult
        rawRequestAdded =
            controller.appendDisplayCodecPeerProfileDecoderRequest(
                rawPeerRequest,
                codecRequest(
                    fusiondesk::runtime::display::DisplayCodecDirection::Decode,
                    {rawCodecCapability("windows.raw_frame")}));
    assert(rawRequestAdded.ok);
    fusiondesk::runtime::connection::PeerProfileExchangeResult rawExchange;
    rawExchange.ok = true;
    const fusiondesk::apps::pc::PcProductDisplayCodecPeerProfileResult
        rawAgentHandled =
            controller.handleDisplayCodecPeerProfileRequest(
                rawPeerRequest,
                rawExchange,
                codecRequest(
                    fusiondesk::runtime::display::DisplayCodecDirection::Encode,
                    {rawCodecCapability("windows.raw_frame"),
                     h264CodecCapability("windows.media_foundation.h264")}));
    assert(rawAgentHandled.ok);
    assert(rawAgentHandled.negotiation.codec ==
           fusiondesk::runtime::display::DisplayCodecId::RawBgra);
    assert(rawAgentHandled.negotiation.fallbackSelected);

    fusiondesk::runtime::display::RawFrameDisplayCodecBackendFactory
        rawCodecFactory(
            fusiondesk::runtime::display::DisplayPlatformFamily::WindowsDesktop);
    fusiondesk::apps::pc::PcProductDisplayCodecInventoryOptions
        rawCodecOptions = codecInventoryOptions;
    rawCodecOptions.codecPreferenceOverride = {
        fusiondesk::runtime::display::DisplayCodecId::RawBgra};
    rawCodecOptions.policy.selectionMode = "controller-smoke";
    const fusiondesk::apps::pc::PcProductDisplayCodecInventoryResult
        rawEncoderInventory =
            controller.buildDisplayCodecInventoryRequest(
                fusiondesk::runtime::display::DisplayCodecDirection::Encode,
                rawCodecFactory,
                rawCodecOptions);
    assert(rawEncoderInventory.ok);
    fusiondesk::apps::pc::PcProductDisplayCodecCreateOptions createCodecOptions;
    createCodecOptions.useControllerProductPolicy = false;
    createCodecOptions.policy = rawCodecOptions.policy;
    const fusiondesk::apps::pc::PcProductDisplayCodecCreateResult
        rawEncoderCreated =
            controller.createDisplayEncoder(rawCodecFactory,
                                            rawEncoderInventory.request,
                                            createCodecOptions);
    assert(rawEncoderCreated.ok);
    assert(rawEncoderCreated.encoder != nullptr);
    assert(rawEncoderCreated.runtimeInfo.selected);
    assert(rawEncoderCreated.runtimeInfo.adapterId == "windows.raw_frame");
    assert(rawEncoderCreated.runtimeInfo.selectionMode == "controller-smoke");

    const fusiondesk::apps::pc::PcProductDisplayCodecInventoryResult
        rawDecoderInventory =
            controller.buildDisplayCodecInventoryRequest(
                fusiondesk::runtime::display::DisplayCodecDirection::Decode,
                rawCodecFactory,
                rawCodecOptions);
    assert(rawDecoderInventory.ok);
    const fusiondesk::apps::pc::PcProductDisplayCodecCreateResult
        rawDecoderCreated =
            controller.createDisplayDecoder(rawCodecFactory,
                                            rawDecoderInventory.request,
                                            createCodecOptions);
    assert(rawDecoderCreated.ok);
    assert(rawDecoderCreated.decoder != nullptr);
    assert(rawDecoderCreated.runtimeInfo.selected);
    assert(rawDecoderCreated.runtimeInfo.adapterId == "windows.raw_frame");

    fusiondesk::apps::pc::PcProductDisplayDependenciesOptions
        agentDisplayDependencies;
    agentDisplayDependencies.role = fusiondesk::apps::pc::PcShellRole::Agent;
    agentDisplayDependencies.capture = std::make_shared<FakeDisplayCapture>();
    agentDisplayDependencies.encoder = rawEncoderCreated;
    const fusiondesk::apps::pc::PcProductDisplayDependenciesResult
        builtAgentDisplayDependencies =
            controller.buildDisplayDependencies(agentDisplayDependencies);
    assert(builtAgentDisplayDependencies.ok);
    assert(builtAgentDisplayDependencies.dependencies.capture != nullptr);
    assert(builtAgentDisplayDependencies.dependencies.encoder != nullptr);
    assert(builtAgentDisplayDependencies.dependencies.encoderCodec.adapterId ==
           "windows.raw_frame");

    fusiondesk::runtime::connection::PeerProfileExchangeRequest missingCodecRequest;
    fusiondesk::runtime::connection::PeerProfileExchangeResult missingCodecExchange;
    missingCodecExchange.ok = true;
    const fusiondesk::apps::pc::PcProductDisplayCodecPeerProfileResult
        missingCodec =
            controller.handleDisplayCodecPeerProfileRequest(
                missingCodecRequest,
                missingCodecExchange,
                codecRequest(
                    fusiondesk::runtime::display::DisplayCodecDirection::Encode,
                    {rawCodecCapability("windows.raw_frame")}));
    assert(!missingCodec.ok);
    assert(missingCodec.code == 14);
    assert(!missingCodecExchange.ok);

    controller.stopPeerProfileService();
    assert(controller.peerProfileService() == nullptr);

    const fusiondesk::apps::pc::PcProductSessionSnapshot created =
        controller.snapshot();
    assert(created.active);
    assert(created.reconnectActive);
    assert(created.connectorCount == 0);
    assert(created.displayHealth.statusCode == "display.module_missing");
    assert(created.displayHealth.primaryActionCode ==
           "module.mount_display");

    const fusiondesk::apps::pc::PcProductSessionOperationResult mounted =
        controller.mountDisplayDependencies(agentDisplayDependencies);
    assert(mounted.ok);

    const fusiondesk::apps::pc::PcProductSessionSnapshot mountedSnapshot =
        controller.snapshot();
    assert(mountedSnapshot.diagnostics.mountedModules == 1);
    assert(mountedSnapshot.displayHealth.statusCode ==
           "display.channel_blocked");
    assert(mountedSnapshot.displayHealth.captureState.find("test.capture") !=
           std::string::npos);
    assert(mountedSnapshot.displayHealth.codecState.find("windows.raw_frame") !=
           std::string::npos);

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    fusiondesk::adapters::qt::display::QtImageDisplayRenderer renderer;
    const fusiondesk::apps::pc::PcProductDisplayWindowResult windowShown =
        controller.showDisplayWindow(renderer);
    assert(windowShown.ok);
    assert(windowShown.supported);
    assert(windowShown.statusText.find("display.channel_blocked") !=
           std::string::npos);
    assert(controller.snapshot().displayWindowStatusText.find(
               "network.bind_required_channels") != std::string::npos);
    controller.closeDisplayWindow();
    assert(!controller.displayWindowVisible());
#endif

    fusiondesk::apps::pc::PcProductDisplayRuntimeStartOptions displayOptions;
    displayOptions.runtime.pumpAgentFrames = true;
    displayOptions.runtime.targetFps = 30;
    displayOptions.startPumpTimer = false;
    const fusiondesk::apps::pc::PcProductDisplayRuntimeResult displayStarted =
        controller.startDisplayRuntime(displayOptions);
    assert(displayStarted.ok);
    assert(displayStarted.snapshot.active);
    assert(controller.displayRuntimeService() != nullptr);
    assert(controller.displayRuntimeSnapshot().targetFps == 30);
    assert(controller.snapshot().displayRuntimeActive);
    controller.stopDisplayRuntime();
    assert(controller.displayRuntimeService() == nullptr);

    const fusiondesk::apps::pc::PcProductSessionOperationResult startModules =
        controller.startProfileModules();
    assert(!startModules.ok);
    assert(startModules.code == 8);
    assert(!startModules.mainline.blockedModules.empty());

    controller.stop("pc product session controller smoke complete");
    assert(!controller.active());

    fusiondesk::apps::pc::PcProductSessionController clipboardController;
    fusiondesk::apps::pc::PcProductSessionStartOptions clipboardStartOptions;
    clipboardStartOptions.role = fusiondesk::apps::pc::PcShellRole::Agent;
    clipboardStartOptions.runtimeOptions = clipboardRuntimeOptions();
    const fusiondesk::apps::pc::PcProductSessionOperationResult
        clipboardStarted = clipboardController.start(clipboardStartOptions);
    assert(clipboardStarted.ok);

    auto clipboardEndpoint = std::make_shared<FakeClipboardEndpoint>();
    clipboardEndpoint->bundle = clipboardTextBundle();
    fusiondesk::runtime::DisplayMvpDependencies clipboardDependencies;
    clipboardDependencies.clipboardEndpoint = clipboardEndpoint;
    clipboardDependencies.clipboardPolicy =
        std::make_shared<fusiondesk::modules::clipboard::ClipboardPolicy>();
    const fusiondesk::apps::pc::PcProductSessionOperationResult
        clipboardMounted =
            clipboardController.mountProfileModules(clipboardDependencies);
    assert(clipboardMounted.ok);

    std::shared_ptr<RecordingChannel> clipboardChannel =
        bindSmallData(*clipboardController.session());
    const fusiondesk::apps::pc::PcProductSessionOperationResult
        clipboardModulesStarted = clipboardController.startProfileModules();
    assert(clipboardModulesStarted.ok);

    fusiondesk::apps::pc::PcProductClipboardRuntimeStartOptions
        clipboardRuntimeStart;
    clipboardRuntimeStart.runtime.endpoint = clipboardEndpoint;
    clipboardRuntimeStart.startPumpTimer = false;
    const fusiondesk::apps::pc::PcProductClipboardRuntimeResult
        clipboardRuntimeStarted =
            clipboardController.startClipboardRuntime(clipboardRuntimeStart);
    assert(clipboardRuntimeStarted.ok);
    assert(clipboardController.clipboardRuntimeService() != nullptr);
    assert(clipboardController.snapshot().clipboardRuntimeActive);

    const fusiondesk::runtime::feature::ClipboardRuntimePumpResult
        clipboardPump = clipboardController.pumpClipboardOnce();
    assert(clipboardPump.announcementsSent == 1);
    assert(clipboardChannel->sentPackets.size() == 1);
    assert(clipboardController.clipboardRuntimeSnapshot().announcementsSent == 1);
    assert(clipboardController.clipboardRuntimePolicySnapshot().auditEvents == 1);
    const fusiondesk::apps::pc::PcProductSessionSnapshot clipboardSnapshot =
        clipboardController.snapshot();
    assert(clipboardSnapshot.clipboardProductPolicy.usable);
    assert(clipboardSnapshot.clipboardProductPolicy.modeCode ==
           "clipboard.policy.open");
    assert(clipboardSnapshot.clipboardProductPolicy.auditEnabled);
    assert(clipboardSnapshot.clipboardProductPolicy.auditState ==
           "audit.enabled");
    assert(clipboardSnapshot.clipboardRuntimePolicy.auditedAllowed == 1);
    assert(clipboardSnapshot.clipboardHealth.usable);
    assert(clipboardSnapshot.clipboardHealth.statusCode ==
           "clipboard.offer_active");
    assert(clipboardSnapshot.clipboardHealth.policyState ==
           "policy.allowed.local_snapshot_announce");
    assert(clipboardSnapshot.clipboardHealth.showAuditIndicator);
    clipboardController.stopClipboardRuntime();
    assert(clipboardController.clipboardRuntimeService() == nullptr);
    assert(!clipboardController.snapshot().clipboardRuntimeActive);
    clipboardController.stop("pc product clipboard controller smoke complete");
    assert(!clipboardController.active());
    return 0;
}
