#include "pc_clipboard_drag_mapper.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
#include <QRect>

#include "fusiondesk/adapters/qt/display/qt_image_display_window.h"
#endif

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

bool hasArg(int argc, char** argv, const std::string& name)
{
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && name == argv[index])
            return true;
    }
    return false;
}

std::optional<std::string> optionValue(int argc,
                                       char** argv,
                                       const std::string& name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] != nullptr &&
            argv[index + 1] != nullptr &&
            name == argv[index]) {
            return std::string(argv[index + 1]);
        }
    }
    return std::nullopt;
}

std::optional<std::int32_t> int32Option(int argc,
                                        char** argv,
                                        const std::string& name)
{
    const std::optional<std::string> value = optionValue(argc, argv, name);
    if (!value.has_value())
        return std::nullopt;
    try {
        const long long parsed = std::stoll(*value);
        if (parsed < std::numeric_limits<std::int32_t>::min() ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> uint32Option(int argc,
                                          char** argv,
                                          const std::string& name)
{
    const std::optional<std::string> value = optionValue(argc, argv, name);
    if (!value.has_value())
        return std::nullopt;
    try {
        const unsigned long long parsed = std::stoull(*value);
        if (parsed == 0 ||
            parsed > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> positiveDoubleOption(int argc,
                                           char** argv,
                                           const std::string& name)
{
    const std::optional<std::string> value = optionValue(argc, argv, name);
    if (!value.has_value())
        return std::nullopt;
    try {
        const double parsed = std::stod(*value);
        if (parsed <= 0.0)
            return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

modules::clipboard::DragCoordinateSpace outputSpaceOption(int argc,
                                                          char** argv)
{
    const std::optional<std::string> value =
        optionValue(argc, argv, "--clipboard-drag-output-space");
    if (value.has_value() && *value == "local-physical")
        return modules::clipboard::DragCoordinateSpace::LocalPhysical;
    return modules::clipboard::DragCoordinateSpace::LocalLogical;
}

std::optional<modules::clipboard::DragCoordinateMapViewport> staticViewportOption(
    int argc,
    char** argv)
{
    const std::optional<std::uint32_t> width =
        uint32Option(argc, argv, "--clipboard-drag-viewport-width");
    const std::optional<std::uint32_t> height =
        uint32Option(argc, argv, "--clipboard-drag-viewport-height");
    if (!width.has_value() || !height.has_value())
        return std::nullopt;

    modules::clipboard::DragCoordinateMapViewport viewport;
    viewport.originX =
        int32Option(argc, argv, "--clipboard-drag-viewport-x").value_or(0);
    viewport.originY =
        int32Option(argc, argv, "--clipboard-drag-viewport-y").value_or(0);
    viewport.width = *width;
    viewport.height = *height;
    viewport.scale =
        positiveDoubleOption(argc,
                             argv,
                             "--clipboard-drag-viewport-scale")
            .value_or(1.0);
    viewport.outputSpace = outputSpaceOption(argc, argv);
    viewport.clampToViewport =
        !hasArg(argc, argv, "--clipboard-drag-no-clamp");
    return viewport;
}

modules::clipboard::DragCoordinateMapResult missingViewportResult()
{
    modules::clipboard::DragCoordinateMapResult result;
    result.status = protocol::ResponseStatus::InvalidArgument;
    result.message = "clipboard drag target viewport is not available";
    return result;
}

class ProviderRemoteDisplayCoordinateMapper final
    : public modules::clipboard::IRemoteDisplayCoordinateMapper
{
public:
    ProviderRemoteDisplayCoordinateMapper(
        ClipboardDragViewportProvider provider,
        std::optional<modules::clipboard::DragCoordinateMapViewport> fallback)
        : provider_(std::move(provider)),
          fallback_(fallback)
    {
    }

    modules::clipboard::DragCoordinateMapResult mapToLocalDragPoint(
        const modules::clipboard::DragSurfaceCoordinate& point) const override
    {
        std::optional<modules::clipboard::DragCoordinateMapViewport> viewport;
        if (provider_)
            viewport = provider_();
        if (!viewport.has_value())
            viewport = fallback_;
        if (!viewport.has_value())
            return missingViewportResult();

        modules::clipboard::DragCoordinateMapRequest request;
        request.source = point;
        request.viewport = *viewport;
        return modules::clipboard::mapDragCoordinateLinear(request);
    }

private:
    ClipboardDragViewportProvider provider_;
    std::optional<modules::clipboard::DragCoordinateMapViewport> fallback_;
};

} // namespace

std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
makeClipboardDragCoordinateMapper(int argc, char** argv)
{
    return makeClipboardDragCoordinateMapper(argc, argv, {});
}

std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
makeClipboardDragCoordinateMapper(
    int argc,
    char** argv,
    ClipboardDragViewportProvider viewportProvider)
{
    std::optional<modules::clipboard::DragCoordinateMapViewport> fallback =
        staticViewportOption(argc, argv);
    if (!viewportProvider && !fallback.has_value())
        return nullptr;

    return std::make_shared<ProviderRemoteDisplayCoordinateMapper>(
        std::move(viewportProvider),
        fallback);
}

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
std::optional<modules::clipboard::DragCoordinateMapViewport>
clipboardDragViewportFromDisplayWindow(
    const adapters::qt::display::QtImageDisplayWindow& window,
    int argc,
    char** argv)
{
    const QRect content = window.imageContentRectInWindow();
    if (!content.isValid() || content.width() <= 0 || content.height() <= 0)
        return std::nullopt;

    modules::clipboard::DragCoordinateMapViewport viewport;
    viewport.originX = content.x();
    viewport.originY = content.y();
    viewport.width = static_cast<std::uint32_t>(content.width());
    viewport.height = static_cast<std::uint32_t>(content.height());
    viewport.scale =
        positiveDoubleOption(argc,
                             argv,
                             "--clipboard-drag-viewport-scale")
            .value_or(window.devicePixelRatioF());
    viewport.outputSpace = outputSpaceOption(argc, argv);
    viewport.clampToViewport =
        !hasArg(argc, argv, "--clipboard-drag-no-clamp");
    return viewport;
}

std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
makeClipboardDisplayWindowDragCoordinateMapper(
    int argc,
    char** argv,
    ClipboardDisplayWindowProvider windowProvider)
{
    return makeClipboardDragCoordinateMapper(
        argc,
        argv,
        [windowProvider = std::move(windowProvider), argc, argv]()
            -> std::optional<modules::clipboard::DragCoordinateMapViewport> {
            const adapters::qt::display::QtImageDisplayWindow* window =
                windowProvider ? windowProvider() : nullptr;
            if (window == nullptr)
                return std::nullopt;
            return clipboardDragViewportFromDisplayWindow(*window, argc, argv);
        });
}
#endif

} // namespace pc
} // namespace apps
} // namespace fusiondesk
