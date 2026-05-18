#include <cassert>

#include "fusiondesk/modules/display/display_control_codec.h"
#include "fusiondesk/modules/display/display_frame_codec.h"

using namespace fusiondesk;

namespace {

modules::display::CapturedFrame makeFrame()
{
    modules::display::CapturedFrame frame;
    frame.frameId = 42;
    frame.keyFrame = true;
    frame.width = 2;
    frame.height = 2;
    frame.strideBytes = 8;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.monotonicTimestampUsec = 123456;
    frame.pixels = {
        0, 0, 255, 255,
        0, 255, 0, 255,
        255, 0, 0, 255,
        255, 255, 255, 255};
    return frame;
}

void rawPayloadRoundtripsFrameMetadataAndPixels()
{
    const modules::display::CapturedFrame frame = makeFrame();
    const protocol::ByteBuffer payload = modules::display::encodeRawFramePayload(frame);
    assert(!payload.empty());

    const modules::display::RawFrameDecodeResult decoded =
        modules::display::decodeRawFramePayload(payload);
    assert(decoded.ok);
    assert(decoded.frame.frameId == frame.frameId);
    assert(decoded.frame.keyFrame == frame.keyFrame);
    assert(decoded.frame.width == frame.width);
    assert(decoded.frame.height == frame.height);
    assert(decoded.frame.strideBytes == frame.strideBytes);
    assert(decoded.frame.pixelFormat == frame.pixelFormat);
    assert(decoded.frame.monotonicTimestampUsec == frame.monotonicTimestampUsec);
    assert(decoded.frame.pixels == frame.pixels);
}

void rawEncoderAndDecoderUseTheSameSchema()
{
    modules::display::RawFrameEncoder encoder;
    modules::display::RawFrameDecoder decoder;
    const modules::display::CapturedFrame frame = makeFrame();

    const modules::display::EncodedFrame encoded = encoder.encode(frame);
    assert(encoded.frameId == frame.frameId);
    assert(encoded.keyFrame == frame.keyFrame);
    assert(encoded.width == frame.width);
    assert(encoded.height == frame.height);
    assert(encoded.strideBytes == frame.strideBytes);
    assert(encoded.pixelFormat == frame.pixelFormat);
    assert(!encoded.payload.empty());

    const modules::display::DecodedFrame decoded = decoder.decode(encoded);
    assert(decoded.frameId == frame.frameId);
    assert(decoded.keyFrame == frame.keyFrame);
    assert(decoded.width == frame.width);
    assert(decoded.height == frame.height);
    assert(decoded.strideBytes == frame.strideBytes);
    assert(decoded.pixelFormat == frame.pixelFormat);
    assert(decoded.pixels == frame.pixels);
}

void invalidPayloadsAreRejected()
{
    const modules::display::RawFrameDecodeResult tooSmall =
        modules::display::decodeRawFramePayload({1, 2, 3});
    assert(!tooSmall.ok);

    modules::display::CapturedFrame invalid = makeFrame();
    invalid.strideBytes = 1;
    assert(modules::display::encodeRawFramePayload(invalid).empty());
}

void displayControlPayloadRoundtripsAndRejectsMalformedInput()
{
    modules::display::DisplayControlPayload payload;
    payload.operation = modules::display::DisplayControlOperation::RequestKeyframe;
    payload.reason = modules::display::DisplayKeyframeReason::DecoderReset;
    payload.frameId = 99;

    const protocol::ByteBuffer encoded = modules::display::encodeDisplayControlPayload(payload);
    const modules::display::DisplayControlDecodeResult decoded =
        modules::display::decodeDisplayControlPayload(encoded);
    assert(decoded.ok);
    assert(decoded.payload.operation == payload.operation);
    assert(decoded.payload.reason == payload.reason);
    assert(decoded.payload.frameId == payload.frameId);

    const modules::display::DisplayControlDecodeResult malformed =
        modules::display::decodeDisplayControlPayload({'k', 'e', 'y', 'f', 'r', 'a', 'm', 'e'});
    assert(!malformed.ok);
}

} // namespace

int main()
{
    rawPayloadRoundtripsFrameMetadataAndPixels();
    rawEncoderAndDecoderUseTheSameSchema();
    invalidPayloadsAreRejected();
    displayControlPayloadRoundtripsAndRejectsMalformedInput();
    return 0;
}
