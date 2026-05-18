#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_BACKEND_FACTORY_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_BACKEND_FACTORY_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_interfaces.h"
#include "fusiondesk/runtime/display/display_codec_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

class IDisplayCodecBackendFactory
{
public:
    virtual ~IDisplayCodecBackendFactory() = default;

    virtual std::vector<DisplayCodecCapability> capabilities() const = 0;
    virtual std::shared_ptr<modules::display::IVideoEncoder> createEncoder(
        const DisplayCodecCapability& selected) const = 0;
    virtual std::shared_ptr<modules::display::IVideoDecoder> createDecoder(
        const DisplayCodecCapability& selected) const = 0;
};

class StaticDisplayCodecBackendFactory final : public IDisplayCodecBackendFactory
{
public:
    explicit StaticDisplayCodecBackendFactory(
        std::vector<DisplayCodecCapability> capabilities);

    std::vector<DisplayCodecCapability> capabilities() const override;

    std::shared_ptr<modules::display::IVideoEncoder> createEncoder(
        const DisplayCodecCapability& selected) const override;
    std::shared_ptr<modules::display::IVideoDecoder> createDecoder(
        const DisplayCodecCapability& selected) const override;

private:
    std::vector<DisplayCodecCapability> capabilities_;
};

class RawFrameDisplayCodecBackendFactory final
    : public IDisplayCodecBackendFactory
{
public:
    explicit RawFrameDisplayCodecBackendFactory(DisplayPlatformFamily platform);

    std::vector<DisplayCodecCapability> capabilities() const override;

    std::shared_ptr<modules::display::IVideoEncoder> createEncoder(
        const DisplayCodecCapability& selected) const override;
    std::shared_ptr<modules::display::IVideoDecoder> createDecoder(
        const DisplayCodecCapability& selected) const override;

private:
    DisplayPlatformFamily platform_ = DisplayPlatformFamily::Unknown;
};

class DisplayCodecBackendFactoryRegistry final
    : public IDisplayCodecBackendFactory
{
public:
    void addFactory(std::shared_ptr<IDisplayCodecBackendFactory> factory);
    std::size_t factoryCount() const;

    std::vector<DisplayCodecCapability> capabilities() const override;

    std::shared_ptr<modules::display::IVideoEncoder> createEncoder(
        const DisplayCodecCapability& selected) const override;
    std::shared_ptr<modules::display::IVideoDecoder> createDecoder(
        const DisplayCodecCapability& selected) const override;

private:
    std::vector<std::shared_ptr<IDisplayCodecBackendFactory>> factories_;
};

struct DisplayCodecEncoderCreateResult
{
    bool ok = false;
    DisplayCodecSelectionResult selection;
    std::shared_ptr<modules::display::IVideoEncoder> encoder;
    std::vector<std::string> messages;
};

struct DisplayCodecDecoderCreateResult
{
    bool ok = false;
    DisplayCodecSelectionResult selection;
    std::shared_ptr<modules::display::IVideoDecoder> decoder;
    std::vector<std::string> messages;
};

DisplayCodecEncoderCreateResult createSelectedDisplayEncoder(
    const IDisplayCodecBackendFactory& factory,
    const DisplayCodecSelectionRequest& request);

DisplayCodecDecoderCreateResult createSelectedDisplayDecoder(
    const IDisplayCodecBackendFactory& factory,
    const DisplayCodecSelectionRequest& request);

std::vector<DisplayCodecCapability> unavailableDefaultDisplayCodecCapabilities(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason);

std::shared_ptr<IDisplayCodecBackendFactory>
createUnavailableDefaultDisplayCodecBackendFactory(
    DisplayPlatformFamily platform,
    const std::string& unavailableReason);

std::shared_ptr<IDisplayCodecBackendFactory>
createRawFrameDisplayCodecBackendFactory(DisplayPlatformFamily platform);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_BACKEND_FACTORY_H
