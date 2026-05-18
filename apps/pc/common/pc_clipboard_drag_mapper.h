#ifndef FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_DRAG_MAPPER_H
#define FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_DRAG_MAPPER_H

#include <functional>
#include <memory>
#include <optional>

#include "fusiondesk/modules/clipboard/clipboard_drag_coordinates.h"

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {
class QtImageDisplayWindow;
} // namespace display
} // namespace qt
} // namespace adapters

namespace apps {
namespace pc {

using ClipboardDragViewportProvider =
    std::function<std::optional<modules::clipboard::DragCoordinateMapViewport>()>;

std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
makeClipboardDragCoordinateMapper(int argc, char** argv);

std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
makeClipboardDragCoordinateMapper(
    int argc,
    char** argv,
    ClipboardDragViewportProvider viewportProvider);

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
using ClipboardDisplayWindowProvider =
    std::function<const adapters::qt::display::QtImageDisplayWindow*()>;

std::optional<modules::clipboard::DragCoordinateMapViewport>
clipboardDragViewportFromDisplayWindow(
    const adapters::qt::display::QtImageDisplayWindow& window,
    int argc,
    char** argv);

std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
makeClipboardDisplayWindowDragCoordinateMapper(
    int argc,
    char** argv,
    ClipboardDisplayWindowProvider windowProvider);
#endif

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_DRAG_MAPPER_H
