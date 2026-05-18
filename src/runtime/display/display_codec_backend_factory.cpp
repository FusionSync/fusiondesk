#include "fusiondesk/runtime/display/display_codec_backend_factory.h"

#include <memory>
#include <utility>

#include "fusiondesk/modules/display/display_frame_codec.h"

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

void appendSelectionMessages(const DisplayCodecSelectionResult& selection,
                             std::vector<std::string>& messages)
{
    messages = selection.messages;
    for (const DisplayCodecRejection& rejection : selection.rejected) {
        std::string message = "display codec rejected: ";
        message += rejection.adapterId.empty()
                       ? displayCodecBackendKindName(rejection.backend)
                       : rejection.adapterId;
        message += "/";
        message += displayCodecIdName(rejection.codec);
        if (!rejection.reason.empty()) {
            message += ": ";
            message += rejection.reason;
        }
        messages.push_back(std::move(message));
    }
}

DisplayCodecSelectionRequest requestWithFactoryCapabilities(
    const IDisplayCodecBackendFactory& factory,
    const DisplayCodecSelectionRequest& request)
{
    DisplayCodecSelectionRequest selectionRequest = request;
    if (selectionRequest.candidates.empty())
        selectionRequest.candidates = factory.capabilities();
    return selectionRequest;
}

bool isRawFrameSelection(const DisplayCodecCapability& selected)
{
    return selected.codec == DisplayCodecId::RawBgra &&
           selected.backend == DisplayCodecBackendKind::RawFrame;
}

std::string rawFrameAdapterId(DisplayPlatformFamily platform)
{
    switch (platform) {
    case DisplayPlatformFamily::WindowsDesktop:
        return "windows.raw_frame";
    case DisplayPlatformFamily::LinuxX11:
    case DisplayPlatformFamily::LinuxWayland:
    case DisplayPlatformFamily::LinuxEmbedded:
        return "linux.raw_frame";
    case DisplayPlatformFamily::MacOS:
        return "macos.raw_frame";
    case DisplayPlatformFamily::AndroidClient:
    case DisplayPlatformFamily::AndroidAgent:
        return "android.raw_frame";
    case DisplayPlatformFamily::RockchipLinux:
        return "linux.rockchip.raw_frame";
    case DisplayPlatformFamily::RockchipAndroid:
        return "android.rockchip.raw_frame";
    case DisplayPlatformFamily::HarmonyOS:
        return "harmonyos.raw_frame";
    case DisplayPlatformFamily::OpenHarmony:
        return "openharmony.raw_frame";
    case DisplayPlatformFamily::Unknown:
        break;
    }
    return "raw_frame";
}

DisplayCodecCapability rawFrameCapability(DisplayPlatformFamily platform)
{
    DisplayCodecCapability capability;
    capability.adapterId = rawFrameAdapterId(platform);
    capability.platform = platform;
    capability.backend = DisplayCodecBackendKind::RawFrame;
    capability.codec = DisplayCodecId::RawBgra;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode =
        platform != DisplayPlatformFamily::AndroidClient;
    capability.supportsDecode = true;
    capability.available = true;
    capability.fallback = true;
    capability.lowLatency = true;
    capability.priority = 5;
    return capability;
}

} // namespace

StaticDisplayCodecBackendFactory::StaticDisplayCodecBackendFactory(
    std::vector<DisplayCodecCapability> capabilities)
    : capabilities_(std::move(capabilities))
{
}

std::vector<DisplayCodecCapability>
StaticDisplayCodecBackendFactory::capabilities() const
{
    return capabilities_;
}

std::shared_ptr<modules::display::IVideoEncoder>
StaticDisplayCodecBackendFactory::createEncoder(
    const DisplayCodecCapability& selected) const
{
    (void)selected;
    return nullptr;
}

std::shared_ptr<modules::display::IVideoDecoder>
StaticDisplayCodecBackendFactory::createDecoder(
    const DisplayCodecCapability& selected) const
{
    (void)selected;
    return nullptr;
}

RawFrameDisplayCodecBackendFactory::RawFrameDisplayCodecBackendFactory(
    DisplayPlatformFamily platform)
    : platform_(platform)
{
}

std::vector<DisplayCodecCapability>
RawFrameDisplayCodecBackendFactory::capabilities() const
{
    return {rawFrameCapability(platform_)};
}

std::shared_ptr<modules::display::IVideoEncoder>
RawFrameDisplayCodecBackendFactory::createEncoder(
    const DisplayCodecCapability& selected) const
{
    if (!isRawFrameSelection(selected) || !selected.supportsEncode)
        return nullptr;

    return std::make_shared<modules::display::RawFrameEncoder>();
}

std::shared_ptr<modules::display::IVideoDecoder>
RawFrameDisplayCodecBackendFactory::createDecoder(
    const DisplayCodecCapability& selected) const
{
    if (!isRawFrameSelection(selected) || !selected.supportsDecode)
        return nullptr;

    return std::make_shared<modules::display::RawFrameDecoder>();
}

void DisplayCodecBackendFactoryRegistry::addFactory(
    std::shared_ptr<IDisplayCodecBackendFactory> factory)
{
    if (factory)
        factories_.push_back(std::move(factory));
}

std::size_t DisplayCodecBackendFactoryRegistry::factoryCount() const
{
    return factories_.size();
}

std::vector<DisplayCodecCapability>
DisplayCodecBackendFactoryRegistry::capabilities() const
{
    std::vector<DisplayCodecCapability> result;
    for (const std::shared_ptr<IDisplayCodecBackendFactory>& factory :
         factories_) {
        if (!factory)
            continue;

        std::vector<DisplayCodecCapability> values = factory->capabilities();
        result.insert(result.end(), values.begin(), values.end());
    }
    return result;
}

std::shared_ptr<modules::display::IVideoEncoder>
DisplayCodecBackendFactoryRegistry::createEncoder(
    const DisplayCodecCapability& selected) const
{
    for (const std::shared_ptr<IDisplayCodecBackendFactory>& factory :
         factories_) {
        if (!factory)
            continue;

        std::shared_ptr<modules::display::IVideoEncoder> encoder =
            factory->createEncoder(selected);
        if (encoder)
            return encoder;
    }
    return nullptr;
}

std::shared_ptr<modules::display::IVideoDecoder>
DisplayCodecBackendFactoryRegistry::createDecoder(
    const DisplayCodecCapability& selected) const
{
    for (const std::shared_ptr<IDisplayCodecBackendFactory>& factory :
         factories_) {
        if (!factory)
            continue;

        std::shared_ptr<modules::display::IVideoDecoder> decoder =
            factory->createDecoder(selected);
        if (decoder)
            return decoder;
    }
    return nullptr;
}

DisplayCodecEncoderCreateResult createSelectedDisplayEncoder(
    const IDisplayCodecBackendFactory& factory,
    const DisplayCodecSelectionRequest& request)
{
    DisplayCodecEncoderCreateResult result;

    DisplayCodecSelectionRequest selectionRequest =
        requestWithFactoryCapabilities(factory, request);
    selectionRequest.direction = DisplayCodecDirection::Encode;
    result.selection = selectDisplayCodec(selectionRequest);
    if (!result.selection.ok) {
        appendSelectionMessages(result.selection, result.messages);
        if (result.messages.empty())
            result.messages.push_back("display encoder selection failed");
        return result;
    }

    result.encoder = factory.createEncoder(result.selection.selected);
    if (result.encoder == nullptr) {
        result.messages.push_back(
            "display codec factory could not create selected encoder");
        return result;
    }

    result.ok = true;
    return result;
}

DisplayCodecDecoderCreateResult createSelectedDisplayDecoder(
    const IDisplayCodecBackendFactory& factory,
    const DisplayCodecSelectionRequest& request)
{
    DisplayCodecDecoderCreateResult result;

    DisplayCodecSelectionRequest selectionRequest =
        requestWithFactoryCapabilities(factory, request);
    selectionRequest.direction = DisplayCodecDirection::Decode;
    result.selection = selectDisplayCodec(selectionRequest);
    if (!result.selection.ok) {
        appendSelectionMessages(result.selection, result.messages);
        if (result.messages.empty())
            result.messages.push_back("display decoder selection failed");
        return result;
    }

    result.decoder = factory.createDecoder(result.selection.selected);
    if (result.decoder == nullptr) {
        result.messages.push_back(
            "display codec factory could not create selected decoder");
        return result;
    }

    result.ok = true;
    return result;
}

std::vector<DisplayCodecCapability> unavailableDefaultDisplayCodecCapabilities(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason)
{
    std::vector<DisplayCodecCapability> capabilities =
        defaultDisplayCodecCapabilities(platform);
    for (DisplayCodecCapability& capability : capabilities) {
        capability.available = false;
        capability.unavailableReason = unavailableReason;
    }
    return capabilities;
}

std::shared_ptr<IDisplayCodecBackendFactory>
createUnavailableDefaultDisplayCodecBackendFactory(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason)
{
    return std::make_shared<StaticDisplayCodecBackendFactory>(
        unavailableDefaultDisplayCodecCapabilities(platform, unavailableReason));
}

std::shared_ptr<IDisplayCodecBackendFactory>
createRawFrameDisplayCodecBackendFactory(DisplayPlatformFamily platform)
{
    return std::make_shared<RawFrameDisplayCodecBackendFactory>(platform);
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
