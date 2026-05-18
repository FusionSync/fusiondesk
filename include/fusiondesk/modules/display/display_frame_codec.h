#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_FRAME_CODEC_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_FRAME_CODEC_H

#include <string>

#include "fusiondesk/modules/display/display_interfaces.h"

namespace fusiondesk {
namespace modules {
namespace display {

struct RawFrameDecodeResult
{
    bool ok = false;
    std::string error;
    DecodedFrame frame;
};

protocol::ByteBuffer encodeRawFramePayload(const CapturedFrame& frame);
RawFrameDecodeResult decodeRawFramePayload(const protocol::ByteBuffer& payload);

class RawFrameEncoder final : public IVideoEncoder
{
public:
    EncodedFrame encode(const CapturedFrame& frame) override;
    DisplayCodecRuntimeInfo codecRuntimeInfo() const override;
};

class RawFrameDecoder final : public IVideoDecoder
{
public:
    DecodedFrame decode(const EncodedFrame& frame) override;
    DisplayCodecRuntimeInfo codecRuntimeInfo() const override;
};

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_FRAME_CODEC_H
