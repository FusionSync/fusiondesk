#include "fusiondesk/modules/clipboard/clipboard_drag_coordinates.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

bool validOutputSpace(DragCoordinateSpace space)
{
    return space == DragCoordinateSpace::LocalLogical ||
           space == DragCoordinateSpace::LocalPhysical;
}

DragCoordinateMapResult failedCoordinateMap(protocol::ResponseStatus status,
                                            std::string message)
{
    DragCoordinateMapResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::int32_t roundToInt(double value)
{
    return static_cast<std::int32_t>(std::llround(value));
}

} // namespace

DragCoordinateMapResult mapDragCoordinateLinear(
    const DragCoordinateMapRequest& request)
{
    if (request.source.surfaceWidth == 0 ||
        request.source.surfaceHeight == 0) {
        return failedCoordinateMap(
            protocol::ResponseStatus::InvalidArgument,
            "drag source surface size is required");
    }
    if (request.viewport.width == 0 || request.viewport.height == 0) {
        return failedCoordinateMap(
            protocol::ResponseStatus::InvalidArgument,
            "drag target viewport size is required");
    }
    if (request.viewport.scale <= 0.0) {
        return failedCoordinateMap(
            protocol::ResponseStatus::InvalidArgument,
            "drag target viewport scale must be positive");
    }
    if (!validOutputSpace(request.viewport.outputSpace)) {
        return failedCoordinateMap(
            protocol::ResponseStatus::InvalidArgument,
            "drag target coordinate space must be local");
    }

    double x =
        static_cast<double>(request.source.x) /
        static_cast<double>(request.source.surfaceWidth);
    double y =
        static_cast<double>(request.source.y) /
        static_cast<double>(request.source.surfaceHeight);

    DragCoordinateMapResult result;
    result.status = protocol::ResponseStatus::Ok;
    if (request.viewport.clampToViewport) {
        const double clampedX = std::max(0.0, std::min(1.0, x));
        const double clampedY = std::max(0.0, std::min(1.0, y));
        result.clamped = clampedX != x || clampedY != y;
        x = clampedX;
        y = clampedY;
    }

    double localX =
        static_cast<double>(request.viewport.originX) +
        x * static_cast<double>(request.viewport.width);
    double localY =
        static_cast<double>(request.viewport.originY) +
        y * static_cast<double>(request.viewport.height);

    if (request.viewport.outputSpace == DragCoordinateSpace::LocalPhysical) {
        localX *= request.viewport.scale;
        localY *= request.viewport.scale;
    }

    result.point = request.source;
    result.point.coordinateSpace = request.viewport.outputSpace;
    result.point.x = roundToInt(localX);
    result.point.y = roundToInt(localY);
    result.point.surfaceWidth = request.viewport.width;
    result.point.surfaceHeight = request.viewport.height;
    result.point.scale = request.viewport.scale;
    return result;
}

StaticRemoteDisplayCoordinateMapper::StaticRemoteDisplayCoordinateMapper(
    DragCoordinateMapViewport viewport)
    : viewport_(viewport)
{
}

DragCoordinateMapResult
StaticRemoteDisplayCoordinateMapper::mapToLocalDragPoint(
    const DragSurfaceCoordinate& point) const
{
    DragCoordinateMapRequest request;
    request.source = point;
    request.viewport = viewport_;
    return mapDragCoordinateLinear(request);
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
