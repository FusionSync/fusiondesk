#include <cassert>
#include <string>

#include "fusiondesk/modules/clipboard/fdcl_codec.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

FdclFormatList textFormatList()
{
    FdclFormatRecord record;
    record.sourceId = 7;
    record.itemIndex = 0;
    record.formatId = 11;
    record.localFormatToken = 22;
    record.estimatedBytes = 5;
    record.canInline = true;
    record.canStream = false;
    record.preferredEncoding = TransferEncodingMode::NativePassthrough;
    record.canonicalFormat = TextPlainUtf8Format;
    record.nativeFormatName = "CF_UNICODETEXT";

    FdclFormatList list;
    list.bundleId = 101;
    list.offerId = 202;
    list.ownerEpoch = 303;
    list.sequence = 404;
    list.origin = TransferOrigin::Clipboard;
    list.side = TransferSide::Local;
    list.originSessionId = 505;
    list.policyVersion = 606;
    list.formats = {record};
    return list;
}

TransferPresentation dragPresentation()
{
    IconRepresentation icon;
    icon.format = ImagePngFormat;
    icon.width = 16;
    icon.height = 16;
    icon.bytes = 128;
    icon.sensitive = true;

    DragImage image;
    image.format = ImagePngFormat;
    image.width = 32;
    image.height = 32;
    image.scale = 2.0;
    image.hotspotX = 4;
    image.hotspotY = 5;
    image.generatedPreview = true;
    image.sensitive = true;

    TransferPresentation presentation;
    presentation.displayName = "document.txt";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::FileList;
    presentation.icons = {icon};
    presentation.dragImage = image;
    presentation.allowedActions = transfer_action::Copy | transfer_action::Link;
    presentation.preferredAction = TransferAction::Copy;
    presentation.previewAllowedByPolicy = true;
    return presentation;
}

void capabilitiesRoundTrips()
{
    FdclCapabilities capabilities;
    capabilities.protocolMajor = 1;
    capabilities.protocolMinor = 0;
    capabilities.maxInlineBytes = 4096;
    capabilities.maxStreamBytes = 65536;
    capabilities.supportedActions = transfer_action::Copy | transfer_action::Move;
    capabilities.supportsStreams = true;
    capabilities.supportsNativePassthrough = true;
    capabilities.supportsPresentationMetadata = true;
    capabilities.supportsDrag = true;
    capabilities.canonicalFormats = {TextPlainUtf8Format, TextHtmlFormat};

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclCapabilities(capabilities));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::Capabilities);
    assert(decoded.capabilities.maxInlineBytes == 4096);
    assert(decoded.capabilities.maxStreamBytes == 65536);
    assert(decoded.capabilities.supportsStreams);
    assert(decoded.capabilities.supportsPresentationMetadata);
    assert(decoded.capabilities.supportsDrag);
    assert(decoded.capabilities.canonicalFormats.size() == 2);
    assert(decoded.capabilities.canonicalFormats.front() == TextPlainUtf8Format);
}

void formatListRoundTrips()
{
    const FdclFormatList list = textFormatList();
    const FdclDecodeResult decoded = decodeFdclPayload(encodeFdclFormatList(list));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::FormatList);
    assert(decoded.formatList.bundleId == 101);
    assert(decoded.formatList.offerId == 202);
    assert(decoded.formatList.ownerEpoch == 303);
    assert(decoded.formatList.sequence == 404);
    assert(decoded.formatList.originSessionId == 505);
    assert(decoded.formatList.formats.size() == 1);
    assert(decoded.formatList.formats.front().canonicalFormat == TextPlainUtf8Format);
    assert(decoded.formatList.formats.front().nativeFormatName == "CF_UNICODETEXT");
    assert(decoded.formatList.formats.front().preferredEncoding ==
           TransferEncodingMode::NativePassthrough);
}

void formatListPresentationRoundTrips()
{
    FdclFormatList list = textFormatList();
    list.origin = TransferOrigin::Drag;
    list.presentation = dragPresentation();

    const FdclDecodeResult decoded = decodeFdclPayload(encodeFdclFormatList(list));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::FormatList);
    assert(decoded.formatList.presentation.has_value());
    const TransferPresentation& presentation = decoded.formatList.presentation.value();
    assert(presentation.displayName == "document.txt");
    assert(presentation.itemCount == 1);
    assert(presentation.sourceKind == TransferSourceKind::FileList);
    assert(presentation.icons.size() == 1);
    assert(presentation.icons.front().format == ImagePngFormat);
    assert(presentation.icons.front().sensitive);
    assert(presentation.dragImage.has_value());
    assert(presentation.dragImage->format == ImagePngFormat);
    assert(presentation.dragImage->scale == 2.0);
    assert(presentation.dragImage->hotspotX == 4);
    assert(presentation.allowedActions == (transfer_action::Copy | transfer_action::Link));

    const TransferSourceBundle bundle =
        makeRemoteBundleFromFormatList(decoded.formatList);
    assert(bundle.presentation.has_value());
    assert(bundle.presentation->displayName == "document.txt");
}

void readRequestRoundTrips()
{
    FdclReadFormatRequest request;
    request.bundleId = 101;
    request.offerId = 202;
    request.ownerEpoch = 303;
    request.sourceId = 7;
    request.itemIndex = 0;
    request.formatId = 11;
    request.localFormatToken = 22;
    request.acceptedMaxBytes = 4096;
    request.streamAccepted = false;
    request.requestedEncoding = TransferEncodingMode::CanonicalBytes;
    request.canonicalFormat = TextPlainUtf8Format;

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclReadFormatRequest(request));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::ReadFormatRequest);
    assert(decoded.readRequest.bundleId == 101);
    assert(decoded.readRequest.offerId == 202);
    assert(decoded.readRequest.ownerEpoch == 303);
    assert(decoded.readRequest.sourceId == 7);
    assert(decoded.readRequest.formatId == 11);
    assert(decoded.readRequest.acceptedMaxBytes == 4096);
    assert(decoded.readRequest.requestedEncoding == TransferEncodingMode::CanonicalBytes);
    assert(decoded.readRequest.canonicalFormat == TextPlainUtf8Format);
}

void readResponseRoundTrips()
{
    FdclReadFormatResponse response;
    response.bundleId = 101;
    response.offerId = 202;
    response.ownerEpoch = 303;
    response.sourceId = 7;
    response.itemIndex = 0;
    response.formatId = 11;
    response.encoding = TransferEncodingMode::CanonicalBytes;
    response.canonicalFormat = TextPlainUtf8Format;
    response.bytes = bytes("hello");

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclReadFormatResponse(response));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::ReadFormatResponse);
    assert(decoded.readResponse.bundleId == 101);
    assert(decoded.readResponse.offerId == 202);
    assert(decoded.readResponse.ownerEpoch == 303);
    assert(decoded.readResponse.sourceId == 7);
    assert(decoded.readResponse.formatId == 11);
    assert(decoded.readResponse.encoding == TransferEncodingMode::CanonicalBytes);
    assert(decoded.readResponse.canonicalFormat == TextPlainUtf8Format);
    assert(decoded.readResponse.bytes == bytes("hello"));
}

void fileRangeRequestRoundTrips()
{
    FdclFileRangeRequest request;
    request.bundleId = 101;
    request.offerId = 202;
    request.ownerEpoch = 303;
    request.sourceId = 7;
    request.objectId = 909;
    request.fileIndex = 2;
    request.offset = 4096;
    request.requestedBytes = 8192;

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclFileRangeRequest(request));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::FileRangeRequest);
    assert(decoded.fileRangeRequest.bundleId == 101);
    assert(decoded.fileRangeRequest.offerId == 202);
    assert(decoded.fileRangeRequest.ownerEpoch == 303);
    assert(decoded.fileRangeRequest.sourceId == 7);
    assert(decoded.fileRangeRequest.objectId == 909);
    assert(decoded.fileRangeRequest.fileIndex == 2);
    assert(decoded.fileRangeRequest.offset == 4096);
    assert(decoded.fileRangeRequest.requestedBytes == 8192);
}

void fileRangeRequestAllowsPolicySizedChunks()
{
    FdclFileRangeRequest request;
    request.bundleId = 101;
    request.offerId = 202;
    request.ownerEpoch = 303;
    request.sourceId = 7;
    request.objectId = 909;
    request.fileIndex = 2;
    request.offset = 0;
    request.requestedBytes = 2U * 1024U * 1024U;

    FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclFileRangeRequest(request));
    assert(decoded.ok);
    assert(decoded.fileRangeRequest.requestedBytes == 2U * 1024U * 1024U);

    request.requestedBytes = 4U * 1024U * 1024U + 1U;
    decoded = decodeFdclPayload(encodeFdclFileRangeRequest(request));
    assert(!decoded.ok);
    assert(decoded.error == "file range request identity is invalid");
}

void fileRangeResponseRoundTrips()
{
    FdclFileRangeResponse response;
    response.bundleId = 101;
    response.offerId = 202;
    response.ownerEpoch = 303;
    response.sourceId = 7;
    response.objectId = 909;
    response.fileIndex = 2;
    response.offset = 4096;
    response.endOfFile = true;
    response.bytes = bytes("chunk");

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclFileRangeResponse(response));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::FileRangeResponse);
    assert(decoded.fileRangeResponse.bundleId == 101);
    assert(decoded.fileRangeResponse.offerId == 202);
    assert(decoded.fileRangeResponse.ownerEpoch == 303);
    assert(decoded.fileRangeResponse.sourceId == 7);
    assert(decoded.fileRangeResponse.objectId == 909);
    assert(decoded.fileRangeResponse.fileIndex == 2);
    assert(decoded.fileRangeResponse.offset == 4096);
    assert(decoded.fileRangeResponse.endOfFile);
    assert(decoded.fileRangeResponse.bytes == bytes("chunk"));
}

void fileRangeResponseAllowsPolicySizedChunks()
{
    FdclFileRangeResponse response;
    response.bundleId = 101;
    response.offerId = 202;
    response.ownerEpoch = 303;
    response.sourceId = 7;
    response.objectId = 909;
    response.fileIndex = 2;
    response.offset = 0;
    response.endOfFile = false;
    response.bytes.assign(1024U * 1024U + 1U, 0x42);

    FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclFileRangeResponse(response));
    assert(decoded.ok);
    assert(decoded.fileRangeResponse.bytes.size() == 1024U * 1024U + 1U);

    response.bytes.assign(4U * 1024U * 1024U + 1U, 0x42);
    decoded = decodeFdclPayload(encodeFdclFileRangeResponse(response));
    assert(!decoded.ok);
    assert(decoded.error == "file range response bytes are invalid");
}

void objectLockRoundTrips()
{
    FdclObjectLock lock;
    lock.bundleId = 101;
    lock.offerId = 202;
    lock.ownerEpoch = 303;
    lock.sourceId = 7;
    lock.objectId = 909;
    lock.fileIndex = 2;
    lock.lockId = 10001;
    lock.leaseUsec = 30000000;

    FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclLockObject(lock));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::LockObject);
    assert(decoded.objectLock.bundleId == 101);
    assert(decoded.objectLock.offerId == 202);
    assert(decoded.objectLock.ownerEpoch == 303);
    assert(decoded.objectLock.sourceId == 7);
    assert(decoded.objectLock.objectId == 909);
    assert(decoded.objectLock.fileIndex == 2);
    assert(decoded.objectLock.lockId == 10001);
    assert(decoded.objectLock.leaseUsec == 30000000);

    decoded = decodeFdclPayload(encodeFdclUnlockObject(lock));
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::UnlockObject);
    assert(decoded.objectLock.lockId == 10001);
}

void objectUnlockRequiresLockId()
{
    FdclObjectLock lock;
    lock.bundleId = 101;
    lock.offerId = 202;
    lock.ownerEpoch = 303;
    lock.sourceId = 7;
    lock.objectId = 909;
    lock.fileIndex = 2;
    lock.lockId = 0;

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclUnlockObject(lock));

    assert(!decoded.ok);
    assert(decoded.operation == FdclOperation::UnlockObject);
    assert(decoded.error == "object unlock identity is invalid");
}

DragSurfaceCoordinate dragPoint()
{
    DragSurfaceCoordinate point;
    point.coordinateSpace = DragCoordinateSpace::RemoteLogical;
    point.x = 12;
    point.y = 34;
    point.surfaceWidth = 1920;
    point.surfaceHeight = 1080;
    point.scale = 1.5;
    return point;
}

void dragMessagesRoundTripCoordinatesOnly()
{
    FdclDragStart start;
    start.start.dragSessionId = 77;
    start.start.bundleId = 101;
    start.start.offerId = 202;
    start.start.ownerEpoch = 303;
    start.start.allowedActions = transfer_action::Copy;
    start.start.preferredAction = TransferAction::Copy;
    start.start.start = dragPoint();

    FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclDragStart(start));
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::DragStart);
    assert(decoded.dragStart.start.dragSessionId == 77);
    assert(decoded.dragStart.start.bundleId == 101);
    assert(decoded.dragStart.start.start.x == 12);
    assert(decoded.dragStart.start.start.surfaceWidth == 1920);
    assert(decoded.dragStart.start.start.scale == 1.5);

    FdclDragMove move;
    move.dragSessionId = 77;
    move.point = dragPoint();
    move.point.x = 20;
    move.proposedAction = TransferAction::Copy;
    decoded = decodeFdclPayload(encodeFdclDragMove(move));
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::DragMove);
    assert(decoded.dragMove.dragSessionId == 77);
    assert(decoded.dragMove.point.x == 20);

    FdclDragDrop drop;
    drop.dragSessionId = 77;
    drop.point = dragPoint();
    drop.proposedAction = TransferAction::Copy;
    decoded = decodeFdclPayload(encodeFdclDragDrop(drop));
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::DragDrop);
    assert(decoded.dragDrop.dragSessionId == 77);

    FdclDragCancel cancel;
    cancel.dragSessionId = 77;
    cancel.reason = DragCancelReason::UserCancelled;
    decoded = decodeFdclPayload(encodeFdclDragCancel(cancel));
    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::DragCancel);
    assert(decoded.dragCancel.reason == DragCancelReason::UserCancelled);
}

void errorDetailRoundTrips()
{
    FdclErrorDetail error;
    error.status = protocol::ResponseStatus::DeniedByPolicy;
    error.message = "blocked";

    const FdclDecodeResult decoded =
        decodeFdclPayload(encodeFdclErrorDetail(error));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::ErrorDetail);
    assert(decoded.errorDetail.status == protocol::ResponseStatus::DeniedByPolicy);
    assert(decoded.errorDetail.message == "blocked");
}

void cancelRoundTrips()
{
    FdclCancel cancel;
    cancel.correlationId = 909;
    cancel.bundleId = 101;
    cancel.offerId = 202;
    cancel.ownerEpoch = 303;
    cancel.sourceId = 7;
    cancel.formatId = 11;
    cancel.reason = FdclCancelReason::Timeout;
    cancel.message = "expired";

    const FdclDecodeResult decoded = decodeFdclPayload(encodeFdclCancel(cancel));

    assert(decoded.ok);
    assert(decoded.operation == FdclOperation::Cancel);
    assert(decoded.cancel.correlationId == 909);
    assert(decoded.cancel.bundleId == 101);
    assert(decoded.cancel.reason == FdclCancelReason::Timeout);
    assert(decoded.cancel.message == "expired");
}

void rejectsInvalidMagic()
{
    protocol::ByteBuffer payload = encodeFdclFormatList(textFormatList());
    payload[0] = 0;
    const FdclDecodeResult decoded = decodeFdclPayload(payload);
    assert(!decoded.ok);
    assert(decoded.error == "FDCL magic is invalid");
}

void bundleBuildsRemoteSource()
{
    const TransferSourceBundle bundle = makeRemoteBundleFromFormatList(textFormatList());
    assert(bundle.bundleId == 101);
    assert(bundle.offerId == 202);
    assert(bundle.side == TransferSide::Remote);
    assert(bundle.sources.size() == 1);
    assert(bundle.sources.front()->formats().front().canonicalFormat == TextPlainUtf8Format);
}

} // namespace

int main()
{
    capabilitiesRoundTrips();
    formatListRoundTrips();
    formatListPresentationRoundTrips();
    readRequestRoundTrips();
    readResponseRoundTrips();
    fileRangeRequestRoundTrips();
    fileRangeRequestAllowsPolicySizedChunks();
    fileRangeResponseRoundTrips();
    fileRangeResponseAllowsPolicySizedChunks();
    objectLockRoundTrips();
    objectUnlockRequiresLockId();
    dragMessagesRoundTripCoordinatesOnly();
    errorDetailRoundTrips();
    cancelRoundTrips();
    rejectsInvalidMagic();
    bundleBuildsRemoteSource();
    return 0;
}
