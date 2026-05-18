#ifndef FUSIONDESK_MODULES_DISPLAY_DISPLAY_TYPES_H
#define FUSIONDESK_MODULES_DISPLAY_DISPLAY_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace modules {
namespace display {

enum class DisplayPixelFormat : std::uint16_t
{
    Unknown = 0,
    Bgra32 = 1,
    Rgba32 = 2,
    Rgb24 = 3,
    Gray8 = 4
};

enum class DisplayEncodedVideoCodec : std::uint16_t
{
    Unknown = 0,
    H264 = 2,
    H265 = 3,
    Av1 = 4
};

enum class DisplayEncodedVideoBitstreamFormat : std::uint16_t
{
    Unknown = 0,
    AnnexB = 1,
    Avcc = 2
};

enum class DisplayScaleMode : std::uint16_t
{
    Source = 0,
    Stretch = 1,
    Fit = 2
};

enum class DisplayCaptureSourceType : std::uint16_t
{
    Unknown = 0,
    Monitor = 1,
    Window = 2,
    VirtualDisplay = 3,
    MobileProjection = 4
};

enum class DisplayCaptureStatusCode : std::uint16_t
{
    Ok = 0,
    Unknown = 1,
    NotOpen = 2,
    AccessDenied = 3,
    PermissionRevoked = 4,
    ProtectedContent = 5,
    SourceNotFound = 6,
    SourceHotplug = 7,
    GeometryOrFormatChanged = 8,
    DeviceLost = 9,
    SessionModeUnsupported = 10,
    FrameTimeout = 11,
    InvalidFrame = 12,
    SystemCallFailed = 13,
    Unsupported = 14
};

enum class DisplayDecodeStatus : std::uint16_t
{
    Ok = 0,
    NeedsMoreInput = 1,
    Failed = 2
};

struct DisplayCodecRuntimeInfo
{
    bool selected = false;
    std::string adapterId;
    std::string codec;
    std::string backend;
    bool fallback = false;
    bool hardwareAccelerated = false;
    bool zeroCopy = false;
    bool lowLatency = true;
    bool deltaFrames = false;
    std::string selectionMode;
    std::string fallbackReason;
    std::vector<std::string> messages;
};

inline const char* displayDecodeStatusName(DisplayDecodeStatus status)
{
    switch (status) {
    case DisplayDecodeStatus::Ok:
        return "Ok";
    case DisplayDecodeStatus::NeedsMoreInput:
        return "NeedsMoreInput";
    case DisplayDecodeStatus::Failed:
        return "Failed";
    }
    return "Failed";
}

inline const char* displayCaptureStatusCodeName(DisplayCaptureStatusCode code)
{
    switch (code) {
    case DisplayCaptureStatusCode::Ok:
        return "Ok";
    case DisplayCaptureStatusCode::Unknown:
        return "Unknown";
    case DisplayCaptureStatusCode::NotOpen:
        return "NotOpen";
    case DisplayCaptureStatusCode::AccessDenied:
        return "AccessDenied";
    case DisplayCaptureStatusCode::PermissionRevoked:
        return "PermissionRevoked";
    case DisplayCaptureStatusCode::ProtectedContent:
        return "ProtectedContent";
    case DisplayCaptureStatusCode::SourceNotFound:
        return "SourceNotFound";
    case DisplayCaptureStatusCode::SourceHotplug:
        return "SourceHotplug";
    case DisplayCaptureStatusCode::GeometryOrFormatChanged:
        return "GeometryOrFormatChanged";
    case DisplayCaptureStatusCode::DeviceLost:
        return "DeviceLost";
    case DisplayCaptureStatusCode::SessionModeUnsupported:
        return "SessionModeUnsupported";
    case DisplayCaptureStatusCode::FrameTimeout:
        return "FrameTimeout";
    case DisplayCaptureStatusCode::InvalidFrame:
        return "InvalidFrame";
    case DisplayCaptureStatusCode::SystemCallFailed:
        return "SystemCallFailed";
    case DisplayCaptureStatusCode::Unsupported:
        return "Unsupported";
    }
    return "Unknown";
}

struct DisplayCaptureStatus
{
    DisplayCaptureStatusCode code = DisplayCaptureStatusCode::Ok;
    int nativeCode = 0;
    bool recoverable = true;
    std::string message;
};

struct DisplaySourceGeometry
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct DisplaySourceInfo
{
    std::uint32_t sourceId = 0;
    DisplayCaptureSourceType sourceType = DisplayCaptureSourceType::Monitor;
    bool primary = false;
    DisplaySourceGeometry geometry;
    std::uint32_t dpiX = 0;
    std::uint32_t dpiY = 0;
    std::string name;
    std::uint64_t nativeSourceHandle = 0;
};

struct DisplayTopologySnapshot
{
    std::uint64_t generation = 0;
    std::vector<DisplaySourceInfo> sources;
};

struct CapturedFrame
{
    std::uint64_t frameId = 0;
    std::uint32_t sourceId = 0;
    bool keyFrame = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    DisplayPixelFormat pixelFormat = DisplayPixelFormat::Unknown;
    std::uint64_t monotonicTimestampUsec = 0;
    protocol::ByteBuffer pixels;
};

struct DisplayCaptureOpenOptions
{
    std::uint32_t sourceId = 0;
    DisplayCaptureSourceType sourceType = DisplayCaptureSourceType::Monitor;
    std::uint64_t nativeSourceHandle = 0;
    std::uint32_t targetWidth = 0;
    std::uint32_t targetHeight = 0;
    DisplayPixelFormat preferredPixelFormat = DisplayPixelFormat::Bgra32;
    DisplayScaleMode scaleMode = DisplayScaleMode::Fit;
    bool includeCursor = true;
};

enum class DisplayRenderSurfaceKind : std::uint16_t
{
    None = 0,
    OpaqueNativeHandle = 1,
    SoftwareBuffer = 2,
    QtImageSink = 3
};

struct DisplayRenderSurface
{
    DisplayRenderSurfaceKind kind = DisplayRenderSurfaceKind::None;
    void* nativeHandle = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DisplayPixelFormat preferredPixelFormat = DisplayPixelFormat::Bgra32;
};

struct EncodedFrame
{
    std::uint64_t frameId = 0;
    bool keyFrame = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    DisplayPixelFormat pixelFormat = DisplayPixelFormat::Unknown;
    std::uint64_t monotonicTimestampUsec = 0;
    protocol::ByteBuffer payload;
};

struct DecodedFrame
{
    DisplayDecodeStatus decodeStatus = DisplayDecodeStatus::Ok;
    std::uint64_t frameId = 0;
    bool keyFrame = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    DisplayPixelFormat pixelFormat = DisplayPixelFormat::Unknown;
    std::uint64_t monotonicTimestampUsec = 0;
    protocol::ByteBuffer pixels;
};

} // namespace display
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_DISPLAY_DISPLAY_TYPES_H
