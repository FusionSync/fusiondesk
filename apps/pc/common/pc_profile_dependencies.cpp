#include "pc_profile_dependencies.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "pc_clipboard_shell.h"
#include "pc_display_diagnostics.h"
#include "pc_profile_options.h"
#include "pc_shell_options.h"

#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
#include "fusiondesk/adapters/qt/input/qt_input_capture.h"
#endif
#if defined(FUSIONDESK_PC_HAS_QT_IMAGE_DISPLAY)
#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"
#endif
#include "fusiondesk/modules/display/display_frame_codec.h"
#include "fusiondesk/modules/display/display_modules.h"
#if defined(FUSIONDESK_PC_HAS_WINDOWS_DISPLAY_CAPTURE)
#include "fusiondesk/platform/windows/display/windows_display_capture_factory.h"
#endif
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
#include "fusiondesk/platform/windows/input/windows_input_injector.h"
#endif
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"
#include "fusiondesk/runtime/display/display_capture_options.h"
#include "fusiondesk/runtime/display/display_capture_platform_plan.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"
#include "fusiondesk/runtime/display/display_codec_peer_profile.h"
#include "fusiondesk/runtime/display/display_codec_selection.h"

namespace fusiondesk {
namespace apps {
namespace pc {
runtime::display::DisplayCapturePlatformPlanRequest
makeDisplayCapturePlatformPlanRequest(int argc, char** argv)
{
    runtime::display::DisplayCapturePlatformPlanRequest request;
    request.role = runtime::display::DisplayCaptureRuntimeRole::Agent;
    request.platform = displayTargetPlatformOptionValue(argc, argv);
    request.sourceType = displayCaptureSourceTypeOptionValue(argc, argv);
    request.requestedAdapterId = displayCaptureBackendOptionValue(argc, argv);
    request.architecture = displayTargetArchitectureOptionValue(argc, argv);
    request.socProfile = displayTargetSocProfileOptionValue(argc, argv);
    request.acceptedMemoryTypes = {
        runtime::display::DisplayCaptureMemoryType::CpuBuffer};
    request.acceptedPixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    return request;
}

const runtime::connection::PeerProfileExtension* findDisplayCodecPeerExtension(
    const std::vector<runtime::connection::PeerProfileExtension>& extensions)
{
    for (const runtime::connection::PeerProfileExtension& extension : extensions) {
        if (extension.key == runtime::display::displayCodecPeerProfileExtensionKey())
            return &extension;
    }
    return nullptr;
}

runtime::connection::PeerProfileExtension makeDisplayCodecPeerExtension(
    const runtime::display::DisplayCodecPeerProfilePayload& payload)
{
    runtime::connection::PeerProfileExtension extension;
    extension.key = runtime::display::displayCodecPeerProfileExtensionKey();
    extension.payload = runtime::display::encodeDisplayCodecPeerProfilePayload(payload);
    return extension;
}

runtime::display::DisplayCodecSelectionRequest pinnedDisplayCodecRequest(
    runtime::display::DisplayCodecSelectionRequest request,
    const runtime::display::DisplayCodecSelectionResult& selection)
{
    if (!selection.hasSelection) {
        request.codecPreference = {runtime::display::DisplayCodecId::Unknown};
        request.requestedAdapterId = "display.codec.negotiation.failed";
        request.candidates.clear();
        return request;
    }

    request.codecPreference = {selection.selected.codec};
    request.requestedAdapterId = selection.selected.adapterId;
    request.candidates = {selection.selected};
    return request;
}

std::string displayCodecSelectionMode(
    const runtime::ProductDisplayCodecPolicy& policy)
{
    return effectiveDisplayCodecPolicy(policy).selectionMode;
}

std::string displayCodecFallbackReason(
    const runtime::display::DisplayCodecSelectionResult& selection,
    const std::vector<std::string>& messages)
{
    if (!selection.fallbackSelected)
        return {};

    for (const runtime::display::DisplayCodecRejection& rejection :
         selection.rejected) {
        if (rejection.codec == runtime::display::DisplayCodecId::H264)
            return rejection.adapterId + ": " + rejection.reason;
    }
    if (!selection.rejected.empty()) {
        const runtime::display::DisplayCodecRejection& rejection =
            selection.rejected.front();
        return rejection.adapterId + ": " + rejection.reason;
    }
    return messages.empty() ? std::string() : messages.front();
}

modules::display::DisplayCodecRuntimeInfo displayCodecRuntimeInfoFromSelection(
    const runtime::display::DisplayCodecSelectionResult& selection,
    const std::vector<std::string>& messages,
    const runtime::ProductDisplayCodecPolicy& policy)
{
    modules::display::DisplayCodecRuntimeInfo info;
    const runtime::ProductDisplayCodecPolicy effectivePolicy =
        effectiveDisplayCodecPolicy(policy);
    info.selectionMode = displayCodecSelectionMode(effectivePolicy);
    if (!selection.hasSelection) {
        info.messages = messages;
        return info;
    }

    const runtime::display::DisplayCodecCapability& selected =
        selection.selected;
    info.selected = true;
    info.adapterId = selected.adapterId;
    info.codec = runtime::display::displayCodecIdName(selected.codec);
    info.backend = runtime::display::displayCodecBackendKindName(selected.backend);
    info.fallback = selected.fallback;
    info.hardwareAccelerated = selected.hardwareAccelerated;
    info.zeroCopy = selected.zeroCopy;
    info.lowLatency = selected.lowLatency;
    info.deltaFrames =
        selected.codec == runtime::display::DisplayCodecId::H264 &&
        effectivePolicy.enableH264DeltaFrames;
    info.fallbackReason = displayCodecFallbackReason(selection, messages);
    info.messages = messages;
    for (const runtime::display::DisplayCodecRejection& rejection :
         selection.rejected) {
        info.messages.push_back(rejection.adapterId + ": " + rejection.reason);
    }
    return info;
}

bool applyPeerCodecNegotiationToRequest(
    PcShellRole role,
    const DisplayCodecPeerNegotiationState* peerNegotiation,
    runtime::display::DisplayCodecSelectionRequest& request)
{
    if (peerNegotiation == nullptr || !peerNegotiation->attempted)
        return false;

    if (!peerNegotiation->ok) {
        request.codecPreference = {runtime::display::DisplayCodecId::Unknown};
        request.requestedAdapterId = "display.codec.negotiation.failed";
        return true;
    }

    const runtime::display::DisplayCodecSelectionResult& selection =
        role == PcShellRole::Agent
            ? peerNegotiation->negotiation.encoderSelection
            : peerNegotiation->negotiation.decoderSelection;
    request = pinnedDisplayCodecRequest(request, selection);
    return true;
}

bool appendClientDisplayCodecPeerProfileExtension(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy,
    runtime::connection::PeerProfileExchangeRequest& request,
    std::vector<std::string>& messages)
{
    std::shared_ptr<runtime::display::IDisplayCodecBackendFactory> codecFactory =
        makeDisplayCodecBackendFactory(displayTargetPlatformOptionValue(argc, argv),
                                       policy);
    runtime::display::DisplayCodecSelectionRequest decoderRequest =
        makeDisplayCodecSelectionRequest(
            argc,
            argv,
            runtime::display::DisplayCodecDirection::Decode,
            policy);
    decoderRequest.candidates = codecFactory->capabilities();

    runtime::display::DisplayCodecPeerProfilePayload payload;
    payload.hasDecoderRequest = true;
    payload.decoderRequest = std::move(decoderRequest);
    runtime::connection::PeerProfileExtension extension =
        makeDisplayCodecPeerExtension(payload);
    if (extension.payload.empty()) {
        messages.push_back("display codec FDPP request payload encode failed");
        return false;
    }

    request.extensions.push_back(std::move(extension));
    return true;
}

runtime::connection::PeerProfileExchangeResult
handleAgentDisplayCodecPeerProfileExchange(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy,
    const runtime::connection::PeerProfileExchangeRequest& request,
    const runtime::connection::PeerProfileExchangeResult& exchange,
    DisplayCodecPeerNegotiationState& state)
{
    runtime::connection::PeerProfileExchangeResult result = exchange;
    if (!result.ok)
        return result;

    state.attempted = true;
    const runtime::connection::PeerProfileExtension* extension =
        findDisplayCodecPeerExtension(request.extensions);
    if (extension == nullptr) {
        state.ok = false;
        state.messages = {"display codec FDPP request extension is missing"};
        result.ok = false;
        result.messages.insert(result.messages.end(),
                               state.messages.begin(),
                               state.messages.end());
        return result;
    }

    const runtime::display::DisplayCodecPeerProfileDecodeResult decoded =
        runtime::display::decodeDisplayCodecPeerProfilePayload(extension->payload);
    if (!decoded.ok || !decoded.payload.hasDecoderRequest) {
        state.ok = false;
        state.messages = {decoded.message.empty()
                              ? "display codec FDPP decoder request is missing"
                              : decoded.message};
        result.ok = false;
        result.messages.insert(result.messages.end(),
                               state.messages.begin(),
                               state.messages.end());
        return result;
    }

    std::shared_ptr<runtime::display::IDisplayCodecBackendFactory> codecFactory =
        makeDisplayCodecBackendFactory(displayTargetPlatformOptionValue(argc, argv),
                                       policy);
    runtime::display::DisplayCodecSelectionRequest encoderRequest =
        makeDisplayCodecSelectionRequest(
            argc,
            argv,
            runtime::display::DisplayCodecDirection::Encode,
            policy);
    encoderRequest.candidates = codecFactory->capabilities();

    runtime::display::DisplayCodecNegotiationRequest negotiationRequest;
    negotiationRequest.encoderRequest = std::move(encoderRequest);
    negotiationRequest.decoderRequest = decoded.payload.decoderRequest;
    state.negotiation =
        runtime::display::negotiateDisplayCodec(negotiationRequest);
    state.ok = state.negotiation.ok;
    state.messages = state.negotiation.messages;

    if (displayCodecPlanDiagnosticsRequested(argc, argv))
        writeDisplayCodecNegotiationDiagnostics(state.negotiation,
                                                "peer_profile_agent_response");

    if (!state.negotiation.ok) {
        result.ok = false;
        result.messages.insert(result.messages.end(),
                               state.negotiation.messages.begin(),
                               state.negotiation.messages.end());
        return result;
    }

    runtime::display::DisplayCodecPeerProfilePayload responsePayload;
    responsePayload.hasEncoderRequest = true;
    responsePayload.encoderRequest = pinnedDisplayCodecRequest(
        negotiationRequest.encoderRequest,
        state.negotiation.encoderSelection);
    responsePayload.hasDecoderRequest = true;
    responsePayload.decoderRequest = pinnedDisplayCodecRequest(
        negotiationRequest.decoderRequest,
        state.negotiation.decoderSelection);

    runtime::connection::PeerProfileExtension responseExtension =
        makeDisplayCodecPeerExtension(responsePayload);
    if (responseExtension.payload.empty()) {
        result.ok = false;
        result.messages.push_back("display codec FDPP response payload encode failed");
        state.ok = false;
        state.messages = result.messages;
        return result;
    }

    result.extensions.push_back(std::move(responseExtension));
    return result;
}

bool readClientDisplayCodecPeerProfileCompletion(
    int argc,
    char** argv,
    const runtime::qt::QtPeerProfileRuntimeService& peerProfileService,
    DisplayCodecPeerNegotiationState& state,
    std::vector<std::string>& messages)
{
    const runtime::qt::QtPeerProfileRuntimeServiceSnapshot snapshot =
        peerProfileService.snapshot();
    for (auto it = snapshot.runtime.completions.rbegin();
         it != snapshot.runtime.completions.rend();
         ++it) {
        if (!it->ok)
            continue;

        state.attempted = true;
        const runtime::connection::PeerProfileExtension* extension =
            findDisplayCodecPeerExtension(it->exchange.extensions);
        if (extension == nullptr) {
            state.ok = false;
            state.messages = {"display codec FDPP response extension is missing"};
            messages.insert(messages.end(),
                            state.messages.begin(),
                            state.messages.end());
            return false;
        }

        const runtime::display::DisplayCodecPeerProfileDecodeResult decoded =
            runtime::display::decodeDisplayCodecPeerProfilePayload(extension->payload);
        if (!decoded.ok ||
            !decoded.payload.hasEncoderRequest ||
            !decoded.payload.hasDecoderRequest) {
            state.ok = false;
            state.messages = {decoded.message.empty()
                                  ? "display codec FDPP response is incomplete"
                                  : decoded.message};
            messages.insert(messages.end(),
                            state.messages.begin(),
                            state.messages.end());
            return false;
        }

        runtime::display::DisplayCodecNegotiationRequest negotiationRequest;
        negotiationRequest.encoderRequest = decoded.payload.encoderRequest;
        negotiationRequest.decoderRequest = decoded.payload.decoderRequest;
        state.negotiation =
            runtime::display::negotiateDisplayCodec(negotiationRequest);
        state.ok = state.negotiation.ok;
        state.messages = state.negotiation.messages;
        if (displayCodecPlanDiagnosticsRequested(argc, argv))
            writeDisplayCodecNegotiationDiagnostics(
                state.negotiation,
                "peer_profile_client_response");
        if (!state.ok) {
            messages.insert(messages.end(),
                            state.messages.begin(),
                            state.messages.end());
            return false;
        }
        return true;
    }

    messages.push_back("display codec FDPP completion is missing");
    return false;
}

runtime::DisplayMvpDependencies makeProfileDependencies(
    PcShellRole role,
    int argc,
    char** argv,
    const runtime::ProductProfile& profile,
    DisplayCaptureRuntimeContext* captureRuntime,
    const DisplayCodecPeerNegotiationState* peerCodecNegotiation,
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> clipboardRemoteReader,
    std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
        clipboardDragCoordinateMapper)
{
    runtime::DisplayMvpDependencies dependencies;
    if (clipboardProfileRequested(argc, argv)) {
        dependencies.clipboardPolicy =
            std::make_shared<modules::clipboard::ClipboardPolicy>(
                runtime::feature::clipboardModulePolicyFromProductPolicy(
                    profile.clipboardPolicy));
    }
    if (role == PcShellRole::Agent) {
#if defined(FUSIONDESK_PC_HAS_WINDOWS_DISPLAY_CAPTURE)
        auto captureFactory =
            std::make_shared<platform::windows::display::WindowsDisplayCaptureBackendFactory>();
        runtime::display::DisplayCapturePlatformPlanRequest capturePlanRequest =
            makeDisplayCapturePlatformPlanRequest(argc, argv);
        capturePlanRequest.probedCapabilities = captureFactory->capabilities();
        const runtime::display::DisplayCapturePlatformPlan capturePlan =
            runtime::display::planDisplayCapturePlatform(capturePlanRequest);
        modules::display::DisplayCaptureOpenOptions options;
        options.sourceId = static_cast<std::uint32_t>(
            intOptionValue(argc, argv, "--display-source-id", 0));
        options.sourceType = displayCaptureSourceTypeOptionValue(argc, argv);
        options.nativeSourceHandle =
            uint64OptionValue(argc, argv, "--display-native-source-handle", 0);
        options.scaleMode = displayScaleModeOptionValue(argc, argv);
        options.targetWidth = static_cast<std::uint32_t>(
            intOptionValue(argc, argv, "--display-target-width", 0));
        options.targetHeight = static_cast<std::uint32_t>(
            intOptionValue(argc, argv, "--display-target-height", 0));
        options.preferredPixelFormat = modules::display::DisplayPixelFormat::Bgra32;
        options.includeCursor = displayIncludeCursorOptionValue(argc, argv);
        if (displayCapturePlanDiagnosticsRequested(argc, argv))
            writeDisplayCapturePlanDiagnostics(capturePlan,
                                               "profile_dependencies",
                                               displayIncludeCursorOptionValue(
                                                   argc, argv));
        if (displaySourceCatalogDiagnosticsRequested(argc, argv))
            writeDisplaySourceCatalogDiagnostics(capturePlan,
                                                 "profile_dependencies",
                                                 *captureFactory,
                                                 options);
        if (!capturePlan.ok)
            writeShellMessages(capturePlan.messages);
        runtime::display::DisplayCaptureBackendCreateResult captureCreated;
        if (capturePlan.ok) {
            captureCreated = runtime::display::createSelectedDisplayCapture(
                *captureFactory,
                capturePlan.selectionRequest);
        }
        if (!captureCreated.ok)
            writeShellMessages(captureCreated.messages);
        dependencies.captureOptions =
            runtime::display::withDefaultRawFrameCaptureTarget(options);
        dependencies.capture = captureCreated.capture;
        std::shared_ptr<runtime::display::IDisplayCodecBackendFactory>
            codecFactory = makeDisplayCodecBackendFactory(
                displayTargetPlatformOptionValue(argc, argv),
                profile.displayCodecPolicy);
        const std::vector<runtime::display::DisplayCodecCapability>
            codecCapabilities = codecFactory->capabilities();
        runtime::display::DisplayCodecSelectionRequest codecRequest =
            makeDisplayCodecSelectionRequest(
                argc,
                argv,
                runtime::display::DisplayCodecDirection::Encode,
                profile.displayCodecPolicy);
        codecRequest.candidates = codecCapabilities;
        if (applyPeerCodecNegotiationToRequest(role,
                                               peerCodecNegotiation,
                                               codecRequest)) {
            if (displayCodecPlanDiagnosticsRequested(argc, argv))
                writeDisplayCodecNegotiationDiagnostics(
                    peerCodecNegotiation->negotiation,
                    "profile_dependencies_fdpp");
            if (!peerCodecNegotiation->ok)
                writeShellMessages(peerCodecNegotiation->messages);
        } else if (displayCodecLocalNegotiationRequested(argc, argv)) {
            const runtime::display::DisplayCodecNegotiationResult negotiation =
                runtime::display::negotiateDisplayCodec(
                    makeDisplayCodecNegotiationRequest(argc,
                                                       argv,
                                                       profile.displayCodecPolicy,
                                                       codecCapabilities));
            if (displayCodecPlanDiagnosticsRequested(argc, argv))
                writeDisplayCodecNegotiationDiagnostics(
                    negotiation,
                    "profile_dependencies");
            if (!negotiation.ok)
                writeShellMessages(negotiation.messages);
            if (!negotiation.ok) {
                codecRequest.codecPreference = {
                    runtime::display::DisplayCodecId::Unknown};
                codecRequest.requestedAdapterId =
                    "display.codec.negotiation.failed";
            } else {
                codecRequest.codecPreference = {negotiation.codec};
                codecRequest.requestedAdapterId =
                    negotiation.encoderSelection.selected.adapterId;
            }
        }
        const runtime::display::DisplayCodecEncoderCreateResult encoderCreated =
            runtime::display::createSelectedDisplayEncoder(*codecFactory,
                                                           codecRequest);
        if (displayCodecPlanDiagnosticsRequested(argc, argv))
            writeDisplayCodecPlanDiagnostics(codecRequest,
                                             encoderCreated.selection,
                                             encoderCreated.messages,
                                             "profile_dependencies");
        if (!encoderCreated.ok)
            writeShellMessages(encoderCreated.messages);
        dependencies.encoder = encoderCreated.encoder;
        dependencies.encoderCodec =
            displayCodecRuntimeInfoFromSelection(encoderCreated.selection,
                                                 encoderCreated.messages,
                                                 profile.displayCodecPolicy);
        if (captureRuntime != nullptr) {
            captureRuntime->backendFactory = std::move(captureFactory);
            captureRuntime->selectionRequest = capturePlan.selectionRequest;
        }
#endif
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS) || \
    defined(FUSIONDESK_PC_HAS_MACOS_FEATURE_ADAPTERS) || \
    defined(FUSIONDESK_PC_HAS_LINUX_FEATURE_ADAPTERS) || \
    defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
        if (clipboardProfileRequested(argc, argv))
            dependencies.clipboardEndpoint =
                makeClipboardEndpoint(argc,
                                      argv,
                                      *dependencies.clipboardPolicy,
                                      clipboardRemoteReader,
                                      clipboardDragCoordinateMapper);
#endif
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
        dependencies.inputInjector =
            std::make_shared<platform::windows::input::WindowsInputInjector>();
#endif
        return dependencies;
    }

    if (displayCapturePlanDiagnosticsRequested(argc, argv)) {
        runtime::display::DisplayCapturePlatformPlanRequest capturePlanRequest =
            makeDisplayCapturePlatformPlanRequest(argc, argv);
        capturePlanRequest.role =
            runtime::display::DisplayCaptureRuntimeRole::Client;
        const runtime::display::DisplayCapturePlatformPlan capturePlan =
            runtime::display::planDisplayCapturePlatform(capturePlanRequest);
        writeDisplayCapturePlanDiagnostics(capturePlan,
                                           "profile_dependencies",
                                           displayIncludeCursorOptionValue(
                                               argc, argv));
    }

    std::shared_ptr<runtime::display::IDisplayCodecBackendFactory>
        codecFactory = makeDisplayCodecBackendFactory(
            displayTargetPlatformOptionValue(argc, argv),
            profile.displayCodecPolicy);
    const std::vector<runtime::display::DisplayCodecCapability>
        codecCapabilities = codecFactory->capabilities();
    runtime::display::DisplayCodecSelectionRequest codecRequest =
        makeDisplayCodecSelectionRequest(
            argc,
            argv,
            runtime::display::DisplayCodecDirection::Decode,
            profile.displayCodecPolicy);
    codecRequest.candidates = codecCapabilities;
    if (applyPeerCodecNegotiationToRequest(role,
                                           peerCodecNegotiation,
                                           codecRequest)) {
        if (displayCodecPlanDiagnosticsRequested(argc, argv))
            writeDisplayCodecNegotiationDiagnostics(
                peerCodecNegotiation->negotiation,
                "profile_dependencies_fdpp");
        if (!peerCodecNegotiation->ok)
            writeShellMessages(peerCodecNegotiation->messages);
    } else if (displayCodecLocalNegotiationRequested(argc, argv)) {
        const runtime::display::DisplayCodecNegotiationResult negotiation =
            runtime::display::negotiateDisplayCodec(
                makeDisplayCodecNegotiationRequest(argc,
                                                   argv,
                                                   profile.displayCodecPolicy,
                                                   codecCapabilities));
        if (displayCodecPlanDiagnosticsRequested(argc, argv))
            writeDisplayCodecNegotiationDiagnostics(
                negotiation,
                "profile_dependencies");
        if (!negotiation.ok)
            writeShellMessages(negotiation.messages);
        if (!negotiation.ok) {
            codecRequest.codecPreference = {
                runtime::display::DisplayCodecId::Unknown};
            codecRequest.requestedAdapterId =
                "display.codec.negotiation.failed";
        } else {
            codecRequest.codecPreference = {negotiation.codec};
            codecRequest.requestedAdapterId =
                negotiation.decoderSelection.selected.adapterId;
        }
    }
    const runtime::display::DisplayCodecDecoderCreateResult decoderCreated =
        runtime::display::createSelectedDisplayDecoder(*codecFactory,
                                                       codecRequest);
    if (displayCodecPlanDiagnosticsRequested(argc, argv))
        writeDisplayCodecPlanDiagnostics(codecRequest,
                                         decoderCreated.selection,
                                         decoderCreated.messages,
                                         "profile_dependencies");
    if (!decoderCreated.ok)
        writeShellMessages(decoderCreated.messages);
    dependencies.decoder = decoderCreated.decoder;
    dependencies.decoderCodec =
        displayCodecRuntimeInfoFromSelection(decoderCreated.selection,
                                             decoderCreated.messages,
                                             profile.displayCodecPolicy);
#if defined(FUSIONDESK_PC_HAS_QT_IMAGE_DISPLAY)
    auto renderer = std::make_shared<adapters::qt::display::QtImageDisplayRenderer>();
    modules::display::DisplayRenderSurface surface;
    surface.kind = modules::display::DisplayRenderSurfaceKind::QtImageSink;
    renderer->attachSurface(surface);
    if (captureRuntime != nullptr)
        captureRuntime->imageRenderer = renderer;
    dependencies.renderer = std::move(renderer);
#endif
#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
    dependencies.inputCapture = std::make_shared<adapters::qt::input::QtInputCapture>();
#endif
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS) || \
    defined(FUSIONDESK_PC_HAS_MACOS_FEATURE_ADAPTERS) || \
    defined(FUSIONDESK_PC_HAS_LINUX_FEATURE_ADAPTERS) || \
    defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
    if (clipboardProfileRequested(argc, argv))
        dependencies.clipboardEndpoint =
            makeClipboardEndpoint(argc,
                                  argv,
                                  *dependencies.clipboardPolicy,
                                  clipboardRemoteReader,
                                  clipboardDragCoordinateMapper);
#endif
    return dependencies;
}


} // namespace pc
} // namespace apps
} // namespace fusiondesk
