#ifndef FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_DRAG_COORDINATES_H
#define FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_DRAG_COORDINATES_H

#include <cstdint>
#include <memory>
#include <string>

#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

struct DragCoordinateMapViewport
{
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double scale = 1.0;
    DragCoordinateSpace outputSpace = DragCoordinateSpace::LocalLogical;
    bool clampToViewport = true;
};

struct DragCoordinateMapRequest
{
    DragSurfaceCoordinate source;
    DragCoordinateMapViewport viewport;
};

struct DragCoordinateMapResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    DragSurfaceCoordinate point;
    bool clamped = false;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

class IRemoteDisplayCoordinateMapper
{
public:
    virtual ~IRemoteDisplayCoordinateMapper() = default;

    virtual DragCoordinateMapResult mapToLocalDragPoint(
        const DragSurfaceCoordinate& point) const = 0;
};

DragCoordinateMapResult mapDragCoordinateLinear(
    const DragCoordinateMapRequest& request);

class StaticRemoteDisplayCoordinateMapper final
    : public IRemoteDisplayCoordinateMapper
{
public:
    explicit StaticRemoteDisplayCoordinateMapper(
        DragCoordinateMapViewport viewport);

    DragCoordinateMapResult mapToLocalDragPoint(
        const DragSurfaceCoordinate& point) const override;

private:
    DragCoordinateMapViewport viewport_;
};

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_DRAG_COORDINATES_H
