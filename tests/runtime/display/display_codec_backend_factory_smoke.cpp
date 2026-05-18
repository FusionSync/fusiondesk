#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_frame_codec.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCodecSelectionRequest windowsEncoderRequest()
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Encode;
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::H265,
                               runtime::display::DisplayCodecId::RawBgra};
    request.acceptedInputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.acceptedOutputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    return request;
}

void rawFactoryCreatesCurrentMvpCodec()
{
    const std::shared_ptr<runtime::display::IDisplayCodecBackendFactory>
        factory = runtime::display::createRawFrameDisplayCodecBackendFactory(
            runtime::display::DisplayPlatformFamily::WindowsDesktop);

    runtime::display::DisplayCodecEncoderCreateResult encoder =
        runtime::display::createSelectedDisplayEncoder(*factory,
                                                       windowsEncoderRequest());
    assert(encoder.ok);
    assert(encoder.encoder);
    assert(encoder.selection.selected.codec ==
           runtime::display::DisplayCodecId::RawBgra);
    assert(encoder.selection.fallbackSelected);

    modules::display::CapturedFrame captured;
    captured.frameId = 42;
    captured.keyFrame = true;
    captured.width = 1;
    captured.height = 1;
    captured.strideBytes = 4;
    captured.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    captured.pixels = {1, 2, 3, 4};
    const modules::display::EncodedFrame encoded =
        encoder.encoder->encode(captured);
    assert(encoded.frameId == 42);
    assert(!encoded.payload.empty());

    runtime::display::DisplayCodecSelectionRequest decoderRequest =
        windowsEncoderRequest();
    decoderRequest.direction = runtime::display::DisplayCodecDirection::Decode;
    runtime::display::DisplayCodecDecoderCreateResult decoder =
        runtime::display::createSelectedDisplayDecoder(*factory, decoderRequest);
    assert(decoder.ok);
    assert(decoder.decoder);
    const modules::display::DecodedFrame decoded =
        decoder.decoder->decode(encoded);
    assert(decoded.frameId == 42);
    assert(decoded.pixels == captured.pixels);
}

void staticUnavailableFactoryFailsWithMessages()
{
    const std::shared_ptr<runtime::display::IDisplayCodecBackendFactory>
        factory =
            runtime::display::createUnavailableDefaultDisplayCodecBackendFactory(
                runtime::display::DisplayPlatformFamily::WindowsDesktop,
                "adapter package missing");

    runtime::display::DisplayCodecEncoderCreateResult created =
        runtime::display::createSelectedDisplayEncoder(*factory,
                                                       windowsEncoderRequest());
    assert(!created.ok);
    assert(!created.messages.empty());
    assert(created.encoder == nullptr);

    bool sawUnavailable = false;
    for (const std::string& message : created.messages) {
        if (message.find("adapter package missing") != std::string::npos)
            sawUnavailable = true;
    }
    assert(sawUnavailable);
}

void registryForwardsCreation()
{
    runtime::display::DisplayCodecBackendFactoryRegistry registry;
    registry.addFactory(nullptr);
    registry.addFactory(runtime::display::createUnavailableDefaultDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        "optional package not installed"));
    registry.addFactory(runtime::display::createRawFrameDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop));

    assert(registry.factoryCount() == 2);
    assert(registry.capabilities().size() >= 3);

    runtime::display::DisplayCodecEncoderCreateResult created =
        runtime::display::createSelectedDisplayEncoder(registry,
                                                       windowsEncoderRequest());
    assert(created.ok);
    assert(created.encoder);
    assert(created.selection.selected.adapterId == "windows.raw_frame");
}

void exactUnavailableAdapterDoesNotFallBack()
{
    runtime::display::DisplayCodecBackendFactoryRegistry registry;
    registry.addFactory(runtime::display::createUnavailableDefaultDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop,
        "adapter package missing"));
    registry.addFactory(runtime::display::createRawFrameDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop));

    runtime::display::DisplayCodecSelectionRequest request =
        windowsEncoderRequest();
    request.requestedAdapterId = "windows.media_foundation.h264";
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::RawBgra};

    const runtime::display::DisplayCodecEncoderCreateResult created =
        runtime::display::createSelectedDisplayEncoder(registry, request);

    assert(!created.ok);
    bool sawRequestedReject = false;
    bool sawUnavailable = false;
    for (const std::string& message : created.messages) {
        if (message.find("does not match requested adapter id") !=
            std::string::npos)
            sawRequestedReject = true;
        if (message.find("adapter package missing") != std::string::npos)
            sawUnavailable = true;
    }
    assert(sawRequestedReject);
    assert(sawUnavailable);
}

runtime::display::DisplayCodecCapability availableH264Capability()
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = "windows.media_foundation.h264";
    capability.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    capability.backend =
        runtime::display::DisplayCodecBackendKind::WindowsMediaFoundation;
    capability.codec = runtime::display::DisplayCodecId::H264;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.available = true;
    capability.hardwareAccelerated = false;
    capability.zeroCopy = false;
    capability.lowLatency = true;
    capability.priority = 90;
    return capability;
}

void selectedUnavailableFactoryCreationFailureDoesNotFallback()
{
    runtime::display::DisplayCodecBackendFactoryRegistry registry;
    registry.addFactory(
        std::make_shared<runtime::display::StaticDisplayCodecBackendFactory>(
            std::vector<runtime::display::DisplayCodecCapability>{
                availableH264Capability()}));
    registry.addFactory(runtime::display::createRawFrameDisplayCodecBackendFactory(
        runtime::display::DisplayPlatformFamily::WindowsDesktop));

    runtime::display::DisplayCodecSelectionRequest request =
        windowsEncoderRequest();
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::RawBgra};

    const runtime::display::DisplayCodecEncoderCreateResult created =
        runtime::display::createSelectedDisplayEncoder(registry, request);

    assert(!created.ok);
    assert(created.encoder == nullptr);
    assert(created.selection.hasSelection);
    assert(created.selection.selected.adapterId == "windows.media_foundation.h264");
    assert(!created.messages.empty());
    assert(created.messages[0].find("could not create selected encoder") !=
           std::string::npos);
}

} // namespace

int main()
{
    rawFactoryCreatesCurrentMvpCodec();
    staticUnavailableFactoryFailsWithMessages();
    registryForwardsCreation();
    exactUnavailableAdapterDoesNotFallBack();
    selectedUnavailableFactoryCreationFailureDoesNotFallback();
    return 0;
}
