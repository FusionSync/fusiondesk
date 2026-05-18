#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_ENCODED_VIDEO_PAYLOAD_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_ENCODED_VIDEO_PAYLOAD_H

#include <string>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace modules {
namespace display {

struct DisplayEncodedVideoPayload
{
    DisplayEncodedVideoCodec codec = DisplayEncodedVideoCodec::Unknown;
    DisplayEncodedVideoBitstreamFormat bitstreamFormat =
        DisplayEncodedVideoBitstreamFormat::Unknown;
    EncodedFrame frame;
    std::uint32_t codedWidth = 0;
    std::uint32_t codedHeight = 0;
    std::uint32_t visibleWidth = 0;
    std::uint32_t visibleHeight = 0;
    protocol::ByteBuffer sequenceHeader;
    protocol::ByteBuffer bitstream;
};

struct DisplayEncodedVideoPayloadDecodeResult
{
    bool ok = false;
    DisplayEncodedVideoPayload payload;
    std::string error;
};

const char* displayEncodedVideoCodecName(DisplayEncodedVideoCodec codec);
const char* displayEncodedVideoBitstreamFormatName(
    DisplayEncodedVideoBitstreamFormat format);

protocol::ByteBuffer encodeDisplayEncodedVideoPayload(
    const DisplayEncodedVideoPayload& payload);

DisplayEncodedVideoPayloadDecodeResult decodeDisplayEncodedVideoPayload(
    const protocol::ByteBuffer& payload);

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_ENCODED_VIDEO_PAYLOAD_H
