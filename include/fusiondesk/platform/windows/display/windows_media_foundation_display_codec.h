#ifndef FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_MEDIA_FOUNDATION_DISPLAY_CODEC_H
#define FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_MEDIA_FOUNDATION_DISPLAY_CODEC_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/runtime/display/display_codec_backend_factory.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

struct WindowsMediaFoundationCodecProbeResult
{
    bool rolloutEnabled = false;
    bool mediaFoundationStarted = false;
    bool h264EncoderFound = false;
    bool h264DecoderFound = false;
    long nativeCode = 0;
    std::string message;
};

struct WindowsMediaFoundationH264AdapterPreflightResult
{
    bool rolloutEnabled = false;
    bool mediaFoundationStarted = false;
    bool h264EncoderMftCreated = false;
    bool h264DecoderMftCreated = false;
    bool encoderOutputTypeAccepted = false;
    bool encoderBgraInputAccepted = false;
    bool encoderNv12InputAccepted = false;
    bool decoderInputTypeAccepted = false;
    bool decoderOutputTypeAccepted = false;
    bool decoderNv12OutputAccepted = false;
    bool decoderBgraOutputAccepted = false;
    bool bgraToNv12ConversionOk = false;
    std::uint64_t bgraToNv12Bytes = 0;
    std::uint64_t bgraToNv12YPlaneBytes = 0;
    std::uint64_t bgraToNv12UvPlaneBytes = 0;
    long nativeCode = 0;
    std::string message;
};

struct WindowsMediaFoundationH264EncodePreflightResult
{
    bool rolloutEnabled = false;
    bool mediaFoundationStarted = false;
    bool bgraToNv12ConversionOk = false;
    bool h264EncoderMftCreated = false;
    bool encoderOutputTypeAccepted = false;
    bool encoderNv12InputAccepted = false;
    bool encoderBgraInputAccepted = false;
    bool streamingStarted = false;
    bool inputSampleAccepted = false;
    bool outputSampleProduced = false;
    bool fdsfPayloadEncoded = false;
    std::uint64_t bgraToNv12Bytes = 0;
    std::uint64_t bitstreamBytes = 0;
    std::uint64_t fdsfPayloadBytes = 0;
    long nativeCode = 0;
    std::string message;
};

struct WindowsMediaFoundationDisplayCodecPolicy
{
    bool rolloutEnabled = false;
    bool selectable = false;
    bool pFrameEnabled = false;
    std::string selectionMode = "default";
};

WindowsMediaFoundationCodecProbeResult
probeWindowsMediaFoundationH264Codec();

WindowsMediaFoundationH264AdapterPreflightResult
preflightWindowsMediaFoundationH264Adapter(std::uint32_t width = 1280,
                                          std::uint32_t height = 720);

WindowsMediaFoundationH264EncodePreflightResult
preflightWindowsMediaFoundationH264Encode(std::uint32_t width = 640,
                                         std::uint32_t height = 360);

class WindowsMediaFoundationDisplayCodecBackendFactory final
    : public runtime::display::IDisplayCodecBackendFactory
{
public:
    WindowsMediaFoundationDisplayCodecBackendFactory();
    explicit WindowsMediaFoundationDisplayCodecBackendFactory(
        WindowsMediaFoundationDisplayCodecPolicy policy);

    std::vector<runtime::display::DisplayCodecCapability>
    capabilities() const override;

    std::shared_ptr<modules::display::IVideoEncoder> createEncoder(
        const runtime::display::DisplayCodecCapability& selected) const override;
    std::shared_ptr<modules::display::IVideoDecoder> createDecoder(
        const runtime::display::DisplayCodecCapability& selected) const override;

private:
    WindowsMediaFoundationDisplayCodecPolicy policy_;
};

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_DISPLAY_WINDOWS_MEDIA_FOUNDATION_DISPLAY_CODEC_H
