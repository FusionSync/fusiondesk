#include "fusiondesk/platform/windows/display/windows_gdi_display_capture.h"

#include "fusiondesk/platform/windows/display/windows_cursor_overlay.h"
#include "fusiondesk/runtime/display/display_capture_geometry.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <vector>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

#if defined(_WIN32)

struct MonitorRecord
{
    HMONITOR handle = nullptr;
    RECT rect = {};
    bool primary = false;
    std::string name;
};

std::string toUtf8(const wchar_t* value)
{
    if (value == nullptr || value[0] == L'\0')
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};

    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, &result[0], size, nullptr, nullptr);
    return result;
}

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
{
    auto* monitors = reinterpret_cast<std::vector<MonitorRecord>*>(userData);
    if (monitors == nullptr)
        return FALSE;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return TRUE;

    MonitorRecord record;
    record.handle = monitor;
    record.rect = info.rcMonitor;
    record.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    record.name = toUtf8(info.szDevice);
    monitors->push_back(record);
    return TRUE;
}

std::vector<MonitorRecord> enumerateMonitorRecords()
{
    std::vector<MonitorRecord> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));
    std::stable_sort(monitors.begin(),
                     monitors.end(),
                     [](const MonitorRecord& left, const MonitorRecord& right) {
                         return left.primary && !right.primary;
                     });
    return monitors;
}

modules::display::DisplayTopologySnapshot makeTopologySnapshot()
{
    modules::display::DisplayTopologySnapshot snapshot;
    snapshot.generation = monotonicNowUsec();
    std::uint32_t dpiX = 0;
    std::uint32_t dpiY = 0;
    HDC screenDc = GetDC(nullptr);
    if (screenDc != nullptr) {
        dpiX = static_cast<std::uint32_t>(GetDeviceCaps(screenDc, LOGPIXELSX));
        dpiY = static_cast<std::uint32_t>(GetDeviceCaps(screenDc, LOGPIXELSY));
        ReleaseDC(nullptr, screenDc);
    }

    const std::vector<MonitorRecord> monitors = enumerateMonitorRecords();
    snapshot.sources.reserve(monitors.size());
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const MonitorRecord& monitor = monitors[i];
        modules::display::DisplaySourceInfo source;
        source.sourceId = static_cast<std::uint32_t>(i);
        source.primary = monitor.primary;
        source.geometry.x = monitor.rect.left;
        source.geometry.y = monitor.rect.top;
        source.geometry.width = static_cast<std::uint32_t>(monitor.rect.right - monitor.rect.left);
        source.geometry.height = static_cast<std::uint32_t>(monitor.rect.bottom - monitor.rect.top);
        source.name = monitor.name;
        source.dpiX = dpiX;
        source.dpiY = dpiY;
        snapshot.sources.push_back(source);
    }
    return snapshot;
}

const MonitorRecord* selectMonitor(const std::vector<MonitorRecord>& monitors,
                                   std::uint32_t sourceId)
{
    if (monitors.empty())
        return nullptr;

    if (sourceId < monitors.size())
        return &monitors[sourceId];

    return nullptr;
}

#endif

} // namespace

modules::display::DisplayTopologySnapshot WindowsGdiDisplaySourceCatalog::snapshot() const
{
#if defined(_WIN32)
    return makeTopologySnapshot();
#else
    return {};
#endif
}

bool WindowsGdiDisplayCapture::open(const modules::display::DisplayCaptureOpenOptions& options)
{
    options_ = options;
    topology_ = WindowsGdiDisplaySourceCatalog().snapshot();
#if defined(_WIN32)
    if (options_.sourceType !=
        modules::display::DisplayCaptureSourceType::Monitor) {
        opened_ = false;
        recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                           0,
                           "GDI capture only supports monitor sources",
                           false);
        return false;
    }
    if (topology_.sources.empty()) {
        opened_ = false;
        recordCaptureError(modules::display::DisplayCaptureStatusCode::SourceNotFound,
                           0,
                           "GDI capture found no display sources",
                           true);
        return false;
    }
    if (options_.sourceId >= topology_.sources.size()) {
        opened_ = false;
        recordCaptureError(modules::display::DisplayCaptureStatusCode::SourceNotFound,
                           0,
                           "GDI capture source id was not found",
                           true);
        return false;
    }
    recordOk();
    opened_ = true;
    return true;
#else
    opened_ = false;
    recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                       0,
                       "GDI capture is only available on Windows",
                       false);
    return false;
#endif
}

void WindowsGdiDisplayCapture::close()
{
    opened_ = false;
}

modules::display::CapturedFrame WindowsGdiDisplayCapture::captureNextFrame(bool keyFrame)
{
    modules::display::CapturedFrame frame;
    frame.frameId = nextFrameId_++;
    frame.sourceId = options_.sourceId;
    frame.keyFrame = keyFrame;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.monotonicTimestampUsec = monotonicNowUsec();

#if defined(_WIN32)
    if (!opened_)
        open(options_);

    const std::vector<MonitorRecord> monitors = enumerateMonitorRecords();
    const MonitorRecord* source = selectMonitor(monitors, options_.sourceId);
    if (source == nullptr) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::SourceNotFound,
                           0,
                           "GDI capture source was not found",
                           true);
        return frame;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::AccessDenied,
                           static_cast<int>(GetLastError()),
                           "GDI capture could not access the desktop DC",
                           true);
        return frame;
    }

    const int sourceWidth = source->rect.right - source->rect.left;
    const int sourceHeight = source->rect.bottom - source->rect.top;
    const runtime::display::DisplayCaptureOutputSize output =
        runtime::display::resolveDisplayCaptureOutputSize(
            static_cast<std::uint32_t>(sourceWidth),
            static_cast<std::uint32_t>(sourceHeight),
            options_);
    if (output.width == 0 || output.height == 0) {
        ReleaseDC(nullptr, screenDc);
        recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                           0,
                           "GDI capture resolved an invalid output size",
                           true);
        return frame;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    const int outputWidth = static_cast<int>(output.width);
    const int outputHeight = static_cast<int>(output.height);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, outputWidth, outputHeight);
    if (memoryDc == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        if (memoryDc != nullptr)
            DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        recordCaptureError(modules::display::DisplayCaptureStatusCode::SystemCallFailed,
                           static_cast<int>(GetLastError()),
                           "GDI capture could not allocate a memory DC or bitmap",
                           true);
        return frame;
    }

    HGDIOBJ previous = SelectObject(memoryDc, bitmap);
    BOOL copied = FALSE;
    if (output.width == static_cast<std::uint32_t>(sourceWidth) &&
        output.height == static_cast<std::uint32_t>(sourceHeight)) {
        copied = BitBlt(memoryDc,
                        0,
                        0,
                        outputWidth,
                        outputHeight,
                        screenDc,
                        source->rect.left,
                        source->rect.top,
                        SRCCOPY | CAPTUREBLT);
    } else {
        SetStretchBltMode(memoryDc, HALFTONE);
        SetBrushOrgEx(memoryDc, 0, 0, nullptr);
        copied = StretchBlt(memoryDc,
                            0,
                            0,
                            outputWidth,
                            outputHeight,
                            screenDc,
                            source->rect.left,
                            source->rect.top,
                            sourceWidth,
                            sourceHeight,
                            SRCCOPY | CAPTUREBLT);
    }
    if (copied == FALSE) {
        const int nativeCode = static_cast<int>(GetLastError());
        SelectObject(memoryDc, previous);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        recordCaptureError(modules::display::DisplayCaptureStatusCode::SystemCallFailed,
                           nativeCode,
                           "GDI BitBlt or StretchBlt failed",
                           true);
        return frame;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = outputWidth;
    info.bmiHeader.biHeight = -outputHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    frame.width = output.width;
    frame.height = output.height;
    frame.strideBytes = frame.width * 4;
    frame.pixels.resize(static_cast<std::size_t>(frame.strideBytes) * frame.height);
    const int copiedLines = GetDIBits(memoryDc,
                                      bitmap,
                                      0,
                                      static_cast<UINT>(output.height),
                                      frame.pixels.data(),
                                      &info,
                                      DIB_RGB_COLORS);
    SelectObject(memoryDc, previous);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (copiedLines != outputHeight) {
        frame.pixels.clear();
        frame.width = 0;
        frame.height = 0;
        frame.strideBytes = 0;
        recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                           static_cast<int>(GetLastError()),
                           "GDI GetDIBits did not return the expected frame lines",
                           true);
    } else {
        modules::display::DisplaySourceGeometry sourceGeometry;
        sourceGeometry.x = source->rect.left;
        sourceGeometry.y = source->rect.top;
        sourceGeometry.width = static_cast<std::uint32_t>(sourceWidth);
        sourceGeometry.height = static_cast<std::uint32_t>(sourceHeight);
        overlayWindowsCursorOnBgraFrame(frame, sourceGeometry, options_);
        recordOk();
    }
#else
    recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                       0,
                       "GDI capture is only available on Windows",
                       false);
#endif

    return frame;
}

bool WindowsGdiDisplayCapture::isOpen() const
{
    return opened_;
}

int WindowsGdiDisplayCapture::captureErrors() const
{
    return captureErrors_;
}

std::string WindowsGdiDisplayCapture::backendId() const
{
    return "windows.gdi";
}

modules::display::DisplayCaptureStatus WindowsGdiDisplayCapture::lastStatus() const
{
    return lastStatus_;
}

modules::display::DisplayTopologySnapshot WindowsGdiDisplayCapture::topologySnapshot() const
{
    return topology_;
}

void WindowsGdiDisplayCapture::recordOk()
{
    lastStatus_ = {};
}

void WindowsGdiDisplayCapture::recordCaptureError(
    modules::display::DisplayCaptureStatusCode code,
    int nativeCode,
    const char* message,
    bool recoverable)
{
    ++captureErrors_;
    lastStatus_.code = code;
    lastStatus_.nativeCode = nativeCode;
    lastStatus_.recoverable = recoverable;
    lastStatus_.message = message == nullptr ? std::string() : std::string(message);
}

std::vector<fusiondesk::runtime::display::DisplayCaptureBackendCapability>
WindowsGdiDisplayCaptureFactory::capabilities() const
{
    namespace display_runtime = fusiondesk::runtime::display;

    display_runtime::DisplayCaptureBackendCapability capability;
    capability.adapterId = "windows.gdi";
    capability.platform = display_runtime::DisplayPlatformFamily::WindowsDesktop;
    capability.backend = display_runtime::DisplayCaptureBackendKind::WindowsGdi;
    capability.sourceTypes = {display_runtime::DisplayCaptureSourceType::Monitor};
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.memoryTypes = {display_runtime::DisplayCaptureMemoryType::CpuBuffer};
    capability.diagnostics = {
        display_runtime::DisplayCaptureDiagnosticKind::AccessDenied,
        display_runtime::DisplayCaptureDiagnosticKind::SourceNotFound,
        display_runtime::DisplayCaptureDiagnosticKind::FrameTimeout,
    };
    capability.available = true;
    capability.fallback = true;
    capability.zeroCopy = false;
    capability.requiresUserConsent = false;
    capability.supportsServiceSession = true;
    capability.agentCaptureSupported = true;
    capability.supportsHotplugNotifications = false;
    capability.priority = 10;
    return {capability};
}

std::shared_ptr<modules::display::IDisplayCapture>
WindowsGdiDisplayCaptureFactory::createCapture(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    namespace display_runtime = fusiondesk::runtime::display;

    if (selected.backend != display_runtime::DisplayCaptureBackendKind::WindowsGdi)
        return nullptr;
    if (!selected.adapterId.empty() && selected.adapterId != "windows.gdi")
        return nullptr;

    return std::make_shared<WindowsGdiDisplayCapture>();
}

std::shared_ptr<modules::display::IDisplaySourceCatalog>
WindowsGdiDisplayCaptureFactory::createSourceCatalog(
    const fusiondesk::runtime::display::DisplayCaptureBackendCapability& selected) const
{
    namespace display_runtime = fusiondesk::runtime::display;

    if (selected.backend != display_runtime::DisplayCaptureBackendKind::WindowsGdi)
        return nullptr;
    if (!selected.adapterId.empty() && selected.adapterId != "windows.gdi")
        return nullptr;

    return std::make_shared<WindowsGdiDisplaySourceCatalog>();
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
