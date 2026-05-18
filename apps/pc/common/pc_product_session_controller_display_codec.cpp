#include "pc_product_session_controller.h"

#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"
#include "fusiondesk/runtime/display/display_codec_peer_profile.h"

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

void appendMessages(std::vector<std::string>& target,
                    const std::vector<std::string>& messages)
{
    target.insert(target.end(), messages.begin(), messages.end());
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
    extension.payload =
        runtime::display::encodeDisplayCodecPeerProfilePayload(payload);
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

runtime::ProductDisplayCodecPolicy normalizedDisplayCodecPolicy(
    runtime::ProductDisplayCodecPolicy policy)
{
    if (policy.codecPreference.empty())
        policy.codecPreference = {runtime::display::DisplayCodecId::RawBgra};
    if (policy.selectionMode.empty())
        policy.selectionMode = "default";
    return policy;
}

runtime::ProductDisplayCodecPolicy controllerDisplayCodecPolicy(
    const runtime::RuntimeHost& host,
    bool useControllerProductPolicy,
    runtime::ProductDisplayCodecPolicy fallback)
{
    return normalizedDisplayCodecPolicy(
        useControllerProductPolicy
            ? host.profile().displayCodecPolicy
            : std::move(fallback));
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
        normalizedDisplayCodecPolicy(policy);
    info.selectionMode = effectivePolicy.selectionMode;
    if (!selection.hasSelection) {
        info.messages = messages;
        return info;
    }

    const runtime::display::DisplayCodecCapability& selected =
        selection.selected;
    info.selected = true;
    info.adapterId = selected.adapterId;
    info.codec = runtime::display::displayCodecIdName(selected.codec);
    info.backend =
        runtime::display::displayCodecBackendKindName(selected.backend);
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

} // namespace

PcProductDisplayCodecPeerProfileResult
PcProductSessionController::appendDisplayCodecPeerProfileDecoderRequest(
    runtime::connection::PeerProfileExchangeRequest& request,
    runtime::display::DisplayCodecSelectionRequest decoderRequest) const
{
    PcProductDisplayCodecPeerProfileResult result;
    result.attempted = true;
    result.decoderRequest = decoderRequest;

    runtime::display::DisplayCodecPeerProfilePayload payload;
    payload.hasDecoderRequest = true;
    payload.decoderRequest = std::move(decoderRequest);

    runtime::connection::PeerProfileExtension extension =
        makeDisplayCodecPeerExtension(payload);
    if (extension.payload.empty()) {
        result.code = 14;
        result.messages.push_back(
            "display codec FDPP decoder request payload encode failed");
        return result;
    }

    request.extensions.push_back(std::move(extension));
    result.ok = true;
    result.extensionAdded = true;
    return result;
}

PcProductDisplayCodecPeerProfileResult
PcProductSessionController::handleDisplayCodecPeerProfileRequest(
    const runtime::connection::PeerProfileExchangeRequest& request,
    runtime::connection::PeerProfileExchangeResult& exchange,
    runtime::display::DisplayCodecSelectionRequest encoderRequest) const
{
    PcProductDisplayCodecPeerProfileResult result;
    if (!exchange.ok) {
        result.code = 14;
        result.messages = exchange.messages;
        if (result.messages.empty()) {
            result.messages.push_back(
                "peer profile exchange failed before display codec negotiation");
        }
        return result;
    }

    result.attempted = true;
    const runtime::connection::PeerProfileExtension* extension =
        findDisplayCodecPeerExtension(request.extensions);
    if (extension == nullptr) {
        result.code = 14;
        result.messages.push_back(
            "display codec FDPP decoder request extension is missing");
        exchange.ok = false;
        appendMessages(exchange.messages, result.messages);
        return result;
    }

    const runtime::display::DisplayCodecPeerProfileDecodeResult decoded =
        runtime::display::decodeDisplayCodecPeerProfilePayload(
            extension->payload);
    if (!decoded.ok || !decoded.payload.hasDecoderRequest) {
        result.code = 14;
        result.messages.push_back(
            decoded.message.empty()
                ? "display codec FDPP decoder request is missing"
                : decoded.message);
        exchange.ok = false;
        appendMessages(exchange.messages, result.messages);
        return result;
    }

    runtime::display::DisplayCodecNegotiationRequest negotiationRequest;
    negotiationRequest.encoderRequest = std::move(encoderRequest);
    negotiationRequest.decoderRequest = decoded.payload.decoderRequest;
    result.negotiation =
        runtime::display::negotiateDisplayCodec(negotiationRequest);
    if (!result.negotiation.ok) {
        result.code = 14;
        result.messages = result.negotiation.messages;
        if (result.messages.empty()) {
            result.messages.push_back(
                "display codec FDPP negotiation failed");
        }
        exchange.ok = false;
        appendMessages(exchange.messages, result.messages);
        return result;
    }

    result.encoderRequest =
        pinnedDisplayCodecRequest(negotiationRequest.encoderRequest,
                                  result.negotiation.encoderSelection);
    result.decoderRequest =
        pinnedDisplayCodecRequest(negotiationRequest.decoderRequest,
                                  result.negotiation.decoderSelection);

    runtime::display::DisplayCodecPeerProfilePayload responsePayload;
    responsePayload.hasEncoderRequest = true;
    responsePayload.encoderRequest = result.encoderRequest;
    responsePayload.hasDecoderRequest = true;
    responsePayload.decoderRequest = result.decoderRequest;

    runtime::connection::PeerProfileExtension responseExtension =
        makeDisplayCodecPeerExtension(responsePayload);
    if (responseExtension.payload.empty()) {
        result.code = 14;
        result.messages.push_back(
            "display codec FDPP response payload encode failed");
        exchange.ok = false;
        appendMessages(exchange.messages, result.messages);
        return result;
    }

    exchange.extensions.push_back(std::move(responseExtension));
    result.ok = true;
    result.extensionAdded = true;
    return result;
}

PcProductDisplayCodecPeerProfileResult
PcProductSessionController::readDisplayCodecPeerProfileCompletion(
    const runtime::qt::QtPeerProfileRuntimeServiceSnapshot& snapshot) const
{
    PcProductDisplayCodecPeerProfileResult result;
    for (auto it = snapshot.runtime.completions.rbegin();
         it != snapshot.runtime.completions.rend();
         ++it) {
        if (!it->ok)
            continue;

        result.attempted = true;
        const runtime::connection::PeerProfileExtension* extension =
            findDisplayCodecPeerExtension(it->exchange.extensions);
        if (extension == nullptr) {
            result.code = 14;
            result.messages.push_back(
                "display codec FDPP response extension is missing");
            return result;
        }

        const runtime::display::DisplayCodecPeerProfileDecodeResult decoded =
            runtime::display::decodeDisplayCodecPeerProfilePayload(
                extension->payload);
        if (!decoded.ok ||
            !decoded.payload.hasEncoderRequest ||
            !decoded.payload.hasDecoderRequest) {
            result.code = 14;
            result.messages.push_back(
                decoded.message.empty()
                    ? "display codec FDPP response is incomplete"
                    : decoded.message);
            return result;
        }

        runtime::display::DisplayCodecNegotiationRequest negotiationRequest;
        negotiationRequest.encoderRequest = decoded.payload.encoderRequest;
        negotiationRequest.decoderRequest = decoded.payload.decoderRequest;
        result.negotiation =
            runtime::display::negotiateDisplayCodec(negotiationRequest);
        result.encoderRequest = decoded.payload.encoderRequest;
        result.decoderRequest = decoded.payload.decoderRequest;
        if (!result.negotiation.ok) {
            result.code = 14;
            result.messages = result.negotiation.messages;
            if (result.messages.empty()) {
                result.messages.push_back(
                    "display codec FDPP response negotiation failed");
            }
            return result;
        }

        result.ok = true;
        result.extensionAdded = true;
        return result;
    }

    result.code = 14;
    result.messages.push_back("display codec FDPP completion is missing");
    return result;
}

PcProductDisplayCodecInventoryResult
PcProductSessionController::buildDisplayCodecInventoryRequest(
    runtime::display::DisplayCodecDirection direction,
    const runtime::display::IDisplayCodecBackendFactory& factory,
    const PcProductDisplayCodecInventoryOptions& options) const
{
    PcProductDisplayCodecInventoryResult result;
    const runtime::ProductDisplayCodecPolicy policy =
        controllerDisplayCodecPolicy(host(),
                                     options.useControllerProductPolicy,
                                     options.policy);

    result.request.platform = options.platform;
    result.request.direction = direction;
    result.request.codecPreference =
        options.codecPreferenceOverride.empty()
            ? policy.codecPreference
            : options.codecPreferenceOverride;
    result.request.acceptedInputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            options.platform);
    result.request.acceptedOutputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            options.platform);
    result.request.acceptedPixelFormats = {
        modules::display::DisplayPixelFormat::Bgra32};
    result.request.architecture = options.architecture;
    result.request.socProfile = options.socProfile;
    result.request.requestedAdapterId = options.requestedAdapterId;
    result.request.allowHardware = policy.allowHardware;
    result.request.allowSoftware = policy.allowSoftware;
    result.request.preferHardware = policy.preferHardware;
    result.request.preferZeroCopy = policy.preferZeroCopy;
    result.request.requireLowLatency = options.requireLowLatency;
    result.request.width = options.width;
    result.request.height = options.height;
    result.request.candidates = factory.capabilities();

    if (result.request.candidates.empty()) {
        result.code = 15;
        result.messages.push_back(
            "display codec inventory requires at least one candidate");
        return result;
    }

    result.ok = true;
    return result;
}

PcProductDisplayCodecInventoryResult
PcProductSessionController::pinDisplayCodecInventoryRequest(
    PcShellRole role,
    runtime::display::DisplayCodecSelectionRequest request,
    const runtime::display::DisplayCodecNegotiationResult& negotiation) const
{
    PcProductDisplayCodecInventoryResult result;
    if (!negotiation.ok) {
        result.code = 15;
        result.messages = negotiation.messages;
        if (result.messages.empty())
            result.messages.push_back("display codec negotiation failed");
        result.request = pinnedDisplayCodecRequest(
            std::move(request),
            runtime::display::DisplayCodecSelectionResult{});
        return result;
    }

    const runtime::display::DisplayCodecSelectionResult& selection =
        role == PcShellRole::Agent
            ? negotiation.encoderSelection
            : negotiation.decoderSelection;
    if (!selection.hasSelection) {
        result.code = 15;
        result.messages.push_back(
            "display codec negotiation did not return a selected adapter");
        result.request = pinnedDisplayCodecRequest(
            std::move(request),
            selection);
        return result;
    }

    result.request = pinnedDisplayCodecRequest(std::move(request), selection);
    result.ok = true;
    return result;
}

PcProductDisplayCodecCreateResult
PcProductSessionController::createDisplayEncoder(
    const runtime::display::IDisplayCodecBackendFactory& factory,
    runtime::display::DisplayCodecSelectionRequest request,
    const PcProductDisplayCodecCreateOptions& options) const
{
    const runtime::ProductDisplayCodecPolicy policy =
        controllerDisplayCodecPolicy(host(),
                                     options.useControllerProductPolicy,
                                     options.policy);
    PcProductDisplayCodecCreateResult result;
    result.request = request;
    const runtime::display::DisplayCodecEncoderCreateResult created =
        runtime::display::createSelectedDisplayEncoder(factory, request);
    result.ok = created.ok;
    result.code = created.ok ? 0 : 16;
    result.selection = created.selection;
    result.encoder = created.encoder;
    result.messages = created.messages;
    result.runtimeInfo =
        displayCodecRuntimeInfoFromSelection(created.selection,
                                             created.messages,
                                             policy);
    return result;
}

PcProductDisplayCodecCreateResult
PcProductSessionController::createDisplayDecoder(
    const runtime::display::IDisplayCodecBackendFactory& factory,
    runtime::display::DisplayCodecSelectionRequest request,
    const PcProductDisplayCodecCreateOptions& options) const
{
    const runtime::ProductDisplayCodecPolicy policy =
        controllerDisplayCodecPolicy(host(),
                                     options.useControllerProductPolicy,
                                     options.policy);
    PcProductDisplayCodecCreateResult result;
    result.request = request;
    const runtime::display::DisplayCodecDecoderCreateResult created =
        runtime::display::createSelectedDisplayDecoder(factory, request);
    result.ok = created.ok;
    result.code = created.ok ? 0 : 16;
    result.selection = created.selection;
    result.decoder = created.decoder;
    result.messages = created.messages;
    result.runtimeInfo =
        displayCodecRuntimeInfoFromSelection(created.selection,
                                             created.messages,
                                             policy);
    return result;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
