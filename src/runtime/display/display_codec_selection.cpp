#include "fusiondesk/runtime/display/display_codec_selection.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

template <typename T>
bool contains(const std::vector<T>& values, T value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
bool intersects(const std::vector<T>& lhs, const std::vector<T>& rhs)
{
    if (lhs.empty() || rhs.empty())
        return true;

    for (T value : lhs) {
        if (contains(rhs, value))
            return true;
    }
    return false;
}

std::string normalizedIdentifier(std::string value)
{
    for (char& ch : value) {
        const unsigned char raw = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::tolower(raw));
        if (ch == '-' || ch == ' ')
            ch = '_';
    }
    return value;
}

DisplayCodecCapability codecCapability(
    std::string adapterId,
    DisplayPlatformFamily platform,
    DisplayCodecBackendKind backend,
    DisplayCodecId codec,
    std::vector<DisplayCodecMemoryType> inputMemoryTypes,
    std::vector<DisplayCodecMemoryType> outputMemoryTypes,
    int priority)
{
    DisplayCodecCapability result;
    result.adapterId = std::move(adapterId);
    result.platform = platform;
    result.backend = backend;
    result.codec = codec;
    result.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    result.inputMemoryTypes = std::move(inputMemoryTypes);
    result.outputMemoryTypes = std::move(outputMemoryTypes);
    result.supportsEncode = true;
    result.supportsDecode = true;
    result.priority = priority;
    return result;
}

DisplayCodecCapability unavailableCodecCapability(
    std::string adapterId,
    DisplayPlatformFamily platform,
    DisplayCodecBackendKind backend,
    DisplayCodecId codec,
    std::vector<DisplayCodecMemoryType> inputMemoryTypes,
    std::vector<DisplayCodecMemoryType> outputMemoryTypes,
    int priority)
{
    DisplayCodecCapability result = codecCapability(std::move(adapterId),
                                                    platform,
                                                    backend,
                                                    codec,
                                                    std::move(inputMemoryTypes),
                                                    std::move(outputMemoryTypes),
                                                    priority);
    result.available = false;
    result.unavailableReason = "codec adapter not linked";
    return result;
}

DisplayCodecCapability rawFallback(DisplayPlatformFamily platform,
                                   std::string adapterId)
{
    DisplayCodecCapability result = codecCapability(
        std::move(adapterId),
        platform,
        DisplayCodecBackendKind::RawFrame,
        DisplayCodecId::RawBgra,
        {DisplayCodecMemoryType::CpuBuffer},
        {DisplayCodecMemoryType::CpuBuffer},
        5);
    result.fallback = true;
    result.hardwareAccelerated = false;
    result.zeroCopy = false;
    result.lowLatency = true;
    return result;
}

void reject(DisplayCodecSelectionResult& result,
            const DisplayCodecCapability& candidate,
            std::string reason)
{
    DisplayCodecRejection rejection;
    rejection.adapterId = candidate.adapterId;
    rejection.backend = candidate.backend;
    rejection.codec = candidate.codec;
    rejection.reason = std::move(reason);
    result.rejected.push_back(std::move(rejection));
}

void fail(DisplayCodecSelectionResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

int codecPreferenceScore(DisplayCodecId codec,
                         const std::vector<DisplayCodecId>& preference)
{
    if (preference.empty())
        return 0;

    for (std::size_t index = 0; index < preference.size(); ++index) {
        if (preference[index] == codec) {
            return static_cast<int>((preference.size() - index) * 30U);
        }
    }
    return 0;
}

int scoreCandidate(const DisplayCodecCapability& candidate,
                   const DisplayCodecSelectionRequest& request)
{
    int score = candidate.priority;
    score += codecPreferenceScore(candidate.codec, request.codecPreference);
    if (request.preferHardware && candidate.hardwareAccelerated)
        score += 20;
    if (request.preferZeroCopy && candidate.zeroCopy)
        score += 10;
    if (candidate.lowLatency)
        score += 5;
    if (!candidate.fallback)
        score += 2;
    return score;
}

bool supportsDirection(const DisplayCodecCapability& candidate,
                       DisplayCodecDirection direction)
{
    if (direction == DisplayCodecDirection::Encode)
        return candidate.supportsEncode;
    if (direction == DisplayCodecDirection::Decode)
        return candidate.supportsDecode;
    return false;
}

bool candidateUsable(const DisplayCodecCapability& candidate,
                     const DisplayCodecSelectionRequest& request,
                     DisplayCodecSelectionResult& result)
{
    if (!request.requestedAdapterId.empty() &&
        candidate.adapterId != request.requestedAdapterId) {
        reject(result, candidate, "codec adapter does not match requested adapter id");
        return false;
    }
    if (!candidate.available) {
        std::string reason = "codec adapter is unavailable";
        if (!candidate.unavailableReason.empty())
            reason += ": " + candidate.unavailableReason;
        reject(result, candidate, std::move(reason));
        return false;
    }
    if (candidate.platform != request.platform) {
        reject(result, candidate, "codec adapter platform does not match request");
        return false;
    }
    if (!supportsDirection(candidate, request.direction)) {
        reject(result, candidate, "codec adapter does not support requested direction");
        return false;
    }
    if (!request.codecPreference.empty() &&
        !contains(request.codecPreference, candidate.codec)) {
        reject(result, candidate, "codec adapter does not match requested codec preference");
        return false;
    }
    if (candidate.hardwareAccelerated && !request.allowHardware) {
        reject(result, candidate, "hardware codec adapters are disabled");
        return false;
    }
    if (!candidate.hardwareAccelerated && !request.allowSoftware) {
        reject(result, candidate, "software codec adapters are disabled");
        return false;
    }
    if (request.requireLowLatency && !candidate.lowLatency) {
        reject(result, candidate, "codec adapter does not support low-latency mode");
        return false;
    }
    if (!intersects(candidate.inputMemoryTypes, request.acceptedInputMemoryTypes)) {
        reject(result, candidate, "codec adapter cannot consume an accepted input memory type");
        return false;
    }
    if (!intersects(candidate.outputMemoryTypes, request.acceptedOutputMemoryTypes)) {
        reject(result, candidate, "codec adapter cannot produce an accepted output memory type");
        return false;
    }
    if (!intersects(candidate.pixelFormats, request.acceptedPixelFormats)) {
        reject(result, candidate, "codec adapter cannot handle an accepted pixel format");
        return false;
    }
    if (!candidate.architectures.empty() &&
        request.architecture != DisplayTargetArchitecture::Unknown &&
        !contains(candidate.architectures, request.architecture)) {
        reject(result, candidate, "codec adapter does not support requested architecture");
        return false;
    }
    if (!candidate.socProfiles.empty() &&
        request.socProfile != DisplayTargetSocProfile::Unknown &&
        request.socProfile != DisplayTargetSocProfile::Generic &&
        !contains(candidate.socProfiles, request.socProfile)) {
        reject(result, candidate, "codec adapter does not support requested SoC profile");
        return false;
    }
    if (request.width > 0 && candidate.maxWidth > 0 &&
        request.width > candidate.maxWidth) {
        reject(result, candidate, "codec adapter max width is smaller than request");
        return false;
    }
    if (request.height > 0 && candidate.maxHeight > 0 &&
        request.height > candidate.maxHeight) {
        reject(result, candidate, "codec adapter max height is smaller than request");
        return false;
    }
    return true;
}

} // namespace

const char* displayCodecIdName(DisplayCodecId codec)
{
    switch (codec) {
    case DisplayCodecId::Unknown:
        return "unknown";
    case DisplayCodecId::RawBgra:
        return "raw_bgra";
    case DisplayCodecId::H264:
        return "h264";
    case DisplayCodecId::H265:
        return "h265";
    case DisplayCodecId::Av1:
        return "av1";
    }
    return "unknown";
}

const char* displayCodecDirectionName(DisplayCodecDirection direction)
{
    switch (direction) {
    case DisplayCodecDirection::Unknown:
        return "unknown";
    case DisplayCodecDirection::Encode:
        return "encode";
    case DisplayCodecDirection::Decode:
        return "decode";
    }
    return "unknown";
}

const char* displayCodecBackendKindName(DisplayCodecBackendKind backend)
{
    switch (backend) {
    case DisplayCodecBackendKind::Unknown:
        return "unknown";
    case DisplayCodecBackendKind::RawFrame:
        return "raw_frame";
    case DisplayCodecBackendKind::Software:
        return "software";
    case DisplayCodecBackendKind::FFmpegSoftware:
        return "ffmpeg.software";
    case DisplayCodecBackendKind::WindowsMediaFoundation:
        return "windows.media_foundation";
    case DisplayCodecBackendKind::LinuxVaapi:
        return "linux.vaapi";
    case DisplayCodecBackendKind::LinuxV4L2M2M:
        return "linux.v4l2_m2m";
    case DisplayCodecBackendKind::MacOSVideoToolbox:
        return "macos.video_toolbox";
    case DisplayCodecBackendKind::AndroidMediaCodec:
        return "android.media_codec";
    case DisplayCodecBackendKind::HarmonySystemCodec:
        return "harmony.system_codec";
    case DisplayCodecBackendKind::RockchipMpp:
        return "rockchip.mpp";
    }
    return "unknown";
}

DisplayCodecId parseDisplayCodecId(const std::string& value)
{
    const std::string normalized = normalizedIdentifier(value);
    if (normalized.empty() || normalized == "auto" || normalized == "unknown")
        return DisplayCodecId::Unknown;
    if (normalized == "raw" || normalized == "raw_bgra" ||
        normalized == "bgra" || normalized == "raw_frame")
        return DisplayCodecId::RawBgra;
    if (normalized == "h264" || normalized == "avc")
        return DisplayCodecId::H264;
    if (normalized == "h265" || normalized == "hevc")
        return DisplayCodecId::H265;
    if (normalized == "av1")
        return DisplayCodecId::Av1;
    return DisplayCodecId::Unknown;
}

std::vector<DisplayCodecCapability>
defaultDisplayCodecCapabilities(DisplayPlatformFamily platform)
{
    std::vector<DisplayCodecCapability> result;

    if (platform == DisplayPlatformFamily::WindowsDesktop) {
        DisplayCodecCapability h264 = unavailableCodecCapability(
            "windows.media_foundation.h264",
            platform,
            DisplayCodecBackendKind::WindowsMediaFoundation,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::D3DTexture, DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::D3DTexture, DisplayCodecMemoryType::CpuBuffer},
            90);
        h264.hardwareAccelerated = true;
        h264.zeroCopy = true;
        h264.requiresHardwareDevice = true;
        h264.maxWidth = 8192;
        h264.maxHeight = 8192;
        result.push_back(h264);

        DisplayCodecCapability h265 = h264;
        h265.adapterId = "windows.media_foundation.h265";
        h265.codec = DisplayCodecId::H265;
        h265.priority = 85;
        result.push_back(h265);

        result.push_back(rawFallback(platform, "windows.raw_frame"));
        return result;
    }

    if (platform == DisplayPlatformFamily::LinuxX11 ||
        platform == DisplayPlatformFamily::LinuxWayland ||
        platform == DisplayPlatformFamily::LinuxEmbedded) {
        DisplayCodecCapability vaapi = unavailableCodecCapability(
            "linux.vaapi.h264",
            platform,
            DisplayCodecBackendKind::LinuxVaapi,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::DmaBuf, DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::DmaBuf, DisplayCodecMemoryType::CpuBuffer},
            85);
        vaapi.hardwareAccelerated = true;
        vaapi.zeroCopy = true;
        vaapi.requiresHardwareDevice = true;
        result.push_back(vaapi);

        DisplayCodecCapability software = unavailableCodecCapability(
            "linux.ffmpeg.software.h264",
            platform,
            DisplayCodecBackendKind::FFmpegSoftware,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::CpuBuffer},
            40);
        software.hardwareAccelerated = false;
        result.push_back(software);

        result.push_back(rawFallback(platform, "linux.raw_frame"));
        return result;
    }

    if (platform == DisplayPlatformFamily::RockchipLinux) {
        DisplayCodecCapability mpp = unavailableCodecCapability(
            "linux.rockchip.mpp.h264",
            platform,
            DisplayCodecBackendKind::RockchipMpp,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::DmaBuf, DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::DmaBuf, DisplayCodecMemoryType::CpuBuffer},
            95);
        mpp.hardwareAccelerated = true;
        mpp.zeroCopy = true;
        mpp.requiresHardwareDevice = true;
        mpp.architectures = {DisplayTargetArchitecture::Arm64};
        mpp.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                           DisplayTargetSocProfile::Rockchip3588};
        result.push_back(mpp);

        DisplayCodecCapability h265 = mpp;
        h265.adapterId = "linux.rockchip.mpp.h265";
        h265.codec = DisplayCodecId::H265;
        h265.priority = 90;
        result.push_back(h265);

        DisplayCodecCapability raw = rawFallback(platform, "linux.rockchip.raw_frame");
        raw.architectures = {DisplayTargetArchitecture::Arm64};
        raw.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                           DisplayTargetSocProfile::Rockchip3588};
        result.push_back(raw);
        return result;
    }

    if (platform == DisplayPlatformFamily::MacOS) {
        DisplayCodecCapability h264 = unavailableCodecCapability(
            "macos.video_toolbox.h264",
            platform,
            DisplayCodecBackendKind::MacOSVideoToolbox,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::CVPixelBuffer, DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::CVPixelBuffer, DisplayCodecMemoryType::CpuBuffer},
            90);
        h264.hardwareAccelerated = true;
        h264.zeroCopy = true;
        h264.requiresHardwareDevice = true;
        result.push_back(h264);

        DisplayCodecCapability h265 = h264;
        h265.adapterId = "macos.video_toolbox.h265";
        h265.codec = DisplayCodecId::H265;
        h265.priority = 85;
        result.push_back(h265);

        result.push_back(rawFallback(platform, "macos.raw_frame"));
        return result;
    }

    if (platform == DisplayPlatformFamily::AndroidClient ||
        platform == DisplayPlatformFamily::AndroidAgent ||
        platform == DisplayPlatformFamily::RockchipAndroid) {
        DisplayCodecCapability mediaCodec = unavailableCodecCapability(
            platform == DisplayPlatformFamily::RockchipAndroid
                ? "android.rockchip.media_codec.h264"
                : "android.media_codec.h264",
            platform,
            DisplayCodecBackendKind::AndroidMediaCodec,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::AndroidHardwareBuffer,
             DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::AndroidHardwareBuffer,
             DisplayCodecMemoryType::CpuBuffer},
            platform == DisplayPlatformFamily::RockchipAndroid ? 95 : 85);
        mediaCodec.hardwareAccelerated = true;
        mediaCodec.zeroCopy = true;
        mediaCodec.requiresHardwareDevice = true;
        if (platform == DisplayPlatformFamily::AndroidClient)
            mediaCodec.supportsEncode = false;
        if (platform == DisplayPlatformFamily::RockchipAndroid) {
            mediaCodec.architectures = {DisplayTargetArchitecture::Arm64};
            mediaCodec.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                                      DisplayTargetSocProfile::Rockchip3588};
        }
        result.push_back(mediaCodec);

        DisplayCodecCapability raw = rawFallback(
            platform,
            platform == DisplayPlatformFamily::RockchipAndroid
                ? "android.rockchip.raw_frame"
                : "android.raw_frame");
        if (platform == DisplayPlatformFamily::AndroidClient)
            raw.supportsEncode = false;
        if (platform == DisplayPlatformFamily::RockchipAndroid) {
            raw.architectures = {DisplayTargetArchitecture::Arm64};
            raw.socProfiles = {DisplayTargetSocProfile::Rockchip3568,
                               DisplayTargetSocProfile::Rockchip3588};
        }
        result.push_back(raw);
        return result;
    }

    if (platform == DisplayPlatformFamily::HarmonyOS ||
        platform == DisplayPlatformFamily::OpenHarmony) {
        DisplayCodecCapability system = unavailableCodecCapability(
            platform == DisplayPlatformFamily::HarmonyOS
                ? "harmonyos.system_codec.h264"
                : "openharmony.system_codec.h264",
            platform,
            DisplayCodecBackendKind::HarmonySystemCodec,
            DisplayCodecId::H264,
            {DisplayCodecMemoryType::CpuBuffer},
            {DisplayCodecMemoryType::CpuBuffer},
            60);
        system.hardwareAccelerated = true;
        system.requiresHardwareDevice = true;
        result.push_back(system);

        result.push_back(rawFallback(
            platform,
            platform == DisplayPlatformFamily::HarmonyOS
                ? "harmonyos.raw_frame"
                : "openharmony.raw_frame"));
        return result;
    }

    return result;
}

std::vector<DisplayCodecMemoryType>
defaultDisplayCodecAcceptedMemoryTypes(DisplayPlatformFamily platform)
{
    if (platform == DisplayPlatformFamily::WindowsDesktop)
        return {DisplayCodecMemoryType::D3DTexture,
                DisplayCodecMemoryType::CpuBuffer};
    if (platform == DisplayPlatformFamily::LinuxWayland ||
        platform == DisplayPlatformFamily::LinuxEmbedded ||
        platform == DisplayPlatformFamily::RockchipLinux)
        return {DisplayCodecMemoryType::DmaBuf,
                DisplayCodecMemoryType::CpuBuffer};
    if (platform == DisplayPlatformFamily::MacOS)
        return {DisplayCodecMemoryType::CVPixelBuffer,
                DisplayCodecMemoryType::CpuBuffer};
    if (platform == DisplayPlatformFamily::AndroidClient ||
        platform == DisplayPlatformFamily::AndroidAgent ||
        platform == DisplayPlatformFamily::RockchipAndroid)
        return {DisplayCodecMemoryType::AndroidHardwareBuffer,
                DisplayCodecMemoryType::CpuBuffer};
    return {DisplayCodecMemoryType::CpuBuffer};
}

DisplayCodecSelectionResult selectDisplayCodec(
    const DisplayCodecSelectionRequest& request)
{
    DisplayCodecSelectionResult result;

    if (request.platform == DisplayPlatformFamily::Unknown)
        fail(result, "display codec selection requires a platform");
    if (request.direction == DisplayCodecDirection::Unknown)
        fail(result, "display codec selection requires a direction");
    if (request.codecPreference.empty())
        fail(result, "display codec selection requires at least one codec preference");
    if (!request.allowHardware && !request.allowSoftware)
        fail(result, "display codec selection disables both hardware and software codecs");

    std::vector<DisplayCodecCapability> candidates = request.candidates;
    if (candidates.empty())
        candidates = defaultDisplayCodecCapabilities(request.platform);
    if (candidates.empty())
        fail(result, "display codec selection has no candidates");
    if (!result.messages.empty())
        return result;

    bool foundUsable = false;
    int bestScore = 0;
    DisplayCodecCapability bestCandidate;
    for (const DisplayCodecCapability& candidate : candidates) {
        if (!candidateUsable(candidate, request, result))
            continue;

        const int score = scoreCandidate(candidate, request);
        if (!foundUsable || score > bestScore) {
            foundUsable = true;
            bestScore = score;
            bestCandidate = candidate;
        }
    }

    if (!foundUsable) {
        fail(result, "display codec selection found no usable codec");
        return result;
    }

    result.ok = true;
    result.hasSelection = true;
    result.selected = bestCandidate;
    result.fallbackSelected = bestCandidate.fallback;
    result.score = bestScore;
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
