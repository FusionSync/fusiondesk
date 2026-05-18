#include <cassert>

#include <QImage>

#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"

using namespace fusiondesk;

namespace {

modules::display::DecodedFrame makeFrame()
{
    modules::display::DecodedFrame frame;
    frame.frameId = 7;
    frame.keyFrame = true;
    frame.width = 2;
    frame.height = 2;
    frame.strideBytes = 8;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.pixels = {
        0, 0, 255, 255,
        0, 255, 0, 255,
        255, 0, 0, 255,
        255, 255, 255, 255};
    return frame;
}

void rendersSoftwareFrameIntoQImageCallback()
{
    adapters::qt::display::QtImageDisplayRenderer renderer;
    modules::display::DisplayRenderSurface surface;
    surface.kind = modules::display::DisplayRenderSurfaceKind::QtImageSink;
    surface.width = 2;
    surface.height = 2;
    assert(renderer.attachSurface(surface));
    assert(renderer.surfaceAttached());

    int rendered = 0;
    renderer.setFrameRenderedHandler([&rendered](const QImage& image) {
        assert(!image.isNull());
        assert(image.width() == 2);
        assert(image.height() == 2);
        ++rendered;
    });

    assert(renderer.render(makeFrame()));
    assert(rendered == 1);
    assert(renderer.renderedFrames() == 1);
    assert(renderer.lastWidth() == 2);
    assert(renderer.lastHeight() == 2);

    renderer.detachSurface();
    assert(!renderer.surfaceAttached());
}

void rejectsInvalidSoftwareFrames()
{
    adapters::qt::display::QtImageDisplayRenderer renderer;
    modules::display::DecodedFrame frame = makeFrame();
    frame.strideBytes = 1;
    assert(!renderer.render(frame));
    assert(renderer.renderedFrames() == 0);
}

} // namespace

int main()
{
    rendersSoftwareFrameIntoQImageCallback();
    rejectsInvalidSoftwareFrames();
    return 0;
}
