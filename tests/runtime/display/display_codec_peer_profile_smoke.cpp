#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/runtime/display/display_codec_negotiation.h"
#include "fusiondesk/runtime/display/display_codec_peer_profile.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCodecCapability h264Hardware(std::string adapterId)
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
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
    capability.maxWidth = 8192;
    capability.maxHeight = 8192;
    capability.priority = 90;
    return capability;
}

runtime::display::DisplayCodecCapability rawFrame(std::string adapterId)
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
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
    runtime::display::DisplayCodecDirection direction,
    std::vector<runtime::display::DisplayCodecCapability> candidates)
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = direction;
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::RawBgra};
    request.acceptedInputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.acceptedOutputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.architecture = runtime::display::DisplayTargetArchitecture::X86_64;
    request.socProfile = runtime::display::DisplayTargetSocProfile::Generic;
    request.width = 1920;
    request.height = 1080;
    request.candidates = std::move(candidates);
    return request;
}

void roundTripsCodecPeerProfilePayload()
{
    runtime::display::DisplayCodecPeerProfilePayload payload;
    payload.hasEncoderRequest = true;
    payload.encoderRequest = codecRequest(
        runtime::display::DisplayCodecDirection::Encode,
        {rawFrame("windows.raw_frame"),
         h264Hardware("windows.media_foundation.h264")});
    payload.hasDecoderRequest = true;
    payload.decoderRequest = codecRequest(
        runtime::display::DisplayCodecDirection::Decode,
        {rawFrame("windows.raw_frame"),
         h264Hardware("windows.media_foundation.h264")});

    const protocol::ByteBuffer encoded =
        runtime::display::encodeDisplayCodecPeerProfilePayload(payload);
    assert(!encoded.empty());
    assert(std::string(runtime::display::displayCodecPeerProfileExtensionKey()) ==
           "display.codec.v1");

    const runtime::display::DisplayCodecPeerProfileDecodeResult decoded =
        runtime::display::decodeDisplayCodecPeerProfilePayload(encoded);
    assert(decoded.ok);
    assert(decoded.payload.hasEncoderRequest);
    assert(decoded.payload.hasDecoderRequest);
    assert(decoded.payload.encoderRequest.direction ==
           runtime::display::DisplayCodecDirection::Encode);
    assert(decoded.payload.decoderRequest.direction ==
           runtime::display::DisplayCodecDirection::Decode);
    assert(decoded.payload.encoderRequest.candidates.size() == 2);
    assert(decoded.payload.decoderRequest.candidates.back().adapterId ==
           "windows.media_foundation.h264");
    assert(decoded.payload.decoderRequest.candidates.back().maxWidth == 8192);

    runtime::display::DisplayCodecNegotiationRequest negotiation;
    negotiation.encoderRequest = decoded.payload.encoderRequest;
    negotiation.decoderRequest = decoded.payload.decoderRequest;
    const runtime::display::DisplayCodecNegotiationResult negotiated =
        runtime::display::negotiateDisplayCodec(negotiation);
    assert(negotiated.ok);
    assert(negotiated.codec == runtime::display::DisplayCodecId::H264);
}

void rejectsMalformedCodecPeerProfilePayload()
{
    protocol::ByteBuffer truncated = {0x46, 0x44, 0x43};
    runtime::display::DisplayCodecPeerProfileDecodeResult decoded =
        runtime::display::decodeDisplayCodecPeerProfilePayload(truncated);
    assert(!decoded.ok);
    assert(!decoded.message.empty());

    protocol::ByteBuffer encoded =
        runtime::display::encodeDisplayCodecPeerProfilePayload({});
    assert(!encoded.empty());
    encoded.push_back(0xff);
    decoded = runtime::display::decodeDisplayCodecPeerProfilePayload(encoded);
    assert(!decoded.ok);
    assert(decoded.message.find("trailing bytes") != std::string::npos);
}

} // namespace

int main()
{
    roundTripsCodecPeerProfilePayload();
    rejectsMalformedCodecPeerProfilePayload();
    return 0;
}
