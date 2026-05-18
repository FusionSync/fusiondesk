#include <cassert>
#include <string>

#include "fusiondesk/modules/display/display_encoded_video_payload.h"
#include "fusiondesk/modules/display/display_frame_codec.h"

using namespace fusiondesk;

namespace {

modules::display::DisplayEncodedVideoPayload samplePayload()
{
    modules::display::DisplayEncodedVideoPayload payload;
    payload.codec = modules::display::DisplayEncodedVideoCodec::H264;
    payload.bitstreamFormat =
        modules::display::DisplayEncodedVideoBitstreamFormat::AnnexB;
    payload.frame.frameId = 42;
    payload.frame.keyFrame = true;
    payload.frame.width = 640;
    payload.frame.height = 360;
    payload.frame.strideBytes = 640 * 4;
    payload.frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    payload.frame.monotonicTimestampUsec = 1234567;
    payload.codedWidth = 640;
    payload.codedHeight = 368;
    payload.visibleWidth = 640;
    payload.visibleHeight = 360;
    payload.sequenceHeader = {0, 0, 0, 1, 0x67, 0x42};
    payload.bitstream = {0, 0, 0, 1, 0x65, 0x88, 0x99};
    return payload;
}

void encodedVideoPayloadRoundtripsMetadataAndBitstream()
{
    const modules::display::DisplayEncodedVideoPayload original =
        samplePayload();
    const protocol::ByteBuffer encoded =
        modules::display::encodeDisplayEncodedVideoPayload(original);
    assert(!encoded.empty());

    const modules::display::DisplayEncodedVideoPayloadDecodeResult decoded =
        modules::display::decodeDisplayEncodedVideoPayload(encoded);
    assert(decoded.ok);
    assert(decoded.payload.codec ==
           modules::display::DisplayEncodedVideoCodec::H264);
    assert(decoded.payload.bitstreamFormat ==
           modules::display::DisplayEncodedVideoBitstreamFormat::AnnexB);
    assert(decoded.payload.frame.frameId == original.frame.frameId);
    assert(decoded.payload.frame.keyFrame);
    assert(decoded.payload.frame.width == original.frame.width);
    assert(decoded.payload.frame.height == original.frame.height);
    assert(decoded.payload.codedWidth == original.codedWidth);
    assert(decoded.payload.codedHeight == original.codedHeight);
    assert(decoded.payload.visibleWidth == original.visibleWidth);
    assert(decoded.payload.visibleHeight == original.visibleHeight);
    assert(decoded.payload.frame.strideBytes == original.frame.strideBytes);
    assert(decoded.payload.frame.pixelFormat == original.frame.pixelFormat);
    assert(decoded.payload.frame.monotonicTimestampUsec ==
           original.frame.monotonicTimestampUsec);
    assert(decoded.payload.sequenceHeader == original.sequenceHeader);
    assert(decoded.payload.bitstream == original.bitstream);
    assert(decoded.payload.frame.payload == original.bitstream);
}

void rejectsInvalidPayloads()
{
    modules::display::DisplayEncodedVideoPayload invalid = samplePayload();
    invalid.codec = modules::display::DisplayEncodedVideoCodec::Unknown;
    assert(modules::display::encodeDisplayEncodedVideoPayload(invalid).empty());

    invalid = samplePayload();
    invalid.bitstreamFormat =
        modules::display::DisplayEncodedVideoBitstreamFormat::Unknown;
    assert(modules::display::encodeDisplayEncodedVideoPayload(invalid).empty());

    invalid = samplePayload();
    invalid.bitstream.clear();
    assert(modules::display::encodeDisplayEncodedVideoPayload(invalid).empty());

    protocol::ByteBuffer truncated = {0x46, 0x44, 0x53};
    modules::display::DisplayEncodedVideoPayloadDecodeResult decoded =
        modules::display::decodeDisplayEncodedVideoPayload(truncated);
    assert(!decoded.ok);
    assert(decoded.error.find("smaller than header") != std::string::npos);

    protocol::ByteBuffer wrongMagic =
        modules::display::encodeDisplayEncodedVideoPayload(samplePayload());
    wrongMagic[0] = 0;
    decoded = modules::display::decodeDisplayEncodedVideoPayload(wrongMagic);
    assert(!decoded.ok);
    assert(decoded.error.find("magic") != std::string::npos);

    protocol::ByteBuffer badSize =
        modules::display::encodeDisplayEncodedVideoPayload(samplePayload());
    badSize.pop_back();
    decoded = modules::display::decodeDisplayEncodedVideoPayload(badSize);
    assert(!decoded.ok);
    assert(decoded.error.find("section sizes") != std::string::npos);
}

void codecNamesAreStable()
{
    assert(std::string(modules::display::displayEncodedVideoCodecName(
               modules::display::DisplayEncodedVideoCodec::H264)) == "h264");
    assert(std::string(modules::display::displayEncodedVideoCodecName(
               modules::display::DisplayEncodedVideoCodec::H265)) == "h265");
    assert(std::string(modules::display::displayEncodedVideoCodecName(
               modules::display::DisplayEncodedVideoCodec::Av1)) == "av1");
    assert(std::string(modules::display::displayEncodedVideoBitstreamFormatName(
               modules::display::DisplayEncodedVideoBitstreamFormat::AnnexB)) ==
           "annex_b");
    assert(std::string(modules::display::displayEncodedVideoBitstreamFormatName(
               modules::display::DisplayEncodedVideoBitstreamFormat::Avcc)) ==
           "avcc");
}

void rawDecoderRejectsCompressedEnvelope()
{
    const protocol::ByteBuffer encoded =
        modules::display::encodeDisplayEncodedVideoPayload(samplePayload());
    modules::display::RawFrameDecoder decoder;
    modules::display::EncodedFrame frame;
    frame.frameId = 42;
    frame.keyFrame = true;
    frame.payload = encoded;
    const modules::display::DecodedFrame decoded = decoder.decode(frame);
    assert(decoded.frameId == 0);
    assert(decoded.pixels.empty());
}

} // namespace

int main()
{
    encodedVideoPayloadRoundtripsMetadataAndBitstream();
    rejectsInvalidPayloads();
    codecNamesAreStable();
    rawDecoderRejectsCompressedEnvelope();
    return 0;
}
