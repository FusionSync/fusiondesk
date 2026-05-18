#include "fusiondesk/platform/windows/display/windows_graphics_capture.h"

#include "fusiondesk/runtime/display/display_capture_geometry.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <roerrorapi.h>
#include <windows.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/base.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
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

template <typename T>
bool succeeded(T value)
{
    return SUCCEEDED(static_cast<HRESULT>(value));
}

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

struct MonitorRecord
{
    HMONITOR handle = nullptr;
    RECT rect = {};
    bool primary = false;
    std::string name;
};

struct WindowRecord
{
    HWND handle = nullptr;
    RECT rect = {};
    std::string name;
    std::uint32_t dpi = 0;
};

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

std::string windowTitleUtf8(HWND window)
{
    wchar_t title[256] = {};
    GetWindowTextW(window, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    std::string name = toUtf8(title);
    if (!name.empty())
        return name;

    wchar_t className[128] = {};
    GetClassNameW(window,
                  className,
                  static_cast<int>(sizeof(className) / sizeof(className[0])));
    return toUtf8(className);
}

bool makeWindowRecord(HWND window, WindowRecord& record)
{
    if (window == nullptr || !IsWindow(window))
        return false;
    if (window == GetShellWindow())
        return false;
    if (!IsWindowVisible(window) || IsIconic(window))
        return false;
    if (GetWindow(window, GW_OWNER) != nullptr)
        return false;

    RECT rect = {};
    if (!GetWindowRect(window, &rect))
        return false;
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return false;

    const std::string name = windowTitleUtf8(window);
    if (name.empty())
        return false;

    record.handle = window;
    record.rect = rect;
    record.name = name;
    record.dpi = static_cast<std::uint32_t>(GetDpiForWindow(window));
    return true;
}

BOOL CALLBACK collectWindow(HWND window, LPARAM userData)
{
    auto* windows = reinterpret_cast<std::vector<WindowRecord>*>(userData);
    if (windows == nullptr)
        return FALSE;

    WindowRecord record;
    if (makeWindowRecord(window, record))
        windows->push_back(record);
    return TRUE;
}

std::vector<WindowRecord> enumerateWindowRecords()
{
    std::vector<WindowRecord> windows;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&windows));
    return windows;
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

const WindowRecord* selectWindow(const std::vector<WindowRecord>& windows,
                                 std::uint32_t sourceId)
{
    if (windows.empty())
        return nullptr;
    if (sourceId < windows.size())
        return &windows[sourceId];
    return nullptr;
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
        source.sourceType = modules::display::DisplayCaptureSourceType::Monitor;
        source.primary = monitor.primary;
        source.geometry.x = monitor.rect.left;
        source.geometry.y = monitor.rect.top;
        source.geometry.width = static_cast<std::uint32_t>(
            monitor.rect.right - monitor.rect.left);
        source.geometry.height = static_cast<std::uint32_t>(
            monitor.rect.bottom - monitor.rect.top);
        source.name = monitor.name;
        source.nativeSourceHandle =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                monitor.handle));
        source.dpiX = dpiX;
        source.dpiY = dpiY;
        snapshot.sources.push_back(source);
    }

    const std::vector<WindowRecord> windows = enumerateWindowRecords();
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const WindowRecord& window = windows[i];
        modules::display::DisplaySourceInfo source;
        source.sourceId = static_cast<std::uint32_t>(i);
        source.sourceType = modules::display::DisplayCaptureSourceType::Window;
        source.primary = false;
        source.geometry.x = window.rect.left;
        source.geometry.y = window.rect.top;
        source.geometry.width = static_cast<std::uint32_t>(
            window.rect.right - window.rect.left);
        source.geometry.height = static_cast<std::uint32_t>(
            window.rect.bottom - window.rect.top);
        source.name = window.name;
        source.nativeSourceHandle =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                window.handle));
        source.dpiX = window.dpi;
        source.dpiY = window.dpi;
        snapshot.sources.push_back(source);
    }
    return snapshot;
}

class ThreadComApartment
{
public:
    ThreadComApartment()
    {
        const HRESULT initialized =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (initialized == S_OK || initialized == S_FALSE) {
            hr = initialized;
            shouldUninitialize = true;
            return;
        }
        if (initialized == RPC_E_CHANGED_MODE) {
            hr = S_FALSE;
            shouldUninitialize = false;
            return;
        }
        hr = initialized;
    }

    ~ThreadComApartment()
    {
        if (shouldUninitialize)
            CoUninitialize();
    }

    HRESULT hr = S_OK;
    bool shouldUninitialize = false;
};

HRESULT ensureApartmentForCapture()
{
    thread_local ThreadComApartment apartment;
    return apartment.hr;
}

HRESULT createDefaultD3dDevice(Microsoft::WRL::ComPtr<ID3D11Device>& device,
                               Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context)
{
    constexpr UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL selectedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return D3D11CreateDevice(nullptr,
                             D3D_DRIVER_TYPE_HARDWARE,
                             nullptr,
                             deviceFlags,
                             featureLevels,
                             static_cast<UINT>(
                                 sizeof(featureLevels) /
                                 sizeof(featureLevels[0])),
                             D3D11_SDK_VERSION,
                             &device,
                             &selectedFeatureLevel,
                             &context);
}

modules::display::DisplayCaptureStatusCode statusCodeFromNativeCode(int nativeCode)
{
    const HRESULT hr = static_cast<HRESULT>(nativeCode);
    if (hr == E_ACCESSDENIED)
        return modules::display::DisplayCaptureStatusCode::AccessDenied;
    if (hr == E_INVALIDARG ||
        hr == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE))
        return modules::display::DisplayCaptureStatusCode::SourceNotFound;
    if (hr == RO_E_CLOSED)
        return modules::display::DisplayCaptureStatusCode::SourceHotplug;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        return modules::display::DisplayCaptureStatusCode::DeviceLost;
    if (hr == DXGI_ERROR_UNSUPPORTED || hr == REGDB_E_CLASSNOTREG)
        return modules::display::DisplayCaptureStatusCode::Unsupported;
    return modules::display::DisplayCaptureStatusCode::SystemCallFailed;
}

bool statusRecoverable(int nativeCode)
{
    const HRESULT hr = static_cast<HRESULT>(nativeCode);
    return hr == RO_E_CLOSED ||
           hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET;
}

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
createWinrtD3dDevice(ID3D11Device* device)
{
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));

    Microsoft::WRL::ComPtr<IInspectable> inspectableDevice;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
        dxgiDevice.Get(),
        &inspectableDevice));

    return winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice{
        inspectableDevice.Detach(),
        winrt::take_ownership_from_abi};
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem
createCaptureItemForMonitor(HMONITOR monitor)
{
    auto factory = winrt::get_activation_factory<
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForMonitor(
        monitor,
        winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem
createCaptureItemForWindow(HWND window)
{
    auto factory = winrt::get_activation_factory<
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(
        window,
        winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

#endif

} // namespace

struct WindowsGraphicsCapture::Impl
{
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::SizeInt32 contentSize{};
#endif
};

WindowsGraphicsCapture::WindowsGraphicsCapture()
    : impl_(std::make_unique<Impl>())
{
}

WindowsGraphicsCapture::~WindowsGraphicsCapture()
{
    close();
}

modules::display::DisplayTopologySnapshot
WindowsGraphicsCaptureSourceCatalog::snapshot() const
{
#if defined(_WIN32)
    return makeTopologySnapshot();
#else
    return {};
#endif
}

bool WindowsGraphicsCapture::open(
    const modules::display::DisplayCaptureOpenOptions& options)
{
    close();
    options_ = options;
    impl_ = std::make_unique<Impl>();

#if defined(_WIN32)
    HRESULT hr = ensureApartmentForCapture();
    if (FAILED(hr)) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "WGC capture could not initialize COM apartment",
                           statusRecoverable(static_cast<int>(hr)));
        close();
        return false;
    }

    try {
        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            recordCaptureError(
                modules::display::DisplayCaptureStatusCode::SessionModeUnsupported,
                0,
                "Windows Graphics Capture is not supported in the current session",
                true);
            close();
            return false;
        }
    } catch (const winrt::hresult_error& error) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(error.code())),
                           static_cast<int>(error.code()),
                           "WGC capture support probe failed",
                           statusRecoverable(static_cast<int>(error.code())));
        close();
        return false;
    }

    MonitorRecord selectedMonitor;
    WindowRecord selectedWindow;
    bool hasMonitor = false;
    bool hasWindow = false;
    if (options.sourceType ==
        modules::display::DisplayCaptureSourceType::Monitor) {
        const std::vector<MonitorRecord> monitors = enumerateMonitorRecords();
        const MonitorRecord* monitor = selectMonitor(monitors, options.sourceId);
        if (monitor == nullptr) {
            recordCaptureError(
                modules::display::DisplayCaptureStatusCode::SourceNotFound,
                0,
                "WGC monitor capture source id was not found",
                true);
            close();
            return false;
        }
        selectedMonitor = *monitor;
        hasMonitor = true;
        sourceGeometry_.x = selectedMonitor.rect.left;
        sourceGeometry_.y = selectedMonitor.rect.top;
        sourceGeometry_.width = static_cast<std::uint32_t>(
            selectedMonitor.rect.right - selectedMonitor.rect.left);
        sourceGeometry_.height = static_cast<std::uint32_t>(
            selectedMonitor.rect.bottom - selectedMonitor.rect.top);
    } else if (options.sourceType ==
               modules::display::DisplayCaptureSourceType::Window) {
        if (options.nativeSourceHandle != 0) {
            const auto window = reinterpret_cast<HWND>(
                static_cast<std::uintptr_t>(options.nativeSourceHandle));
            if (!makeWindowRecord(window, selectedWindow)) {
                recordCaptureError(
                    modules::display::DisplayCaptureStatusCode::SourceNotFound,
                    0,
                    "WGC window native source handle was not found",
                    true);
                close();
                return false;
            }
        } else {
            const std::vector<WindowRecord> windows = enumerateWindowRecords();
            const WindowRecord* window = selectWindow(windows, options.sourceId);
            if (window == nullptr) {
                recordCaptureError(
                    modules::display::DisplayCaptureStatusCode::SourceNotFound,
                    0,
                    "WGC window capture source id was not found",
                    true);
                close();
                return false;
            }
            selectedWindow = *window;
        }
        hasWindow = true;
        sourceGeometry_.x = selectedWindow.rect.left;
        sourceGeometry_.y = selectedWindow.rect.top;
        sourceGeometry_.width = static_cast<std::uint32_t>(
            selectedWindow.rect.right - selectedWindow.rect.left);
        sourceGeometry_.height = static_cast<std::uint32_t>(
            selectedWindow.rect.bottom - selectedWindow.rect.top);
    } else {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                           0,
                           "WGC capture source type is not supported",
                           false);
        close();
        return false;
    }

    hr = createDefaultD3dDevice(impl_->device, impl_->context);
    if (!succeeded(hr)) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "WGC capture could not create a D3D11 device",
                           statusRecoverable(static_cast<int>(hr)));
        close();
        return false;
    }

    try {
        impl_->winrtDevice = createWinrtD3dDevice(impl_->device.Get());
        if (hasMonitor)
            impl_->item = createCaptureItemForMonitor(selectedMonitor.handle);
        else if (hasWindow)
            impl_->item = createCaptureItemForWindow(selectedWindow.handle);
        impl_->contentSize = impl_->item.Size();
        if (impl_->contentSize.Width <= 0 || impl_->contentSize.Height <= 0) {
            recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                               0,
                               "WGC capture item had invalid geometry",
                               true);
            close();
            return false;
        }

        impl_->framePool =
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::
                CreateFreeThreaded(
                    impl_->winrtDevice,
                    winrt::Windows::Graphics::DirectX::DirectXPixelFormat::
                        B8G8R8A8UIntNormalized,
                    2,
                    impl_->contentSize);
        impl_->session = impl_->framePool.CreateCaptureSession(impl_->item);
        auto sessionCursor =
            impl_->session.try_as<
                winrt::Windows::Graphics::Capture::IGraphicsCaptureSession2>();
        if (sessionCursor)
            sessionCursor.IsCursorCaptureEnabled(options.includeCursor);
        impl_->session.StartCapture();
    } catch (const winrt::hresult_error& error) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(error.code())),
                           static_cast<int>(error.code()),
                           "WGC capture could not start the capture session",
                           statusRecoverable(static_cast<int>(error.code())));
        close();
        return false;
    }

    opened_ = true;
    recordOk();
    return true;
#else
    recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                       0,
                       "Windows Graphics Capture is only available on Windows",
                       false);
    return false;
#endif
}

void WindowsGraphicsCapture::close()
{
#if defined(_WIN32)
    if (impl_) {
        try {
            if (impl_->session)
                impl_->session.Close();
        } catch (...) {
        }
        try {
            if (impl_->framePool)
                impl_->framePool.Close();
        } catch (...) {
        }
        impl_->session = nullptr;
        impl_->framePool = nullptr;
        impl_->item = nullptr;
        impl_->winrtDevice = nullptr;
        impl_->context.Reset();
        impl_->device.Reset();
    }
#endif
    opened_ = false;
    sourceGeometry_ = {};
}

modules::display::CapturedFrame WindowsGraphicsCapture::captureNextFrame(bool keyFrame)
{
    modules::display::CapturedFrame frame;
    frame.frameId = nextFrameId_++;
    frame.sourceId = options_.sourceId;
    frame.keyFrame = keyFrame;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.monotonicTimestampUsec = monotonicNowUsec();

#if defined(_WIN32)
    if (!opened_ && !open(options_))
        return frame;

    if (!impl_ || !impl_->framePool || !impl_->context || !impl_->device) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::NotOpen,
                           0,
                           "WGC capture was not open",
                           true);
        return frame;
    }

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame captureFrame{nullptr};
    try {
        for (int attempt = 0; attempt < 10 && !captureFrame; ++attempt) {
            captureFrame = impl_->framePool.TryGetNextFrame();
            if (!captureFrame)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (const winrt::hresult_error& error) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(error.code())),
                           static_cast<int>(error.code()),
                           "WGC capture could not read the next frame",
                           statusRecoverable(static_cast<int>(error.code())));
        close();
        return frame;
    }

    if (!captureFrame) {
        recordFrameTimeout();
        return frame;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture;
    try {
        auto access = captureFrame.Surface().as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        const HRESULT hr = access->GetInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(sourceTexture.GetAddressOf()));
        if (!succeeded(hr)) {
            recordCaptureError(statusCodeFromNativeCode(static_cast<int>(hr)),
                               static_cast<int>(hr),
                               "WGC capture frame surface was not a D3D11 texture",
                               statusRecoverable(static_cast<int>(hr)));
            return frame;
        }
    } catch (const winrt::hresult_error& error) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(error.code())),
                           static_cast<int>(error.code()),
                           "WGC capture could not access the frame surface",
                           statusRecoverable(static_cast<int>(error.code())));
        return frame;
    }

    D3D11_TEXTURE2D_DESC textureDesc = {};
    sourceTexture->GetDesc(&textureDesc);
    if (textureDesc.Width == 0 || textureDesc.Height == 0) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                           0,
                           "WGC capture frame had invalid geometry",
                           true);
        return frame;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = impl_->device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (!succeeded(hr)) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "WGC capture could not create a staging texture",
                           statusRecoverable(static_cast<int>(hr)));
        return frame;
    }

    impl_->context->CopyResource(stagingTexture.Get(), sourceTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = impl_->context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (!succeeded(hr)) {
        recordCaptureError(statusCodeFromNativeCode(static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "WGC capture could not map the staging texture",
                           statusRecoverable(static_cast<int>(hr)));
        return frame;
    }

    const runtime::display::DisplayCaptureOutputSize output =
        runtime::display::resolveDisplayCaptureOutputSize(textureDesc.Width,
                                                          textureDesc.Height,
                                                          options_);
    if (output.width == 0 || output.height == 0) {
        impl_->context->Unmap(stagingTexture.Get(), 0);
        recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                           0,
                           "WGC capture resolved an invalid output size",
                           true);
        return frame;
    }

    frame.width = output.width;
    frame.height = output.height;
    frame.strideBytes = frame.width * 4;
    frame.pixels.resize(static_cast<std::size_t>(frame.strideBytes) * frame.height);

    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    if (frame.width == textureDesc.Width && frame.height == textureDesc.Height) {
        for (std::uint32_t row = 0; row < frame.height; ++row) {
            const std::uint8_t* sourceRow =
                source + static_cast<std::size_t>(mapped.RowPitch) * row;
            std::uint8_t* targetRow =
                frame.pixels.data() +
                static_cast<std::size_t>(frame.strideBytes) * row;
            std::memcpy(targetRow, sourceRow, frame.strideBytes);
        }
    } else {
        for (std::uint32_t row = 0; row < frame.height; ++row) {
            const std::uint32_t sourceY = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(row) * textureDesc.Height /
                frame.height);
            const std::uint8_t* sourceRow =
                source + static_cast<std::size_t>(mapped.RowPitch) * sourceY;
            std::uint8_t* targetRow =
                frame.pixels.data() +
                static_cast<std::size_t>(frame.strideBytes) * row;
            for (std::uint32_t column = 0; column < frame.width; ++column) {
                const std::uint32_t sourceX = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(column) * textureDesc.Width /
                    frame.width);
                std::memcpy(targetRow + static_cast<std::size_t>(column) * 4,
                            sourceRow + static_cast<std::size_t>(sourceX) * 4,
                            4);
            }
        }
    }

    impl_->context->Unmap(stagingTexture.Get(), 0);
    recordOk();
#else
    recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                       0,
                       "Windows Graphics Capture is only available on Windows",
                       false);
#endif
    return frame;
}

bool WindowsGraphicsCapture::isOpen() const
{
    return opened_;
}

int WindowsGraphicsCapture::captureErrors() const
{
    return captureErrors_;
}

std::string WindowsGraphicsCapture::backendId() const
{
    return "windows.graphics_capture";
}

modules::display::DisplayCaptureStatus WindowsGraphicsCapture::lastStatus() const
{
    return lastStatus_;
}

void WindowsGraphicsCapture::recordOk()
{
    lastStatus_ = {};
}

void WindowsGraphicsCapture::recordCaptureError(
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

void WindowsGraphicsCapture::recordFrameTimeout()
{
    lastStatus_.code = modules::display::DisplayCaptureStatusCode::FrameTimeout;
    lastStatus_.nativeCode = 0;
    lastStatus_.recoverable = true;
    lastStatus_.message = "WGC capture timed out waiting for a new frame";
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
