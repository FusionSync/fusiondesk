#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/runtime/display/display_codec_negotiation.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCodecCapability h264Hardware(
    std::string adapterId,
    runtime::display::DisplayPlatformFamily platform)
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = platform;
    capability.backend =
        runtime::display::DisplayCodecBackendKind::WindowsMediaFoundation;
    capability.codec = runtime::display::DisplayCodecId::H264;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::D3DTexture,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::D3DTexture,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.hardwareAccelerated = true;
    capability.zeroCopy = true;
    capability.lowLatency = true;
    capability.requiresHardwareDevice = true;
    capability.priority = 90;
    return capability;
}

runtime::display::DisplayCodecCapability rawSoftware(
    std::string adapterId,
    runtime::display::DisplayPlatformFamily platform)
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = platform;
    capability.backend = runtime::display::DisplayCodecBackendKind::RawFrame;
    capability.codec = runtime::display::DisplayCodecId::RawBgra;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.fallback = true;
    capability.lowLatency = true;
    capability.priority = 5;
    return capability;
}

runtime::display::DisplayCodecSelectionRequest codecRequest(
    runtime::display::DisplayPlatformFamily platform,
    std::vector<runtime::display::DisplayCodecCapability> candidates)
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = platform;
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::RawBgra};
    request.acceptedInputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(platform);
    request.acceptedOutputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(platform);
    request.candidates = std::move(candidates);
    return request;
}

void negotiatesH264WhenBothPeersSupportIt()
{
    runtime::display::DisplayCodecNegotiationRequest request;
    request.encoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop),
         h264Hardware("windows.media_foundation.h264",
                      runtime::display::DisplayPlatformFamily::WindowsDesktop)});
    request.decoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop),
         h264Hardware("windows.media_foundation.h264",
                      runtime::display::DisplayPlatformFamily::WindowsDesktop)});

    const runtime::display::DisplayCodecNegotiationResult negotiated =
        runtime::display::negotiateDisplayCodec(request);

    assert(negotiated.ok);
    assert(negotiated.hasSelection);
    assert(negotiated.codec == runtime::display::DisplayCodecId::H264);
    assert(negotiated.encoderSelection.selected.adapterId ==
           "windows.media_foundation.h264");
    assert(negotiated.decoderSelection.selected.adapterId ==
           "windows.media_foundation.h264");
    assert(!negotiated.fallbackSelected);
}

void fallsBackToRawWhenRemoteDecoderLacksH264()
{
    runtime::display::DisplayCodecNegotiationRequest request;
    request.encoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop),
         h264Hardware("windows.media_foundation.h264",
                      runtime::display::DisplayPlatformFamily::WindowsDesktop)});
    request.decoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop)});

    const runtime::display::DisplayCodecNegotiationResult negotiated =
        runtime::display::negotiateDisplayCodec(request);

    assert(negotiated.ok);
    assert(negotiated.codec == runtime::display::DisplayCodecId::RawBgra);
    assert(negotiated.fallbackSelected);
    assert(negotiated.attempts.size() == 2);
    assert(!negotiated.messages.empty());
}

void exactRequestedBackendDoesNotSilentlyFallback()
{
    runtime::display::DisplayCodecNegotiationRequest request;
    request.encoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop),
         h264Hardware("windows.media_foundation.h264",
                      runtime::display::DisplayPlatformFamily::WindowsDesktop)});
    request.decoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop)});
    request.encoderRequest.requestedAdapterId = "windows.media_foundation.h264";
    request.decoderRequest.requestedAdapterId = "windows.media_foundation.h264";

    const runtime::display::DisplayCodecNegotiationResult negotiated =
        runtime::display::negotiateDisplayCodec(request);

    assert(!negotiated.ok);
    assert(!negotiated.hasSelection);
    bool sawRequestedAdapterReject = false;
    for (const std::string& message : negotiated.messages) {
        if (message.find("requested adapter id") != std::string::npos)
            sawRequestedAdapterReject = true;
    }
    assert(sawRequestedAdapterReject);
}

void rejectsMismatchedDirectionsAndPreferences()
{
    runtime::display::DisplayCodecNegotiationRequest request;
    request.encoderRequest = codecRequest(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        {rawSoftware("windows.raw_frame",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop)});
    request.decoderRequest = request.encoderRequest;
    request.encoderRequest.direction =
        runtime::display::DisplayCodecDirection::Decode;

    runtime::display::DisplayCodecNegotiationResult negotiated =
        runtime::display::negotiateDisplayCodec(request);
    assert(!negotiated.ok);
    assert(!negotiated.messages.empty());

    request.encoderRequest.direction =
        runtime::display::DisplayCodecDirection::Unknown;
    request.encoderRequest.codecPreference = {runtime::display::DisplayCodecId::H264};
    request.decoderRequest.codecPreference = {runtime::display::DisplayCodecId::RawBgra};
    negotiated = runtime::display::negotiateDisplayCodec(request);
    assert(!negotiated.ok);
    assert(negotiated.messages[0].find("no common codec preference") !=
           std::string::npos);
}

} // namespace

int main()
{
    negotiatesH264WhenBothPeersSupportIt();
    fallsBackToRawWhenRemoteDecoderLacksH264();
    exactRequestedBackendDoesNotSilentlyFallback();
    rejectsMismatchedDirectionsAndPreferences();
    return 0;
}
