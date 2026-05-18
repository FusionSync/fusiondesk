#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_NEGOTIATION_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_NEGOTIATION_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/display/display_codec_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

struct DisplayCodecNegotiationRequest
{
    DisplayCodecSelectionRequest encoderRequest;
    DisplayCodecSelectionRequest decoderRequest;
};

struct DisplayCodecNegotiationAttempt
{
    DisplayCodecId codec = DisplayCodecId::Unknown;
    DisplayCodecSelectionResult encoderSelection;
    DisplayCodecSelectionResult decoderSelection;
};

struct DisplayCodecNegotiationResult
{
    bool ok = false;
    bool hasSelection = false;
    DisplayCodecId codec = DisplayCodecId::Unknown;
    DisplayCodecSelectionResult encoderSelection;
    DisplayCodecSelectionResult decoderSelection;
    bool fallbackSelected = false;
    std::vector<DisplayCodecNegotiationAttempt> attempts;
    std::vector<std::string> messages;
};

DisplayCodecNegotiationResult negotiateDisplayCodec(
    const DisplayCodecNegotiationRequest& request);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_NEGOTIATION_H
