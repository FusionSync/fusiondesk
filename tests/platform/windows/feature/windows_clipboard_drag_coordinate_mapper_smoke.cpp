#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;
using namespace fusiondesk::platform::windows::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

TransferSourceBundle textBundle()
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("drag text");

    TransferSourceBundle bundle;
    bundle.bundleId = 12;
    bundle.offerId = 23;
    bundle.ownerEpoch = 34;
    bundle.sequence = 45;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            77,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

DragSurfaceCoordinate remotePoint(std::int32_t x, std::int32_t y)
{
    DragSurfaceCoordinate point;
    point.coordinateSpace = DragCoordinateSpace::RemoteLogical;
    point.x = x;
    point.y = y;
    point.surfaceWidth = 1280;
    point.surfaceHeight = 720;
    point.scale = 1.0;
    return point;
}

DragSessionStart dragStart(std::int32_t x, std::int32_t y)
{
    DragSessionStart start;
    start.dragSessionId = 7001;
    start.bundleId = 12;
    start.offerId = 23;
    start.ownerEpoch = 34;
    start.allowedActions = transfer_action::Copy;
    start.preferredAction = TransferAction::Copy;
    start.start = remotePoint(x, y);
    return start;
}

std::shared_ptr<IRemoteDisplayCoordinateMapper> mapper()
{
    DragCoordinateMapViewport viewport;
    viewport.originX = 10;
    viewport.originY = 20;
    viewport.width = 640;
    viewport.height = 360;
    viewport.outputSpace = DragCoordinateSpace::LocalLogical;
    return std::make_shared<StaticRemoteDisplayCoordinateMapper>(viewport);
}

void endpointRecordsMappedDragCoordinates()
{
    WindowsClipboardEndpoint endpoint({}, {}, {}, {}, mapper());
    assert(endpoint.publishBundle(ClipboardPublishRequest{textBundle()}) ==
           protocol::ResponseStatus::Ok);

    assert(endpoint.dragStart(dragStart(640, 360)) ==
           protocol::ResponseStatus::Ok);
    WindowsClipboardEndpointDiagnostics diagnostics = endpoint.diagnostics();
    assert(diagnostics.activeDragSessionId == 7001);
    assert(diagnostics.lastDragX == 330);
    assert(diagnostics.lastDragY == 200);

    assert(endpoint.dragMove(7001, remotePoint(1280, 720), TransferAction::Copy) ==
           protocol::ResponseStatus::Ok);
    diagnostics = endpoint.diagnostics();
    assert(diagnostics.lastDragX == 650);
    assert(diagnostics.lastDragY == 380);

    assert(endpoint.dragDrop(7001, remotePoint(0, 0), TransferAction::Copy) ==
           protocol::ResponseStatus::Ok);
    diagnostics = endpoint.diagnostics();
    assert(diagnostics.activeDragSessionId == 0);
    assert(diagnostics.lastDragX == 10);
    assert(diagnostics.lastDragY == 20);
}

void invalidMappedCoordinateRejectsDragStart()
{
    DragCoordinateMapViewport viewport;
    viewport.width = 640;
    viewport.height = 0;
    auto invalidMapper =
        std::make_shared<StaticRemoteDisplayCoordinateMapper>(viewport);

    WindowsClipboardEndpoint endpoint({}, {}, {}, {}, invalidMapper);
    assert(endpoint.publishBundle(ClipboardPublishRequest{textBundle()}) ==
           protocol::ResponseStatus::Ok);
    assert(endpoint.dragStart(dragStart(640, 360)) ==
           protocol::ResponseStatus::InvalidArgument);

    const WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.activeDragSessionId == 0);
    assert(!diagnostics.lastMessage.empty());
}

} // namespace

int main()
{
    endpointRecordsMappedDragCoordinates();
    invalidMappedCoordinateRejectsDragStart();
    return 0;
}
