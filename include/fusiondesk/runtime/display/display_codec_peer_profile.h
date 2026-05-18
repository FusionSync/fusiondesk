#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_PEER_PROFILE_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_PEER_PROFILE_H

#include <string>

#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/runtime/display/display_codec_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

struct DisplayCodecPeerProfilePayload
{
    bool hasEncoderRequest = false;
    DisplayCodecSelectionRequest encoderRequest;
    bool hasDecoderRequest = false;
    DisplayCodecSelectionRequest decoderRequest;
};

struct DisplayCodecPeerProfileDecodeResult
{
    bool ok = false;
    DisplayCodecPeerProfilePayload payload;
    std::string message;
};

const char* displayCodecPeerProfileExtensionKey();

protocol::ByteBuffer encodeDisplayCodecPeerProfilePayload(
    const DisplayCodecPeerProfilePayload& payload);

DisplayCodecPeerProfileDecodeResult decodeDisplayCodecPeerProfilePayload(
    const protocol::ByteBuffer& payload);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_PEER_PROFILE_H
