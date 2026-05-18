#include "fusiondesk/runtime/display/display_codec_negotiation.h"

#include <algorithm>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

bool containsCodec(const std::vector<DisplayCodecId>& codecs,
                   DisplayCodecId codec)
{
    return std::find(codecs.begin(), codecs.end(), codec) != codecs.end();
}

std::vector<DisplayCodecId> commonCodecPreference(
    const DisplayCodecSelectionRequest& encoderRequest,
    const DisplayCodecSelectionRequest& decoderRequest)
{
    const std::vector<DisplayCodecId>& primary =
        encoderRequest.codecPreference.empty()
            ? decoderRequest.codecPreference
            : encoderRequest.codecPreference;
    const std::vector<DisplayCodecId>& secondary =
        encoderRequest.codecPreference.empty()
            ? encoderRequest.codecPreference
            : decoderRequest.codecPreference;

    std::vector<DisplayCodecId> result;
    for (DisplayCodecId codec : primary) {
        if (codec == DisplayCodecId::Unknown)
            continue;
        if (!secondary.empty() && !containsCodec(secondary, codec))
            continue;
        if (!containsCodec(result, codec))
            result.push_back(codec);
    }
    return result;
}

void appendSelectionMessages(std::vector<std::string>& target,
                             const char* direction,
                             const DisplayCodecSelectionResult& selection)
{
    for (const std::string& message : selection.messages) {
        target.push_back(std::string(direction) + ": " + message);
    }
    for (const DisplayCodecRejection& rejection : selection.rejected) {
        std::string message = std::string(direction) + ": rejected ";
        message += rejection.adapterId.empty() ? "<empty>" : rejection.adapterId;
        message += " for ";
        message += displayCodecIdName(rejection.codec);
        message += ": ";
        message += rejection.reason;
        target.push_back(std::move(message));
    }
}

} // namespace

DisplayCodecNegotiationResult negotiateDisplayCodec(
    const DisplayCodecNegotiationRequest& request)
{
    DisplayCodecNegotiationResult result;

    if (request.encoderRequest.direction != DisplayCodecDirection::Unknown &&
        request.encoderRequest.direction != DisplayCodecDirection::Encode) {
        result.messages.push_back(
            "display codec negotiation encoder request must use encode direction");
        return result;
    }
    if (request.decoderRequest.direction != DisplayCodecDirection::Unknown &&
        request.decoderRequest.direction != DisplayCodecDirection::Decode) {
        result.messages.push_back(
            "display codec negotiation decoder request must use decode direction");
        return result;
    }

    DisplayCodecSelectionRequest encoderRequest = request.encoderRequest;
    DisplayCodecSelectionRequest decoderRequest = request.decoderRequest;
    encoderRequest.direction = DisplayCodecDirection::Encode;
    decoderRequest.direction = DisplayCodecDirection::Decode;

    const std::vector<DisplayCodecId> preference =
        commonCodecPreference(encoderRequest, decoderRequest);
    if (preference.empty()) {
        result.messages.push_back(
            "display codec negotiation found no common codec preference");
        return result;
    }

    for (DisplayCodecId codec : preference) {
        DisplayCodecSelectionRequest encoderAttempt = encoderRequest;
        DisplayCodecSelectionRequest decoderAttempt = decoderRequest;
        encoderAttempt.codecPreference = {codec};
        decoderAttempt.codecPreference = {codec};

        DisplayCodecNegotiationAttempt attempt;
        attempt.codec = codec;
        attempt.encoderSelection = selectDisplayCodec(encoderAttempt);
        attempt.decoderSelection = selectDisplayCodec(decoderAttempt);

        if (attempt.encoderSelection.ok &&
            attempt.decoderSelection.ok &&
            attempt.encoderSelection.selected.codec == codec &&
            attempt.decoderSelection.selected.codec == codec) {
            result.ok = true;
            result.hasSelection = true;
            result.codec = codec;
            result.encoderSelection = attempt.encoderSelection;
            result.decoderSelection = attempt.decoderSelection;
            result.fallbackSelected =
                attempt.encoderSelection.fallbackSelected ||
                attempt.decoderSelection.fallbackSelected;
            result.attempts.push_back(std::move(attempt));
            return result;
        }

        appendSelectionMessages(result.messages,
                                "encoder",
                                attempt.encoderSelection);
        appendSelectionMessages(result.messages,
                                "decoder",
                                attempt.decoderSelection);
        result.attempts.push_back(std::move(attempt));
    }

    result.messages.push_back(
        "display codec negotiation found no mutually selectable codec");
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
