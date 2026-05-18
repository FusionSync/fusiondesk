#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_encoded_video_payload.h"
#include "fusiondesk/platform/windows/display/windows_media_foundation_display_codec.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCodecSelectionRequest windowsH264Request()
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Encode;
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::RawBgra};
    request.acceptedInputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.acceptedOutputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    return request;
}

modules::display::CapturedFrame makeBgraFrame(std::uint64_t frameId = 7,
                                              bool keyFrame = true)
{
    modules::display::CapturedFrame frame;
    frame.frameId = frameId;
    frame.keyFrame = keyFrame;
    frame.width = 640;
    frame.height = 360;
    frame.strideBytes = frame.width * 4;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.monotonicTimestampUsec = 1000 + frameId * 33333;
    frame.pixels.resize(static_cast<std::size_t>(frame.strideBytes) *
                        frame.height);
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y) * frame.strideBytes +
                static_cast<std::size_t>(x) * 4U;
            frame.pixels[offset] =
                static_cast<std::uint8_t>((x + y + frameId) & 0xffU);
            frame.pixels[offset + 1] =
                static_cast<std::uint8_t>((x * 3U + frameId) & 0xffU);
            frame.pixels[offset + 2] =
                static_cast<std::uint8_t>((y * 5U + frameId) & 0xffU);
            frame.pixels[offset + 3] = 255;
        }
    }
    return frame;
}

bool containsAnnexBNalType(const std::vector<std::uint8_t>& bytes,
                           int expectedNalType)
{
    for (std::size_t index = 0; index + 4 < bytes.size(); ++index) {
        std::size_t startCodeBytes = 0;
        if (bytes[index] == 0 && bytes[index + 1] == 0 &&
            bytes[index + 2] == 1) {
            startCodeBytes = 3;
        } else if (index + 5 < bytes.size() && bytes[index] == 0 &&
                   bytes[index + 1] == 0 && bytes[index + 2] == 0 &&
                   bytes[index + 3] == 1) {
            startCodeBytes = 4;
        }
        if (startCodeBytes == 0)
            continue;

        const std::size_t nalOffset = index + startCodeBytes;
        if (nalOffset >= bytes.size())
            break;
        if (static_cast<int>(bytes[nalOffset] & 0x1fU) == expectedNalType)
            return true;
        index = nalOffset;
    }
    return false;
}

void disabledRolloutPublishesUnavailableCapability()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "0");

    platform::windows::display::WindowsMediaFoundationDisplayCodecBackendFactory
        factory;
    const std::vector<runtime::display::DisplayCodecCapability> capabilities =
        factory.capabilities();

    assert(capabilities.size() == 1);
    assert(capabilities[0].adapterId == "windows.media_foundation.h264");
    assert(capabilities[0].codec == runtime::display::DisplayCodecId::H264);
    assert(capabilities[0].backend ==
           runtime::display::DisplayCodecBackendKind::WindowsMediaFoundation);
    assert(!capabilities[0].available);
    assert(!capabilities[0].zeroCopy);
    assert(!capabilities[0].requiresHardwareDevice);
    assert(capabilities[0].unavailableReason.find("rollout is not enabled") !=
           std::string::npos);
}

void registryFallsBackToRawWhenMediaFoundationIsDisabled()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "0");

    runtime::display::DisplayCodecBackendFactoryRegistry registry;
    registry.addFactory(std::make_shared<
                        platform::windows::display::
                            WindowsMediaFoundationDisplayCodecBackendFactory>());
    registry.addFactory(runtime::display::createRawFrameDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop));

    const runtime::display::DisplayCodecEncoderCreateResult created =
        runtime::display::createSelectedDisplayEncoder(registry,
                                                       windowsH264Request());

    assert(created.ok);
    assert(created.encoder);
    assert(created.selection.selected.adapterId == "windows.raw_frame");
    assert(created.selection.selected.codec ==
           runtime::display::DisplayCodecId::RawBgra);

    bool sawMediaFoundationReject = false;
    for (const runtime::display::DisplayCodecRejection& rejection :
         created.selection.rejected) {
        if (rejection.adapterId == "windows.media_foundation.h264" &&
            rejection.reason.find("rollout is not enabled") != std::string::npos)
            sawMediaFoundationReject = true;
    }
    assert(sawMediaFoundationReject);
}

void optInProbeIsStable()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    const platform::windows::display::WindowsMediaFoundationCodecProbeResult
        probe =
            platform::windows::display::probeWindowsMediaFoundationH264Codec();
    assert(probe.rolloutEnabled);
    assert(!probe.message.empty());
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
}

void disabledPreflightDoesNotTouchAdapter()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "0");
    const platform::windows::display::
        WindowsMediaFoundationH264AdapterPreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Adapter(640, 360);
    assert(!preflight.rolloutEnabled);
    assert(!preflight.mediaFoundationStarted);
    assert(!preflight.h264EncoderMftCreated);
    assert(!preflight.h264DecoderMftCreated);
    assert(!preflight.bgraToNv12ConversionOk);
    assert(preflight.message.find("rollout is not enabled") !=
           std::string::npos);
}

void oddPreflightSizeIsRejectedBeforeAdapterStartup()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    const platform::windows::display::
        WindowsMediaFoundationH264AdapterPreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Adapter(641, 360);
    assert(preflight.rolloutEnabled);
    assert(!preflight.mediaFoundationStarted);
    assert(!preflight.bgraToNv12ConversionOk);
    assert(preflight.message.find("even dimensions") != std::string::npos);
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
}

void optInPreflightIsStable()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    const platform::windows::display::
        WindowsMediaFoundationH264AdapterPreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Adapter(640, 360);
    assert(preflight.rolloutEnabled);
    assert(preflight.bgraToNv12ConversionOk);
    assert(preflight.bgraToNv12YPlaneBytes == 640ULL * 360ULL);
    assert(preflight.bgraToNv12UvPlaneBytes == 640ULL * 180ULL);
    assert(preflight.bgraToNv12Bytes ==
           preflight.bgraToNv12YPlaneBytes +
               preflight.bgraToNv12UvPlaneBytes);
    assert(!preflight.message.empty());
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
}

void disabledEncodePreflightDoesNotTouchAdapter()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "0");
    const platform::windows::display::
        WindowsMediaFoundationH264EncodePreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Encode(640, 360);
    assert(!preflight.rolloutEnabled);
    assert(!preflight.mediaFoundationStarted);
    assert(!preflight.bgraToNv12ConversionOk);
    assert(!preflight.h264EncoderMftCreated);
    assert(preflight.message.find("rollout is not enabled") !=
           std::string::npos);
}

void oddEncodePreflightSizeIsRejectedBeforeAdapterStartup()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    const platform::windows::display::
        WindowsMediaFoundationH264EncodePreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Encode(641, 360);
    assert(preflight.rolloutEnabled);
    assert(!preflight.mediaFoundationStarted);
    assert(!preflight.bgraToNv12ConversionOk);
    assert(preflight.message.find("even dimensions") != std::string::npos);
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
}

void optInEncodePreflightIsStable()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    const platform::windows::display::
        WindowsMediaFoundationH264EncodePreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Encode(640, 360);
    assert(preflight.rolloutEnabled);
    assert(preflight.bgraToNv12ConversionOk);
    assert(preflight.bgraToNv12Bytes == 640ULL * 360ULL * 3ULL / 2ULL);
    assert(!preflight.message.empty());
    if (preflight.outputSampleProduced) {
        assert(preflight.encoderNv12InputAccepted ||
               preflight.encoderBgraInputAccepted);
        assert(preflight.bitstreamBytes > 0);
        assert(preflight.fdsfPayloadEncoded);
        assert(preflight.fdsfPayloadBytes > preflight.bitstreamBytes);
    }
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
}

void disabledFactoryDoesNotCreateEncoder()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "0");
    platform::windows::display::WindowsMediaFoundationDisplayCodecBackendFactory
        factory;
    const std::vector<runtime::display::DisplayCodecCapability> capabilities =
        factory.capabilities();
    assert(capabilities.size() == 1);
    assert(!factory.createEncoder(capabilities[0]));
    assert(!factory.createDecoder(capabilities[0]));
}

void optInFactoryCreatesEncoderWithoutSelectingCapability()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "0");
    platform::windows::display::WindowsMediaFoundationDisplayCodecBackendFactory
        factory;
    const std::vector<runtime::display::DisplayCodecCapability> capabilities =
        factory.capabilities();
    assert(capabilities.size() == 1);
    assert(!capabilities[0].available);
    assert(factory.createEncoder(capabilities[0]));
    assert(factory.createDecoder(capabilities[0]));
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "");
}

void optInSelectionCreatesH264CodecObjects()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "1");
    runtime::display::DisplayCodecBackendFactoryRegistry registry;
    registry.addFactory(std::make_shared<
                        platform::windows::display::
                            WindowsMediaFoundationDisplayCodecBackendFactory>());
    registry.addFactory(runtime::display::createRawFrameDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop));

    runtime::display::DisplayCodecSelectionRequest request =
        windowsH264Request();
    request.requestedAdapterId = "windows.media_foundation.h264";

    const runtime::display::DisplayCodecEncoderCreateResult encoderCreated =
        runtime::display::createSelectedDisplayEncoder(registry, request);
    assert(encoderCreated.ok);
    assert(encoderCreated.encoder);
    assert(encoderCreated.selection.selected.adapterId ==
           "windows.media_foundation.h264");
    assert(encoderCreated.selection.selected.codec ==
           runtime::display::DisplayCodecId::H264);

    request.direction = runtime::display::DisplayCodecDirection::Decode;
    const runtime::display::DisplayCodecDecoderCreateResult decoderCreated =
        runtime::display::createSelectedDisplayDecoder(registry, request);
    assert(decoderCreated.ok);
    assert(decoderCreated.decoder);
    assert(decoderCreated.selection.selected.adapterId ==
           "windows.media_foundation.h264");
    assert(decoderCreated.selection.selected.codec ==
           runtime::display::DisplayCodecId::H264);

    const modules::display::EncodedFrame encoded =
        encoderCreated.encoder->encode(makeBgraFrame());
    assert(!encoded.payload.empty());
    const modules::display::DecodedFrame rendered =
        decoderCreated.decoder->decode(encoded);
    assert(rendered.frameId == 7);
    assert(rendered.width == 640);
    assert(rendered.height == 360);
    assert(rendered.pixelFormat ==
           modules::display::DisplayPixelFormat::Bgra32);

    const modules::display::EncodedFrame delta =
        encoderCreated.encoder->encode(makeBgraFrame(8, false));
    assert(delta.frameId == 8);
    assert(delta.keyFrame);
    assert(!delta.payload.empty());
    const modules::display::DecodedFrame deltaRendered =
        decoderCreated.decoder->decode(delta);
    assert(deltaRendered.frameId == 8);
    assert(deltaRendered.keyFrame);
    assert(deltaRendered.width == 640);
    assert(deltaRendered.height == 360);
    assert(deltaRendered.pixelFormat ==
           modules::display::DisplayPixelFormat::Bgra32);

    const modules::display::EncodedFrame secondKeyframe =
        encoderCreated.encoder->encode(makeBgraFrame(9, true));
    assert(secondKeyframe.frameId == 9);
    assert(secondKeyframe.keyFrame);
    assert(!secondKeyframe.payload.empty());
    const modules::display::DecodedFrame secondRendered =
        decoderCreated.decoder->decode(secondKeyframe);
    assert(secondRendered.frameId == 9);
    assert(secondRendered.width == 640);
    assert(secondRendered.height == 360);
    assert(secondRendered.pixelFormat ==
           modules::display::DisplayPixelFormat::Bgra32);
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "");
}

void productionPolicySelectsH264AndUsesPFrames()
{
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "0");
    _putenv_s("FUSIONDESK_MF_H264_PFRAME", "");
    _putenv_s("FUSIONDESK_ENABLE_MF_H264_PRODUCTION", "1");

    runtime::display::DisplayCodecBackendFactoryRegistry registry;
    registry.addFactory(std::make_shared<
                        platform::windows::display::
                            WindowsMediaFoundationDisplayCodecBackendFactory>());
    registry.addFactory(runtime::display::createRawFrameDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop));

    runtime::display::DisplayCodecSelectionRequest request =
        windowsH264Request();
    const runtime::display::DisplayCodecEncoderCreateResult encoderCreated =
        runtime::display::createSelectedDisplayEncoder(registry, request);
    assert(encoderCreated.ok);
    assert(encoderCreated.encoder);
    assert(encoderCreated.selection.selected.adapterId ==
           "windows.media_foundation.h264");
    assert(!encoderCreated.selection.fallbackSelected);

    request.direction = runtime::display::DisplayCodecDirection::Decode;
    const runtime::display::DisplayCodecDecoderCreateResult decoderCreated =
        runtime::display::createSelectedDisplayDecoder(registry, request);
    assert(decoderCreated.ok);
    assert(decoderCreated.decoder);
    assert(decoderCreated.selection.selected.adapterId ==
           "windows.media_foundation.h264");
    assert(!decoderCreated.selection.fallbackSelected);

    bool decodedKeyframe = false;
    bool decodedPFrame = false;
    bool sawPFrameBitstream = false;

    const modules::display::EncodedFrame keyframe =
        encoderCreated.encoder->encode(makeBgraFrame(101, true));
    assert(keyframe.keyFrame);
    modules::display::DecodedFrame output =
        decoderCreated.decoder->decode(keyframe);
    assert(output.decodeStatus == modules::display::DisplayDecodeStatus::Ok ||
           output.decodeStatus ==
               modules::display::DisplayDecodeStatus::NeedsMoreInput);
    if (output.decodeStatus == modules::display::DisplayDecodeStatus::Ok)
        decodedKeyframe = output.frameId == 101;

    for (std::uint64_t frameId = 102; frameId <= 106; ++frameId) {
        const modules::display::EncodedFrame pframe =
            encoderCreated.encoder->encode(makeBgraFrame(frameId, false));
        assert(!pframe.keyFrame);
        const modules::display::DisplayEncodedVideoPayloadDecodeResult
            payload =
                modules::display::decodeDisplayEncodedVideoPayload(
                    pframe.payload);
        assert(payload.ok);
        sawPFrameBitstream =
            sawPFrameBitstream ||
            containsAnnexBNalType(payload.payload.bitstream, 1);

        output = decoderCreated.decoder->decode(pframe);
        assert(output.decodeStatus ==
                   modules::display::DisplayDecodeStatus::Ok ||
               output.decodeStatus ==
                   modules::display::DisplayDecodeStatus::NeedsMoreInput);
        if (output.decodeStatus != modules::display::DisplayDecodeStatus::Ok)
            continue;
        if (output.frameId == 101)
            decodedKeyframe = true;
        if (output.frameId > 101 && output.frameId <= frameId)
            decodedPFrame = true;
    }

    assert(sawPFrameBitstream);
    assert(decodedKeyframe);
    assert(decodedPFrame);

    _putenv_s("FUSIONDESK_ENABLE_MF_H264_PRODUCTION", "");
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "");
}

void manualEncodeValidationGate()
{
    const char* validate = std::getenv("FUSIONDESK_VALIDATE_MF_H264_ENCODE");
    if (validate == nullptr || std::string(validate) != "1")
        return;

    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    const platform::windows::display::
        WindowsMediaFoundationH264EncodePreflightResult preflight =
            platform::windows::display::
                preflightWindowsMediaFoundationH264Encode(640, 360);
    std::cout << "MF H264 encode preflight: " << preflight.message
              << std::endl;
    assert(preflight.rolloutEnabled);
    assert(preflight.bgraToNv12ConversionOk);
    assert(preflight.h264EncoderMftCreated);
    assert(preflight.encoderOutputTypeAccepted);
    assert(preflight.encoderNv12InputAccepted ||
           preflight.encoderBgraInputAccepted);
    assert(preflight.streamingStarted);
    assert(preflight.inputSampleAccepted);
    assert(preflight.outputSampleProduced);
    assert(preflight.bitstreamBytes > 0);
    assert(preflight.fdsfPayloadEncoded);
    assert(preflight.fdsfPayloadBytes > preflight.bitstreamBytes);

    platform::windows::display::WindowsMediaFoundationDisplayCodecBackendFactory
        factory;
    const std::vector<runtime::display::DisplayCodecCapability> capabilities =
        factory.capabilities();
    assert(capabilities.size() == 1);
    std::shared_ptr<modules::display::IVideoEncoder> encoder =
        factory.createEncoder(capabilities[0]);
    std::shared_ptr<modules::display::IVideoDecoder> decoder =
        factory.createDecoder(capabilities[0]);
    assert(encoder);
    assert(decoder);

    const modules::display::EncodedFrame encoded =
        encoder->encode(makeBgraFrame());
    assert(encoded.frameId == 7);
    assert(encoded.width == 640);
    assert(encoded.height == 360);
    assert(encoded.pixelFormat == modules::display::DisplayPixelFormat::Bgra32);
    assert(!encoded.payload.empty());

    const modules::display::DisplayEncodedVideoPayloadDecodeResult decoded =
        modules::display::decodeDisplayEncodedVideoPayload(encoded.payload);
    assert(decoded.ok);
    assert(decoded.payload.codec ==
           modules::display::DisplayEncodedVideoCodec::H264);
    assert(decoded.payload.frame.frameId == 7);
    assert(!decoded.payload.bitstream.empty());

    const modules::display::DecodedFrame rendered = decoder->decode(encoded);
    std::cout << "MF H264 factory decode: frameId=" << rendered.frameId
              << " width=" << rendered.width
              << " height=" << rendered.height
              << " stride=" << rendered.strideBytes
              << " pixels=" << rendered.pixels.size() << std::endl;
    assert(rendered.frameId == 7);
    assert(rendered.width == 640);
    assert(rendered.height == 360);
    assert(rendered.strideBytes == 640 * 4);
    assert(rendered.pixelFormat ==
           modules::display::DisplayPixelFormat::Bgra32);
    assert(rendered.pixels.size() >= 640ULL * 360ULL * 4ULL);
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
}

void manualPFrameValidationGate()
{
    const char* validate = std::getenv("FUSIONDESK_VALIDATE_MF_H264_PFRAME");
    if (validate == nullptr || std::string(validate) != "1")
        return;

    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "1");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "1");
    _putenv_s("FUSIONDESK_MF_H264_PFRAME", "1");

    platform::windows::display::WindowsMediaFoundationDisplayCodecBackendFactory
        factory;
    const std::vector<runtime::display::DisplayCodecCapability> capabilities =
        factory.capabilities();
    assert(capabilities.size() == 1);
    assert(capabilities[0].available);
    std::shared_ptr<modules::display::IVideoEncoder> encoder =
        factory.createEncoder(capabilities[0]);
    std::shared_ptr<modules::display::IVideoDecoder> decoder =
        factory.createDecoder(capabilities[0]);
    assert(encoder);
    assert(decoder);

    bool decodedKeyframe = false;
    bool decodedPFrame = false;
    bool sawPFrameBitstream = false;

    const modules::display::EncodedFrame keyframe =
        encoder->encode(makeBgraFrame(101, true));
    assert(keyframe.keyFrame);
    modules::display::DecodedFrame output = decoder->decode(keyframe);
    assert(output.decodeStatus == modules::display::DisplayDecodeStatus::Ok ||
           output.decodeStatus ==
               modules::display::DisplayDecodeStatus::NeedsMoreInput);
    if (output.decodeStatus == modules::display::DisplayDecodeStatus::Ok)
        decodedKeyframe = output.frameId == 101;

    for (std::uint64_t frameId = 102; frameId <= 112; ++frameId) {
        const modules::display::EncodedFrame pframe =
            encoder->encode(makeBgraFrame(frameId, false));
        assert(!pframe.keyFrame);
        const modules::display::DisplayEncodedVideoPayloadDecodeResult
            pPayload =
                modules::display::decodeDisplayEncodedVideoPayload(
                    pframe.payload);
        assert(pPayload.ok);
        sawPFrameBitstream =
            sawPFrameBitstream ||
            containsAnnexBNalType(pPayload.payload.bitstream, 1);

        output = decoder->decode(pframe);
        assert(output.decodeStatus ==
                   modules::display::DisplayDecodeStatus::Ok ||
               output.decodeStatus ==
                   modules::display::DisplayDecodeStatus::NeedsMoreInput);
        if (output.decodeStatus != modules::display::DisplayDecodeStatus::Ok)
            continue;
        if (output.frameId == 101)
            decodedKeyframe = true;
        if (output.frameId > 101 && output.frameId <= frameId)
            decodedPFrame = true;
    }

    assert(sawPFrameBitstream);
    assert(decodedKeyframe);
    assert(decodedPFrame);

    _putenv_s("FUSIONDESK_MF_H264_PFRAME", "");
    _putenv_s("FUSIONDESK_ENABLE_MF_CODEC", "");
    _putenv_s("FUSIONDESK_SELECT_MF_H264", "");
}

} // namespace

int main()
{
    disabledRolloutPublishesUnavailableCapability();
    registryFallsBackToRawWhenMediaFoundationIsDisabled();
    optInProbeIsStable();
    disabledPreflightDoesNotTouchAdapter();
    oddPreflightSizeIsRejectedBeforeAdapterStartup();
    optInPreflightIsStable();
    disabledEncodePreflightDoesNotTouchAdapter();
    oddEncodePreflightSizeIsRejectedBeforeAdapterStartup();
    optInEncodePreflightIsStable();
    disabledFactoryDoesNotCreateEncoder();
    optInFactoryCreatesEncoderWithoutSelectingCapability();
    optInSelectionCreatesH264CodecObjects();
    productionPolicySelectsH264AndUsesPFrames();
    manualEncodeValidationGate();
    manualPFrameValidationGate();
    return 0;
}
