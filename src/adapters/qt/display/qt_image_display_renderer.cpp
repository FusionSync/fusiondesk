#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"

#include <cstdint>
#include <utility>

#include <QImage>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {

namespace {

int bytesPerPixel(modules::display::DisplayPixelFormat format)
{
    switch (format) {
    case modules::display::DisplayPixelFormat::Bgra32:
    case modules::display::DisplayPixelFormat::Rgba32:
        return 4;
    case modules::display::DisplayPixelFormat::Rgb24:
        return 3;
    case modules::display::DisplayPixelFormat::Gray8:
        return 1;
    case modules::display::DisplayPixelFormat::Unknown:
        return 0;
    }
    return 0;
}

QImage::Format toQImageFormat(modules::display::DisplayPixelFormat format)
{
    switch (format) {
    case modules::display::DisplayPixelFormat::Bgra32:
        return QImage::Format_ARGB32;
    case modules::display::DisplayPixelFormat::Rgba32:
        return QImage::Format_RGBA8888;
    case modules::display::DisplayPixelFormat::Rgb24:
        return QImage::Format_RGB888;
    case modules::display::DisplayPixelFormat::Gray8:
        return QImage::Format_Grayscale8;
    case modules::display::DisplayPixelFormat::Unknown:
        return QImage::Format_Invalid;
    }
    return QImage::Format_Invalid;
}

bool hasValidLayout(const modules::display::DecodedFrame& frame)
{
    const int bpp = bytesPerPixel(frame.pixelFormat);
    if (frame.width == 0 || frame.height == 0 || bpp == 0)
        return false;

    if (frame.strideBytes < frame.width * static_cast<std::uint32_t>(bpp))
        return false;

    const std::uint64_t requiredBytes =
        static_cast<std::uint64_t>(frame.strideBytes) * frame.height;
    return requiredBytes <= frame.pixels.size();
}

} // namespace

void QtImageDisplayRenderer::setFrameRenderedHandler(FrameRenderedHandler handler)
{
    handler_ = std::move(handler);
}

bool QtImageDisplayRenderer::attachSurface(const modules::display::DisplayRenderSurface& surface)
{
    surface_ = surface;
    surfaceAttached_ = true;
    return true;
}

void QtImageDisplayRenderer::detachSurface()
{
    surface_ = {};
    surfaceAttached_ = false;
}

bool QtImageDisplayRenderer::render(const modules::display::DecodedFrame& frame)
{
    if (!surfaceAttached_)
        attachSurface({});

    if (!hasValidLayout(frame))
        return false;

    const QImage::Format format = toQImageFormat(frame.pixelFormat);
    if (format == QImage::Format_Invalid)
        return false;

    const QImage wrapped(frame.pixels.data(),
                         static_cast<int>(frame.width),
                         static_cast<int>(frame.height),
                         static_cast<int>(frame.strideBytes),
                         format);
    if (wrapped.isNull())
        return false;

    const QImage owned = wrapped.copy();
    if (handler_)
        handler_(owned);

    lastWidth_ = frame.width;
    lastHeight_ = frame.height;
    ++renderedFrames_;
    return true;
}

bool QtImageDisplayRenderer::surfaceAttached() const
{
    return surfaceAttached_;
}

int QtImageDisplayRenderer::renderedFrames() const
{
    return renderedFrames_;
}

std::uint32_t QtImageDisplayRenderer::lastWidth() const
{
    return lastWidth_;
}

std::uint32_t QtImageDisplayRenderer::lastHeight() const
{
    return lastHeight_;
}

} // namespace display
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
