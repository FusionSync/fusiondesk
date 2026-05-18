#include <cassert>

#include <QApplication>
#include <QImage>

#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"
#include "fusiondesk/adapters/qt/display/qt_image_display_window.h"

using namespace fusiondesk;

namespace {

modules::display::DecodedFrame makeFrame()
{
    modules::display::DecodedFrame frame;
    frame.frameId = 11;
    frame.keyFrame = true;
    frame.width = 3;
    frame.height = 2;
    frame.strideBytes = 12;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.pixels.assign(static_cast<std::size_t>(frame.strideBytes) * frame.height,
                        127);
    return frame;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    adapters::qt::display::QtImageDisplayRenderer renderer;
    modules::display::DisplayRenderSurface surface;
    surface.kind = modules::display::DisplayRenderSurfaceKind::QtImageSink;
    assert(renderer.attachSurface(surface));

    adapters::qt::display::QtImageDisplayWindow window;
    window.attachRenderer(renderer);
    assert(!window.hasFrame());
    assert(window.statusText() == QStringLiteral("display.initializing"));
    window.setStatusText(QStringLiteral("display.ready"));
    assert(window.statusText() == QStringLiteral("display.ready"));

    assert(renderer.render(makeFrame()));
    assert(window.hasFrame());
    assert(window.renderedFrames() == 1);
    assert(window.lastFrameSize().width() == 3);
    assert(window.lastFrameSize().height() == 2);
    assert(window.imageContentRectInWindow().width() == 3);
    assert(window.imageContentRectInWindow().height() == 2);

    window.detachRenderer();
    assert(renderer.render(makeFrame()));
    assert(window.renderedFrames() == 1);
    return 0;
}
