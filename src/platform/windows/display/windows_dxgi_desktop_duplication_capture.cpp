#include "fusiondesk/platform/windows/display/windows_dxgi_desktop_duplication_capture.h"

#include "fusiondesk/platform/windows/display/windows_cursor_overlay.h"
#include "fusiondesk/runtime/display/display_capture_geometry.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

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

HRESULT createDefaultDxgiDevice(Microsoft::WRL::ComPtr<ID3D11Device>& device,
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

modules::display::DisplayTopologySnapshot makeDxgiTopologySnapshot()
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

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (!succeeded(createDefaultDxgiDevice(device, context)))
        return snapshot;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (!succeeded(device.As(&dxgiDevice)))
        return snapshot;

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (!succeeded(dxgiDevice->GetAdapter(&adapter)))
        return snapshot;

    for (UINT outputIndex = 0;; ++outputIndex) {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        const HRESULT hr = adapter->EnumOutputs(outputIndex, &output);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (!succeeded(hr))
            break;

        DXGI_OUTPUT_DESC desc = {};
        if (!succeeded(output->GetDesc(&desc)) || !desc.AttachedToDesktop)
            continue;

        modules::display::DisplaySourceInfo source;
        source.sourceId = static_cast<std::uint32_t>(outputIndex);
        source.primary =
            desc.DesktopCoordinates.left == 0 &&
            desc.DesktopCoordinates.top == 0;
        source.geometry.x = desc.DesktopCoordinates.left;
        source.geometry.y = desc.DesktopCoordinates.top;
        source.geometry.width = static_cast<std::uint32_t>(
            desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
        source.geometry.height = static_cast<std::uint32_t>(
            desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
        source.dpiX = dpiX;
        source.dpiY = dpiY;
        source.name = toUtf8(desc.DeviceName);
        snapshot.sources.push_back(source);
    }

    return snapshot;
}

#endif

} // namespace

struct WindowsDxgiDesktopDuplicationCapture::Impl
{
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_OUTDUPL_DESC duplicationDesc = {};
#endif
};

WindowsDxgiDesktopDuplicationCapture::WindowsDxgiDesktopDuplicationCapture()
    : impl_(std::make_unique<Impl>())
{
}

WindowsDxgiDesktopDuplicationCapture::~WindowsDxgiDesktopDuplicationCapture()
{
    close();
}

modules::display::DisplayTopologySnapshot
WindowsDxgiDesktopDuplicationSourceCatalog::snapshot() const
{
#if defined(_WIN32)
    return makeDxgiTopologySnapshot();
#else
    return {};
#endif
}

WindowsDxgiDesktopDuplicationProbeResult
probeWindowsDxgiDesktopDuplication(std::uint32_t sourceId)
{
    WindowsDxgiDesktopDuplicationProbeResult result;
#if defined(_WIN32)
    WindowsDxgiDesktopDuplicationCapture capture;
    modules::display::DisplayCaptureOpenOptions options;
    options.sourceId = sourceId;
    if (capture.open(options)) {
        capture.close();
        result.available = true;
        result.reason = "DXGI Desktop Duplication initialized";
        return result;
    }

    const modules::display::DisplayCaptureStatus status = capture.lastStatus();
    result.reason = status.message.empty()
                        ? "DXGI Desktop Duplication could not initialize in the current session"
                        : status.message;
    return result;
#else
    (void)sourceId;
    result.reason = "DXGI Desktop Duplication is only available on Windows";
    return result;
#endif
}

modules::display::DisplayCaptureStatusCode
windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(int nativeCode)
{
#if defined(_WIN32)
    const HRESULT hr = static_cast<HRESULT>(nativeCode);
    if (hr == E_ACCESSDENIED)
        return modules::display::DisplayCaptureStatusCode::AccessDenied;
    if (hr == DXGI_ERROR_ACCESS_LOST)
        return modules::display::DisplayCaptureStatusCode::SourceHotplug;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        return modules::display::DisplayCaptureStatusCode::DeviceLost;
    if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ||
        hr == DXGI_ERROR_SESSION_DISCONNECTED ||
        hr == DXGI_ERROR_UNSUPPORTED)
        return modules::display::DisplayCaptureStatusCode::SessionModeUnsupported;
    if (hr == DXGI_ERROR_INVALID_CALL)
        return modules::display::DisplayCaptureStatusCode::Unsupported;
#else
    (void)nativeCode;
#endif
    return modules::display::DisplayCaptureStatusCode::SystemCallFailed;
}

bool windowsDxgiDesktopDuplicationStatusRecoverable(int nativeCode)
{
#if defined(_WIN32)
    const HRESULT hr = static_cast<HRESULT>(nativeCode);
    return hr == DXGI_ERROR_ACCESS_LOST ||
           hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ||
           hr == DXGI_ERROR_SESSION_DISCONNECTED;
#else
    (void)nativeCode;
    return false;
#endif
}

bool WindowsDxgiDesktopDuplicationCapture::open(
    const modules::display::DisplayCaptureOpenOptions& options)
{
    close();
    options_ = options;
    impl_ = std::make_unique<Impl>();

#if defined(_WIN32)
    if (options_.sourceType !=
        modules::display::DisplayCaptureSourceType::Monitor) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                           0,
                           "DXGI Desktop Duplication only supports monitor sources",
                           false);
        close();
        return false;
    }

    HRESULT hr = createDefaultDxgiDevice(impl_->device, impl_->context);
    if (!succeeded(hr)) {
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI capture could not create a D3D11 device",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        close();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = impl_->device.As(&dxgiDevice);
    if (!succeeded(hr)) {
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI capture could not query IDXGIDevice",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        close();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (!succeeded(hr)) {
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI capture could not get the DXGI adapter",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        close();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(options.sourceId, &output);
    if (!succeeded(hr)) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::SourceNotFound,
                           static_cast<int>(hr),
                           "DXGI capture source id was not found",
                           true);
        close();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (!succeeded(hr)) {
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI capture could not query IDXGIOutput1",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        close();
        return false;
    }

    hr = output1->DuplicateOutput(impl_->device.Get(), &impl_->duplication);
    if (!succeeded(hr)) {
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI DuplicateOutput failed",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        close();
        return false;
    }

    impl_->duplication->GetDesc(&impl_->duplicationDesc);
    sourceGeometry_.x = 0;
    sourceGeometry_.y = 0;
    sourceGeometry_.width = impl_->duplicationDesc.ModeDesc.Width;
    sourceGeometry_.height = impl_->duplicationDesc.ModeDesc.Height;
    const modules::display::DisplayTopologySnapshot topology =
        makeDxgiTopologySnapshot();
    for (const modules::display::DisplaySourceInfo& source : topology.sources) {
        if (source.sourceId == options.sourceId) {
            sourceGeometry_ = source.geometry;
            break;
        }
    }
    opened_ = true;
    recordOk();
    return true;
#else
    recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                       0,
                       "DXGI Desktop Duplication is only available on Windows",
                       false);
    return false;
#endif
}

void WindowsDxgiDesktopDuplicationCapture::close()
{
#if defined(_WIN32)
    if (impl_) {
        impl_->duplication.Reset();
        impl_->context.Reset();
        impl_->device.Reset();
    }
#endif
    opened_ = false;
    lastFrame_ = {};
    lastFrameValid_ = false;
    sourceGeometry_ = {};
}

modules::display::CapturedFrame
WindowsDxgiDesktopDuplicationCapture::captureNextFrame(bool keyFrame)
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

    if (!impl_ || !impl_->duplication || !impl_->context) {
        recordCaptureError(modules::display::DisplayCaptureStatusCode::NotOpen,
                           0,
                           "DXGI capture was not open",
                           true);
        return frame;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
    HRESULT hr = impl_->duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (keyFrame && lastFrameValid_) {
            modules::display::CapturedFrame cached = lastFrame_;
            cached.frameId = frame.frameId;
            cached.keyFrame = true;
            cached.monotonicTimestampUsec = frame.monotonicTimestampUsec;
            recordOk();
            return cached;
        }
        recordFrameTimeout();
        return frame;
    }
    if (!succeeded(hr)) {
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI AcquireNextFrame failed",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        close();
        return frame;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (!succeeded(hr)) {
        impl_->duplication->ReleaseFrame();
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI frame resource was not a texture",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        return frame;
    }

    D3D11_TEXTURE2D_DESC textureDesc = {};
    desktopTexture->GetDesc(&textureDesc);
    if (textureDesc.Width == 0 || textureDesc.Height == 0) {
        impl_->duplication->ReleaseFrame();
        recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                           0,
                           "DXGI frame had invalid geometry",
                           true);
        return frame;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    hr = impl_->device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (!succeeded(hr)) {
        impl_->duplication->ReleaseFrame();
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI capture could not create a staging texture",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        return frame;
    }

    impl_->context->CopyResource(stagingTexture.Get(), desktopTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = impl_->context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (!succeeded(hr)) {
        impl_->duplication->ReleaseFrame();
        recordCaptureError(windowsDxgiDesktopDuplicationStatusCodeFromNativeCode(
                               static_cast<int>(hr)),
                           static_cast<int>(hr),
                           "DXGI capture could not map the staging texture",
                           windowsDxgiDesktopDuplicationStatusRecoverable(
                               static_cast<int>(hr)));
        return frame;
    }

    const runtime::display::DisplayCaptureOutputSize output =
        runtime::display::resolveDisplayCaptureOutputSize(textureDesc.Width,
                                                          textureDesc.Height,
                                                          options_);
    if (output.width == 0 || output.height == 0) {
        impl_->context->Unmap(stagingTexture.Get(), 0);
        impl_->duplication->ReleaseFrame();
        recordCaptureError(modules::display::DisplayCaptureStatusCode::InvalidFrame,
                           0,
                           "DXGI capture resolved an invalid output size",
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
    impl_->duplication->ReleaseFrame();
    overlayWindowsCursorOnBgraFrame(frame, sourceGeometry_, options_);
    lastFrame_ = frame;
    lastFrameValid_ = true;
    recordOk();
#else
    recordCaptureError(modules::display::DisplayCaptureStatusCode::Unsupported,
                       0,
                       "DXGI Desktop Duplication is only available on Windows",
                       false);
#endif

    return frame;
}

bool WindowsDxgiDesktopDuplicationCapture::isOpen() const
{
    return opened_;
}

int WindowsDxgiDesktopDuplicationCapture::captureErrors() const
{
    return captureErrors_;
}

std::string WindowsDxgiDesktopDuplicationCapture::backendId() const
{
    return "windows.dxgi.desktop_duplication";
}

modules::display::DisplayCaptureStatus
WindowsDxgiDesktopDuplicationCapture::lastStatus() const
{
    return lastStatus_;
}

void WindowsDxgiDesktopDuplicationCapture::recordOk()
{
    lastStatus_ = {};
}

void WindowsDxgiDesktopDuplicationCapture::recordCaptureError(
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

void WindowsDxgiDesktopDuplicationCapture::recordFrameTimeout()
{
    lastStatus_.code = modules::display::DisplayCaptureStatusCode::FrameTimeout;
    lastStatus_.nativeCode = 0;
    lastStatus_.recoverable = true;
    lastStatus_.message = "DXGI capture timed out waiting for a new frame";
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
