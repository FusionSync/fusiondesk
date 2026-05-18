#ifndef FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_NATIVE_HELPERS_H
#define FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_NATIVE_HELPERS_H

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_descriptors.h"

#include <chrono>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#endif

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

inline std::uint64_t windowsClipboardMonotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

#if defined(_WIN32)

inline constexpr const wchar_t* OwnerMarkerFormatName =
    L"FusionDeskClipboardOwner";
inline constexpr const wchar_t* OwnerWindowClassName =
    L"FusionDeskClipboardEndpointWindow";

struct OwnerMarker
{
    std::uint32_t pid = 0;
    std::uint64_t offerId = 0;
    std::uint64_t ownerEpoch = 0;
    std::uint64_t sequence = 0;
};

inline bool tryOpenClipboard(HWND owner,
                             std::uint32_t retryCount,
                             std::uint32_t retryDelayMs)
{
    const std::uint32_t attempts = retryCount == 0 ? 1 : retryCount;
    for (std::uint32_t attempt = 0; attempt < attempts; ++attempt) {
        if (OpenClipboard(owner))
            return true;
        if (attempt + 1 < attempts && retryDelayMs > 0)
            Sleep(retryDelayMs);
    }
    return false;
}

inline LRESULT CALLBACK clipboardOwnerWindowProc(HWND hwnd,
                                                 UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam)
{
    if (message == WM_RENDERFORMAT) {
        auto* endpoint = reinterpret_cast<WindowsClipboardEndpoint*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (endpoint != nullptr) {
            endpoint->renderDelayedFormatForNative(
                static_cast<std::uint32_t>(wParam));
        }
        return 0;
    }

    if (message == WM_RENDERALLFORMATS) {
        auto* endpoint = reinterpret_cast<WindowsClipboardEndpoint*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (endpoint != nullptr)
            endpoint->renderAllDelayedFormatsForNative();
        return 0;
    }

    if (message == WM_CLIPBOARDUPDATE) {
        auto* endpoint = reinterpret_cast<WindowsClipboardEndpoint*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (endpoint != nullptr)
            endpoint->notifyNativeClipboardChanged();
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

inline HWND clipboardOwnerWindow()
{
    static std::once_flag once;
    static HWND hwnd = nullptr;
    std::call_once(once, []() {
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = clipboardOwnerWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = OwnerWindowClassName;
        RegisterClassW(&windowClass);
        hwnd = CreateWindowExW(0,
                               OwnerWindowClassName,
                               L"",
                               0,
                               0,
                               0,
                               0,
                               0,
                               HWND_MESSAGE,
                               nullptr,
                               windowClass.hInstance,
                               nullptr);
    });
    return hwnd;
}

inline UINT ownerMarkerFormat()
{
    return RegisterClipboardFormatW(OwnerMarkerFormatName);
}

inline UINT htmlFormat()
{
    return RegisterClipboardFormatA(WindowsHtmlName);
}

inline UINT rtfFormat()
{
    return RegisterClipboardFormatA(WindowsRtfName);
}

inline UINT pngFormat()
{
    return RegisterClipboardFormatA(WindowsPngName);
}

inline bool readOwnerMarker(OwnerMarker& marker)
{
    const UINT format = ownerMarkerFormat();
    if (format == 0 || !IsClipboardFormatAvailable(format))
        return false;

    HANDLE handle = GetClipboardData(format);
    if (handle == nullptr)
        return false;

    const SIZE_T size = GlobalSize(handle);
    if (size < sizeof(OwnerMarker))
        return false;

    const void* locked = GlobalLock(handle);
    if (locked == nullptr)
        return false;

    std::memcpy(&marker, locked, sizeof(marker));
    GlobalUnlock(handle);
    return true;
}

inline HANDLE allocMoveableBytes(const protocol::ByteBuffer& bytes)
{
    const SIZE_T size = static_cast<SIZE_T>(bytes.size());
    HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (handle == nullptr)
        return nullptr;

    void* locked = GlobalLock(handle);
    if (locked == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    if (!bytes.empty())
        std::memcpy(locked, bytes.data(), bytes.size());
    GlobalUnlock(handle);
    return handle;
}

inline HANDLE allocOwnerMarker(
    modules::clipboard::TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence)
{
    OwnerMarker marker;
    marker.pid = GetCurrentProcessId();
    marker.offerId = offerId;
    marker.ownerEpoch = ownerEpoch;
    marker.sequence = sequence;

    protocol::ByteBuffer bytes(sizeof(marker));
    std::memcpy(bytes.data(), &marker, sizeof(marker));
    return allocMoveableBytes(bytes);
}

#endif // defined(_WIN32)

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_NATIVE_HELPERS_H
