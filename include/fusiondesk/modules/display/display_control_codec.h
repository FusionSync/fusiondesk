#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_CONTROL_CODEC_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_CONTROL_CODEC_H

#include <cstdint>
#include <string>

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace modules {
namespace display {

enum class DisplayControlOperation : std::uint16_t
{
    Unknown = 0,
    RequestKeyframe = 1,
    KeyframeScheduled = 2
};

enum class DisplayKeyframeReason : std::uint16_t
{
    Unknown = 0,
    FirstFrameTimeout = 1,
    DecoderReset = 2,
    FrameGap = 3,
    Reconnect = 4,
    Manual = 5
};

struct DisplayControlPayload
{
    DisplayControlOperation operation = DisplayControlOperation::Unknown;
    DisplayKeyframeReason reason = DisplayKeyframeReason::Unknown;
    std::uint64_t frameId = 0;
};

struct DisplayControlDecodeResult
{
    bool ok = false;
    DisplayControlPayload payload;
    std::string error;
};

protocol::ByteBuffer encodeDisplayControlPayload(const DisplayControlPayload& payload);
DisplayControlDecodeResult decodeDisplayControlPayload(const protocol::ByteBuffer& payload);

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_CONTROL_CODEC_H
