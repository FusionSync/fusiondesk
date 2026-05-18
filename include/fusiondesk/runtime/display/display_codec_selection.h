#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_SELECTION_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_SELECTION_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_types.h"
#include "fusiondesk/runtime/display/display_capture_backend_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

enum class DisplayCodecId : std::uint16_t
{
    Unknown = 0,
    RawBgra = 1,
    H264 = 2,
    H265 = 3,
    Av1 = 4
};

enum class DisplayCodecDirection : std::uint16_t
{
    Unknown = 0,
    Encode = 1,
    Decode = 2
};

enum class DisplayCodecBackendKind : std::uint16_t
{
    Unknown = 0,
    RawFrame = 1,
    Software = 2,
    FFmpegSoftware = 3,
    WindowsMediaFoundation = 4,
    LinuxVaapi = 5,
    LinuxV4L2M2M = 6,
    MacOSVideoToolbox = 7,
    AndroidMediaCodec = 8,
    HarmonySystemCodec = 9,
    RockchipMpp = 10
};

using DisplayCodecMemoryType = DisplayCaptureMemoryType;

struct DisplayCodecCapability
{
    std::string adapterId;
    DisplayPlatformFamily platform = DisplayPlatformFamily::Unknown;
    DisplayCodecBackendKind backend = DisplayCodecBackendKind::Unknown;
    DisplayCodecId codec = DisplayCodecId::Unknown;
    std::vector<modules::display::DisplayPixelFormat> pixelFormats;
    std::vector<DisplayCodecMemoryType> inputMemoryTypes;
    std::vector<DisplayCodecMemoryType> outputMemoryTypes;
    std::vector<DisplayTargetArchitecture> architectures;
    std::vector<DisplayTargetSocProfile> socProfiles;
    bool supportsEncode = false;
    bool supportsDecode = false;
    bool available = true;
    std::string unavailableReason;
    bool fallback = false;
    bool hardwareAccelerated = false;
    bool zeroCopy = false;
    bool lowLatency = true;
    bool requiresHardwareDevice = false;
    std::uint32_t maxWidth = 0;
    std::uint32_t maxHeight = 0;
    int priority = 0;
};

struct DisplayCodecSelectionRequest
{
    DisplayPlatformFamily platform = DisplayPlatformFamily::Unknown;
    DisplayCodecDirection direction = DisplayCodecDirection::Unknown;
    std::vector<DisplayCodecId> codecPreference = {DisplayCodecId::RawBgra};
    std::vector<modules::display::DisplayPixelFormat> acceptedPixelFormats = {
        modules::display::DisplayPixelFormat::Bgra32};
    std::vector<DisplayCodecMemoryType> acceptedInputMemoryTypes = {
        DisplayCodecMemoryType::CpuBuffer};
    std::vector<DisplayCodecMemoryType> acceptedOutputMemoryTypes = {
        DisplayCodecMemoryType::CpuBuffer};
    DisplayTargetArchitecture architecture = DisplayTargetArchitecture::Unknown;
    DisplayTargetSocProfile socProfile = DisplayTargetSocProfile::Unknown;
    std::string requestedAdapterId;
    bool allowHardware = true;
    bool allowSoftware = true;
    bool preferHardware = true;
    bool preferZeroCopy = true;
    bool requireLowLatency = true;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<DisplayCodecCapability> candidates;
};

struct DisplayCodecRejection
{
    std::string adapterId;
    DisplayCodecBackendKind backend = DisplayCodecBackendKind::Unknown;
    DisplayCodecId codec = DisplayCodecId::Unknown;
    std::string reason;
};

struct DisplayCodecSelectionResult
{
    bool ok = false;
    bool hasSelection = false;
    DisplayCodecCapability selected;
    bool fallbackSelected = false;
    int score = 0;
    std::vector<DisplayCodecRejection> rejected;
    std::vector<std::string> messages;
};

const char* displayCodecIdName(DisplayCodecId codec);
const char* displayCodecDirectionName(DisplayCodecDirection direction);
const char* displayCodecBackendKindName(DisplayCodecBackendKind backend);
DisplayCodecId parseDisplayCodecId(const std::string& value);

std::vector<DisplayCodecCapability>
defaultDisplayCodecCapabilities(DisplayPlatformFamily platform);

std::vector<DisplayCodecMemoryType>
defaultDisplayCodecAcceptedMemoryTypes(DisplayPlatformFamily platform);

DisplayCodecSelectionResult selectDisplayCodec(
    const DisplayCodecSelectionRequest& request);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CODEC_SELECTION_H
