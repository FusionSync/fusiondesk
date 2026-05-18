#include "fusiondesk/platform/windows/display/windows_cursor_overlay.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

namespace {

int scaleToFrame(std::int64_t value,
                 std::uint32_t frameSize,
                 std::uint32_t sourceSize)
{
    if (sourceSize == 0)
        return 0;
    return static_cast<int>(std::lround(
        static_cast<double>(value) * static_cast<double>(frameSize) /
        static_cast<double>(sourceSize)));
}

#if defined(_WIN32)

struct CursorImageInfo
{
    HICON icon = nullptr;
    std::uint32_t hotspotX = 0;
    std::uint32_t hotspotY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

void deleteIconInfoBitmaps(ICONINFO& iconInfo)
{
    if (iconInfo.hbmMask != nullptr)
        DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor != nullptr)
        DeleteObject(iconInfo.hbmColor);
}

CursorImageInfo captureCurrentCursorImage()
{
    CursorImageInfo result;

    CURSORINFO cursorInfo = {};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo))
        return result;
    if ((cursorInfo.flags & CURSOR_SHOWING) == 0 || cursorInfo.hCursor == nullptr)
        return result;

    result.icon = CopyIcon(cursorInfo.hCursor);
    if (result.icon == nullptr)
        return result;

    result.width = static_cast<std::uint32_t>(
        std::max(1, GetSystemMetrics(SM_CXCURSOR)));
    result.height = static_cast<std::uint32_t>(
        std::max(1, GetSystemMetrics(SM_CYCURSOR)));

    ICONINFO iconInfo = {};
    if (GetIconInfo(result.icon, &iconInfo)) {
        result.hotspotX = iconInfo.xHotspot;
        result.hotspotY = iconInfo.yHotspot;

        BITMAP bitmap = {};
        if (iconInfo.hbmColor != nullptr &&
            GetObject(iconInfo.hbmColor, sizeof(bitmap), &bitmap) != 0) {
            result.width = static_cast<std::uint32_t>(std::max(1L, bitmap.bmWidth));
            result.height = static_cast<std::uint32_t>(std::max(1L, bitmap.bmHeight));
        } else if (iconInfo.hbmMask != nullptr &&
                   GetObject(iconInfo.hbmMask, sizeof(bitmap), &bitmap) != 0) {
            result.width = static_cast<std::uint32_t>(std::max(1L, bitmap.bmWidth));
            result.height = static_cast<std::uint32_t>(
                std::max(1L, bitmap.bmHeight / 2));
        }
        deleteIconInfoBitmaps(iconInfo);
    }

    return result;
}

#endif

} // namespace

WindowsCursorFrameRect mapWindowsCursorToFrame(
    const WindowsCursorFrameMappingInput& input)
{
    WindowsCursorFrameRect result;
    if (input.sourceWidth == 0 || input.sourceHeight == 0 ||
        input.frameWidth == 0 || input.frameHeight == 0 ||
        input.cursorWidth == 0 || input.cursorHeight == 0)
        return result;

    const int relativeCursorX =
        input.cursorScreenX - input.sourceX;
    const int relativeCursorY =
        input.cursorScreenY - input.sourceY;

    const int scaledCursorX =
        scaleToFrame(relativeCursorX, input.frameWidth, input.sourceWidth);
    const int scaledCursorY =
        scaleToFrame(relativeCursorY, input.frameHeight, input.sourceHeight);
    const int scaledHotspotX =
        scaleToFrame(input.hotspotX, input.frameWidth, input.sourceWidth);
    const int scaledHotspotY =
        scaleToFrame(input.hotspotY, input.frameHeight, input.sourceHeight);
    const int scaledWidth = std::max(
        1, scaleToFrame(input.cursorWidth, input.frameWidth, input.sourceWidth));
    const int scaledHeight = std::max(
        1, scaleToFrame(input.cursorHeight, input.frameHeight, input.sourceHeight));

    result.x = scaledCursorX - scaledHotspotX;
    result.y = scaledCursorY - scaledHotspotY;
    result.width = scaledWidth;
    result.height = scaledHeight;
    result.insideFrame =
        result.x < static_cast<int>(input.frameWidth) &&
        result.y < static_cast<int>(input.frameHeight) &&
        result.x + result.width > 0 &&
        result.y + result.height > 0;
    return result;
}

bool overlayWindowsCursorOnBgraFrame(
    modules::display::CapturedFrame& frame,
    const modules::display::DisplaySourceGeometry& sourceGeometry,
    const modules::display::DisplayCaptureOpenOptions& options)
{
    if (!options.includeCursor)
        return false;
    if (frame.pixelFormat != modules::display::DisplayPixelFormat::Bgra32 ||
        frame.width == 0 || frame.height == 0 || frame.strideBytes < frame.width * 4 ||
        frame.pixels.size() <
            static_cast<std::size_t>(frame.strideBytes) * frame.height)
        return false;

#if defined(_WIN32)
    CURSORINFO cursorInfo = {};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo))
        return false;
    if ((cursorInfo.flags & CURSOR_SHOWING) == 0 || cursorInfo.hCursor == nullptr)
        return false;

    CursorImageInfo cursor = captureCurrentCursorImage();
    if (cursor.icon == nullptr)
        return false;

    WindowsCursorFrameMappingInput mappingInput;
    mappingInput.sourceX = sourceGeometry.x;
    mappingInput.sourceY = sourceGeometry.y;
    mappingInput.sourceWidth = sourceGeometry.width;
    mappingInput.sourceHeight = sourceGeometry.height;
    mappingInput.frameWidth = frame.width;
    mappingInput.frameHeight = frame.height;
    mappingInput.cursorScreenX = cursorInfo.ptScreenPos.x;
    mappingInput.cursorScreenY = cursorInfo.ptScreenPos.y;
    mappingInput.hotspotX = cursor.hotspotX;
    mappingInput.hotspotY = cursor.hotspotY;
    mappingInput.cursorWidth = cursor.width;
    mappingInput.cursorHeight = cursor.height;

    const WindowsCursorFrameRect rect = mapWindowsCursorToFrame(mappingInput);
    if (!rect.insideFrame) {
        DestroyIcon(cursor.icon);
        return false;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(frame.width);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* dibPixels = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screenDc,
                                      &bitmapInfo,
                                      DIB_RGB_COLORS,
                                      &dibPixels,
                                      nullptr,
                                      0);
    if (bitmap == nullptr || dibPixels == nullptr) {
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        if (screenDc != nullptr)
            ReleaseDC(nullptr, screenDc);
        DestroyIcon(cursor.icon);
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (memoryDc == nullptr) {
        DeleteObject(bitmap);
        if (screenDc != nullptr)
            ReleaseDC(nullptr, screenDc);
        DestroyIcon(cursor.icon);
        return false;
    }

    const std::size_t byteCount =
        static_cast<std::size_t>(frame.strideBytes) * frame.height;
    std::memcpy(dibPixels, frame.pixels.data(), byteCount);

    HGDIOBJ previous = SelectObject(memoryDc, bitmap);
    const BOOL drawn = DrawIconEx(memoryDc,
                                  rect.x,
                                  rect.y,
                                  cursor.icon,
                                  rect.width,
                                  rect.height,
                                  0,
                                  nullptr,
                                  DI_NORMAL);
    SelectObject(memoryDc, previous);

    if (drawn != FALSE)
        std::memcpy(frame.pixels.data(), dibPixels, byteCount);

    DeleteDC(memoryDc);
    DeleteObject(bitmap);
    if (screenDc != nullptr)
        ReleaseDC(nullptr, screenDc);
    DestroyIcon(cursor.icon);
    return drawn != FALSE;
#else
    (void)sourceGeometry;
    return false;
#endif
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
