#include "pc_display_diagnostics.h"

#include <iostream>

#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"
#include "fusiondesk/runtime/display/display_capture_platform_plan.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"
#include "fusiondesk/runtime/display/display_codec_negotiation.h"
#include "fusiondesk/runtime/display/display_source_selection.h"

namespace fusiondesk {
namespace apps {
namespace pc {

const char* boolValue(bool value)
{
    return value ? "1" : "0";
}

void writeDisplayCapturePlanDiagnostics(
    const runtime::display::DisplayCapturePlatformPlan& plan,
    const char* phase,
    bool includeCursor)
{
    const runtime::display::DisplayCaptureBackendCapability* selected =
        plan.selection.hasSelection ? &plan.selection.selected : nullptr;
    const bool agentRole = plan.selectionRequest.requireAgentCapture;

    std::cout << "display.capture.plan"
              << " phase=" << phase
              << " ok=" << boolValue(plan.ok)
              << " captureRequired=" << boolValue(plan.captureRequired)
              << " renderOnly=" << boolValue(plan.renderOnly)
              << " capabilitySource="
              << runtime::display::displayCaptureCapabilitySourceName(
                     plan.capabilitySource)
              << " role=" << (agentRole ? "agent" : "client")
              << " platform="
              << runtime::display::displayPlatformFamilyName(
                     plan.selectionRequest.platform)
              << " source="
              << runtime::display::displayCaptureSourceTypeName(
                     plan.selectionRequest.sourceType)
              << " includeCursor="
              << boolValue(includeCursor)
              << " requested="
              << (plan.selectionRequest.requestedAdapterId.empty()
                      ? "auto"
                      : plan.selectionRequest.requestedAdapterId)
              << " arch="
              << runtime::display::displayTargetArchitectureName(
                     plan.selectionRequest.architecture)
              << " soc="
              << runtime::display::displayTargetSocProfileName(
                     plan.selectionRequest.socProfile)
              << " selected="
              << (selected == nullptr ? std::string() : selected->adapterId)
              << " backend="
              << (selected == nullptr
                      ? "unknown"
                      : runtime::display::displayCaptureBackendKindName(
                            selected->backend))
              << " fallback="
              << boolValue(selected != nullptr && selected->fallback)
              << " score=" << plan.selection.score
              << " candidates=" << plan.selectionRequest.candidates.size()
              << " rejected=" << plan.selection.rejected.size()
              << " messages=" << plan.messages.size()
              << std::endl;

    for (std::size_t index = 0;
         index < plan.selectionRequest.candidates.size();
         ++index) {
        const runtime::display::DisplayCaptureBackendCapability& candidate =
            plan.selectionRequest.candidates[index];
        std::cout << "display.capture.plan.candidate"
                  << " phase=" << phase
                  << " index=" << index
                  << " adapter=" << candidate.adapterId
                  << " backend="
                  << runtime::display::displayCaptureBackendKindName(
                         candidate.backend)
                  << " available=" << boolValue(candidate.available)
                  << " fallback=" << boolValue(candidate.fallback)
                  << " zeroCopy=" << boolValue(candidate.zeroCopy)
                  << " serviceSession="
                  << boolValue(candidate.supportsServiceSession)
                  << " agentCapture="
                  << boolValue(candidate.agentCaptureSupported)
                  << " priority=" << candidate.priority
                  << " reason=" << candidate.unavailableReason
                  << std::endl;
    }

    for (std::size_t index = 0; index < plan.selection.rejected.size();
         ++index) {
        const runtime::display::DisplayCaptureBackendRejection& rejection =
            plan.selection.rejected[index];
        std::cout << "display.capture.plan.rejection"
                  << " phase=" << phase
                  << " index=" << index
                  << " adapter=" << rejection.adapterId
                  << " backend="
                  << runtime::display::displayCaptureBackendKindName(
                         rejection.backend)
                  << " reason=" << rejection.reason
                  << std::endl;
    }

    for (std::size_t index = 0; index < plan.messages.size(); ++index) {
        std::cout << "display.capture.plan.message"
                  << " phase=" << phase
                  << " index=" << index
                  << " text=" << plan.messages[index]
                  << std::endl;
    }
}

std::string displayCodecPreferenceText(
    const std::vector<runtime::display::DisplayCodecId>& codecs)
{
    std::string result;
    for (runtime::display::DisplayCodecId codec : codecs) {
        if (!result.empty())
            result += ",";
        result += runtime::display::displayCodecIdName(codec);
    }
    return result;
}

void writeDisplayCodecPlanDiagnostics(
    const runtime::display::DisplayCodecSelectionRequest& request,
    const runtime::display::DisplayCodecSelectionResult& selection,
    const std::vector<std::string>& messages,
    const char* phase)
{
    const runtime::display::DisplayCodecCapability* selected =
        selection.hasSelection ? &selection.selected : nullptr;
    std::cout << "display.codec.plan"
              << " phase=" << phase
              << " ok=" << boolValue(selection.ok)
              << " direction="
              << runtime::display::displayCodecDirectionName(
                     request.direction)
              << " platform="
              << runtime::display::displayPlatformFamilyName(request.platform)
              << " preference="
              << displayCodecPreferenceText(request.codecPreference)
              << " requested="
              << (request.requestedAdapterId.empty()
                      ? "auto"
                      : request.requestedAdapterId)
              << " arch="
              << runtime::display::displayTargetArchitectureName(
                     request.architecture)
              << " soc="
              << runtime::display::displayTargetSocProfileName(
                     request.socProfile)
              << " selected="
              << (selected == nullptr ? std::string() : selected->adapterId)
              << " backend="
              << (selected == nullptr
                      ? "unknown"
                      : runtime::display::displayCodecBackendKindName(
                            selected->backend))
              << " codec="
              << (selected == nullptr
                      ? "unknown"
                      : runtime::display::displayCodecIdName(selected->codec))
              << " fallback="
              << boolValue(selected != nullptr && selected->fallback)
              << " hardware="
              << boolValue(selected != nullptr &&
                           selected->hardwareAccelerated)
              << " zeroCopy="
              << boolValue(selected != nullptr && selected->zeroCopy)
              << " score=" << selection.score
              << " candidates=" << request.candidates.size()
              << " rejected=" << selection.rejected.size()
              << " messages=" << messages.size()
              << std::endl;

    for (std::size_t index = 0; index < request.candidates.size();
         ++index) {
        const runtime::display::DisplayCodecCapability& candidate =
            request.candidates[index];
        std::cout << "display.codec.plan.candidate"
                  << " phase=" << phase
                  << " index=" << index
                  << " adapter=" << candidate.adapterId
                  << " backend="
                  << runtime::display::displayCodecBackendKindName(
                         candidate.backend)
                  << " codec="
                  << runtime::display::displayCodecIdName(candidate.codec)
                  << " encode=" << boolValue(candidate.supportsEncode)
                  << " decode=" << boolValue(candidate.supportsDecode)
                  << " available=" << boolValue(candidate.available)
                  << " fallback=" << boolValue(candidate.fallback)
                  << " hardware="
                  << boolValue(candidate.hardwareAccelerated)
                  << " zeroCopy=" << boolValue(candidate.zeroCopy)
                  << " priority=" << candidate.priority
                  << " reason=" << candidate.unavailableReason
                  << std::endl;
    }

    for (std::size_t index = 0; index < selection.rejected.size(); ++index) {
        const runtime::display::DisplayCodecRejection& rejection =
            selection.rejected[index];
        std::cout << "display.codec.plan.rejection"
                  << " phase=" << phase
                  << " index=" << index
                  << " adapter=" << rejection.adapterId
                  << " backend="
                  << runtime::display::displayCodecBackendKindName(
                         rejection.backend)
                  << " codec="
                  << runtime::display::displayCodecIdName(rejection.codec)
                  << " reason=" << rejection.reason
                  << std::endl;
    }

    for (std::size_t index = 0; index < messages.size(); ++index) {
        std::cout << "display.codec.plan.message"
                  << " phase=" << phase
                  << " index=" << index
                  << " text=" << messages[index]
                  << std::endl;
    }
}

void writeDisplayCodecNegotiationDiagnostics(
    const runtime::display::DisplayCodecNegotiationResult& negotiation,
    const char* phase)
{
    const runtime::display::DisplayCodecCapability* encoderSelected =
        negotiation.encoderSelection.hasSelection
            ? &negotiation.encoderSelection.selected
            : nullptr;
    const runtime::display::DisplayCodecCapability* decoderSelected =
        negotiation.decoderSelection.hasSelection
            ? &negotiation.decoderSelection.selected
            : nullptr;

    std::cout << "display.codec.negotiation"
              << " phase=" << phase
              << " ok=" << boolValue(negotiation.ok)
              << " codec="
              << runtime::display::displayCodecIdName(negotiation.codec)
              << " fallback=" << boolValue(negotiation.fallbackSelected)
              << " selectedEncoder="
              << (encoderSelected == nullptr ? std::string()
                                             : encoderSelected->adapterId)
              << " selectedDecoder="
              << (decoderSelected == nullptr ? std::string()
                                             : decoderSelected->adapterId)
              << " attempts=" << negotiation.attempts.size()
              << " messages=" << negotiation.messages.size()
              << std::endl;

    for (std::size_t index = 0; index < negotiation.attempts.size(); ++index) {
        const runtime::display::DisplayCodecNegotiationAttempt& attempt =
            negotiation.attempts[index];
        const runtime::display::DisplayCodecCapability* attemptEncoder =
            attempt.encoderSelection.hasSelection
                ? &attempt.encoderSelection.selected
                : nullptr;
        const runtime::display::DisplayCodecCapability* attemptDecoder =
            attempt.decoderSelection.hasSelection
                ? &attempt.decoderSelection.selected
                : nullptr;
        std::cout << "display.codec.negotiation.attempt"
                  << " phase=" << phase
                  << " index=" << index
                  << " codec="
                  << runtime::display::displayCodecIdName(attempt.codec)
                  << " encoderOk=" << boolValue(attempt.encoderSelection.ok)
                  << " decoderOk=" << boolValue(attempt.decoderSelection.ok)
                  << " encoder="
                  << (attemptEncoder == nullptr ? std::string()
                                                : attemptEncoder->adapterId)
                  << " decoder="
                  << (attemptDecoder == nullptr ? std::string()
                                                : attemptDecoder->adapterId)
                  << std::endl;
    }

    for (std::size_t index = 0; index < negotiation.messages.size(); ++index) {
        std::cout << "display.codec.negotiation.message"
                  << " phase=" << phase
                  << " index=" << index
                  << " text=" << negotiation.messages[index]
                  << std::endl;
    }
}

void writeDisplaySourceCatalogDiagnostics(
    const runtime::display::DisplayCapturePlatformPlan& plan,
    const char* phase,
    const runtime::display::IDisplayCaptureBackendFactory& factory,
    const modules::display::DisplayCaptureOpenOptions& options)
{
    const runtime::display::DisplayCaptureBackendCapability* selected =
        nullptr;
    const runtime::display::DisplayCaptureSourceCatalogResult catalogResult =
        runtime::display::queryDisplayCaptureSourceCatalog(
            factory,
            plan.selectionRequest);
    if (catalogResult.selection.hasSelection)
        selected = &catalogResult.selection.selected;
    const runtime::display::DisplayCaptureBackendKind backend =
        selected == nullptr ? runtime::display::DisplayCaptureBackendKind::Unknown
                            : selected->backend;
    const modules::display::DisplayTopologySnapshot& topology =
        catalogResult.topology;
    runtime::display::DisplaySourceSelectionRequest sourceRequest;
    sourceRequest.sourceType = options.sourceType;
    sourceRequest.sourceId = options.sourceId;
    sourceRequest.nativeSourceHandle = options.nativeSourceHandle;
    const runtime::display::DisplaySourceSelectionResult sourceSelection =
        runtime::display::selectDisplaySource(topology, sourceRequest);

    std::cout << "display.source.catalog"
              << " phase=" << phase
              << " ok=" << boolValue(catalogResult.ok)
              << " source="
              << runtime::display::displayCaptureSourceTypeName(
                     plan.selectionRequest.sourceType)
              << " requested="
              << (plan.selectionRequest.requestedAdapterId.empty()
                      ? "auto"
                      : plan.selectionRequest.requestedAdapterId)
              << " selected="
              << (selected == nullptr ? std::string() : selected->adapterId)
              << " backend="
              << runtime::display::displayCaptureBackendKindName(backend)
              << " provider=" << boolValue(catalogResult.hasCatalog)
              << " generation=" << topology.generation
              << " sources=" << topology.sources.size()
              << " requestedSourceId=" << sourceRequest.sourceId
              << " requestedNativeHandle=" << sourceRequest.nativeSourceHandle
              << " sourceMatched=" << boolValue(sourceSelection.ok)
              << " sourceCandidates=" << sourceSelection.candidateCount
              << " rejected=" << catalogResult.selection.rejected.size()
              << " messages=" << catalogResult.messages.size()
              << std::endl;

    std::cout << "display.source.selection"
              << " phase=" << phase
              << " ok=" << boolValue(sourceSelection.ok)
              << " type="
              << runtime::display::displayCaptureSourceTypeName(
                     sourceRequest.sourceType)
              << " requestedId=" << sourceRequest.sourceId
              << " requestedNativeHandle=" << sourceRequest.nativeSourceHandle
              << " selectedIndex=" << sourceSelection.sourceIndex
              << " selectedId="
              << (sourceSelection.hasSource
                      ? sourceSelection.source.sourceId
                      : 0)
              << " selectedNativeHandle="
              << (sourceSelection.hasSource
                      ? sourceSelection.source.nativeSourceHandle
                      : 0)
              << " candidates=" << sourceSelection.candidateCount
              << " messages=" << sourceSelection.messages.size()
              << std::endl;

    for (std::size_t index = 0;
         index < catalogResult.selection.rejected.size();
         ++index) {
        const runtime::display::DisplayCaptureBackendRejection& rejection =
            catalogResult.selection.rejected[index];
        std::cout << "display.source.catalog.rejection"
                  << " phase=" << phase
                  << " index=" << index
                  << " adapter=" << rejection.adapterId
                  << " backend="
                  << runtime::display::displayCaptureBackendKindName(
                         rejection.backend)
                  << " reason=" << rejection.reason
                  << std::endl;
    }

    for (std::size_t index = 0; index < catalogResult.messages.size();
         ++index) {
        std::cout << "display.source.catalog.message"
                  << " phase=" << phase
                  << " index=" << index
                  << " text=" << catalogResult.messages[index]
                  << std::endl;
    }

    for (std::size_t index = 0; index < sourceSelection.messages.size();
         ++index) {
        std::cout << "display.source.selection.message"
                  << " phase=" << phase
                  << " index=" << index
                  << " text=" << sourceSelection.messages[index]
                  << std::endl;
    }

    for (std::size_t index = 0; index < topology.sources.size(); ++index) {
        const modules::display::DisplaySourceInfo& source =
            topology.sources[index];
        const bool sourceSelected =
            sourceSelection.ok &&
            runtime::display::displaySourceMatchesSelection(
                source,
                sourceSelection.source);
        std::cout << "display.source"
                  << " phase=" << phase
                  << " index=" << index
                  << " id=" << source.sourceId
                  << " selected=" << boolValue(sourceSelected)
                  << " type="
                  << runtime::display::displayCaptureSourceTypeName(
                         source.sourceType)
                  << " primary=" << boolValue(source.primary)
                  << " x=" << source.geometry.x
                  << " y=" << source.geometry.y
                  << " width=" << source.geometry.width
                  << " height=" << source.geometry.height
                  << " dpiX=" << source.dpiX
                  << " dpiY=" << source.dpiY
                  << " nativeHandle=" << source.nativeSourceHandle
                  << " name=" << source.name
                  << std::endl;
    }
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
