#include <cassert>

#include "fusiondesk/modules/clipboard/clipboard_drag_coordinates.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

DragSurfaceCoordinate remotePoint(std::int32_t x = 960,
                                  std::int32_t y = 540)
{
    DragSurfaceCoordinate point;
    point.coordinateSpace = DragCoordinateSpace::RemoteLogical;
    point.x = x;
    point.y = y;
    point.surfaceWidth = 1920;
    point.surfaceHeight = 1080;
    point.scale = 1.0;
    return point;
}

DragCoordinateMapViewport viewport()
{
    DragCoordinateMapViewport value;
    value.originX = 100;
    value.originY = 50;
    value.width = 1280;
    value.height = 720;
    value.scale = 1.0;
    value.outputSpace = DragCoordinateSpace::LocalLogical;
    return value;
}

void mapsRemoteLogicalToLocalViewport()
{
    DragCoordinateMapRequest request;
    request.source = remotePoint();
    request.viewport = viewport();

    const DragCoordinateMapResult result = mapDragCoordinateLinear(request);
    assert(result.ok());
    assert(result.point.coordinateSpace == DragCoordinateSpace::LocalLogical);
    assert(result.point.x == 740);
    assert(result.point.y == 410);
    assert(result.point.surfaceWidth == 1280);
    assert(result.point.surfaceHeight == 720);
    assert(!result.clamped);
}

void clampsOutOfSurfaceCoordinates()
{
    DragCoordinateMapRequest request;
    request.source = remotePoint(2500, -20);
    request.viewport = viewport();

    const DragCoordinateMapResult result = mapDragCoordinateLinear(request);
    assert(result.ok());
    assert(result.clamped);
    assert(result.point.x == 1380);
    assert(result.point.y == 50);
}

void canMapToLocalPhysicalSpace()
{
    DragCoordinateMapRequest request;
    request.source = remotePoint();
    request.viewport = viewport();
    request.viewport.scale = 2.0;
    request.viewport.outputSpace = DragCoordinateSpace::LocalPhysical;

    const DragCoordinateMapResult result = mapDragCoordinateLinear(request);
    assert(result.ok());
    assert(result.point.coordinateSpace == DragCoordinateSpace::LocalPhysical);
    assert(result.point.x == 1480);
    assert(result.point.y == 820);
    assert(result.point.scale == 2.0);
}

void rejectsInvalidSurfaceOrViewport()
{
    DragCoordinateMapRequest request;
    request.source = remotePoint();
    request.source.surfaceWidth = 0;
    request.viewport = viewport();
    assert(mapDragCoordinateLinear(request).status ==
           protocol::ResponseStatus::InvalidArgument);

    request.source = remotePoint();
    request.viewport = viewport();
    request.viewport.height = 0;
    assert(mapDragCoordinateLinear(request).status ==
           protocol::ResponseStatus::InvalidArgument);
}

void staticMapperUsesStoredViewport()
{
    StaticRemoteDisplayCoordinateMapper mapper(viewport());
    const DragCoordinateMapResult result =
        mapper.mapToLocalDragPoint(remotePoint(480, 270));
    assert(result.ok());
    assert(result.point.x == 420);
    assert(result.point.y == 230);
}

} // namespace

int main()
{
    mapsRemoteLogicalToLocalViewport();
    clampsOutOfSurfaceCoordinates();
    canMapToLocalPhysicalSpace();
    rejectsInvalidSurfaceOrViewport();
    staticMapperUsesStoredViewport();
    return 0;
}
