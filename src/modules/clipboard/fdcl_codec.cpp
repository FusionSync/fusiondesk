#include "fusiondesk/modules/clipboard/fdcl_codec.h"
#include "fdcl_codec_internal.h"

#include <cstddef>
#include <limits>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

constexpr std::uint32_t FdclMagic = 0x4644434c; // "FDCL"
constexpr std::uint16_t FdclMajor = 1;
constexpr std::uint16_t FdclMinor = 0;
constexpr std::size_t HeaderSize = 16;
constexpr std::size_t MaxStringBytes = 4096;
constexpr std::size_t MaxFormatRecords = 256;
constexpr std::size_t MaxInlineBytes = 1024 * 1024;
constexpr std::size_t MaxFileRangeBytes = 4 * 1024 * 1024;
constexpr std::size_t MaxPresentationIcons = 16;

bool readU16(const protocol::ByteBuffer& input, std::size_t& offset, std::uint16_t& value)
{
    if (offset + 2 > input.size())
        return false;
    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[offset]) << 8) |
                                       static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

bool readU32(const protocol::ByteBuffer& input, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4 > input.size())
        return false;
    value = (static_cast<std::uint32_t>(input[offset]) << 24) |
            (static_cast<std::uint32_t>(input[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 2]) << 8) |
            static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool readI32(const protocol::ByteBuffer& input, std::size_t& offset, std::int32_t& value)
{
    std::uint32_t raw = 0;
    if (!readU32(input, offset, raw))
        return false;
    value = static_cast<std::int32_t>(raw);
    return true;
}

bool readU64(const protocol::ByteBuffer& input, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8 > input.size())
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + i]);
    offset += 8;
    return true;
}

bool readScale(const protocol::ByteBuffer& input, std::size_t& offset, double& value)
{
    std::uint32_t scaleMilli = 0;
    if (!readU32(input, offset, scaleMilli))
        return false;
    if (scaleMilli == 0)
        return false;
    value = static_cast<double>(scaleMilli) / 1000.0;
    return true;
}

bool readBool(const protocol::ByteBuffer& input, std::size_t& offset, bool& value)
{
    if (offset + 1 > input.size())
        return false;
    value = input[offset] != 0;
    ++offset;
    return true;
}

bool validEncodingMode(TransferEncodingMode mode)
{
    return mode == TransferEncodingMode::CanonicalBytes ||
           mode == TransferEncodingMode::NativePassthrough ||
           mode == TransferEncodingMode::Transcoded;
}

bool readEncodingMode(const protocol::ByteBuffer& input,
                      std::size_t& offset,
                      TransferEncodingMode& value)
{
    std::uint16_t raw = 0;
    if (!readU16(input, offset, raw))
        return false;

    value = static_cast<TransferEncodingMode>(raw);
    return validEncodingMode(value);
}

bool readString(const protocol::ByteBuffer& input,
                std::size_t& offset,
                std::string& value,
                std::string& error)
{
    std::uint16_t length = 0;
    if (!readU16(input, offset, length)) {
        error = "string length is truncated";
        return false;
    }
    if (length > MaxStringBytes) {
        error = "string length exceeds limit";
        return false;
    }
    if (offset + length > input.size()) {
        error = "string bytes are truncated";
        return false;
    }
    value.assign(reinterpret_cast<const char*>(input.data() + offset), length);
    offset += length;
    return true;
}

bool validFormatName(const std::string& value)
{
    return !value.empty() && value.size() <= MaxStringBytes;
}

bool validSourceKind(TransferSourceKind value)
{
    return value == TransferSourceKind::Unknown ||
           value == TransferSourceKind::Text ||
           value == TransferSourceKind::Image ||
           value == TransferSourceKind::FileList ||
           value == TransferSourceKind::Custom ||
           value == TransferSourceKind::Mixed;
}

bool validAction(TransferAction value)
{
    return value == TransferAction::None ||
           value == TransferAction::Copy ||
           value == TransferAction::Move ||
           value == TransferAction::Link;
}

bool validDragCoordinateSpace(DragCoordinateSpace value)
{
    return value == DragCoordinateSpace::RemoteLogical ||
           value == DragCoordinateSpace::RemotePhysical ||
           value == DragCoordinateSpace::LocalLogical ||
           value == DragCoordinateSpace::LocalPhysical;
}

bool validDragCancelReason(DragCancelReason value)
{
    return value == DragCancelReason::Unknown ||
           value == DragCancelReason::UserCancelled ||
           value == DragCancelReason::SourceLost ||
           value == DragCancelReason::DropFailed ||
           value == DragCancelReason::PolicyDenied ||
           value == DragCancelReason::Timeout;
}

bool readDragSurfaceCoordinate(const protocol::ByteBuffer& body,
                               std::size_t& offset,
                               DragSurfaceCoordinate& point,
                               std::string& error)
{
    std::uint16_t coordinateSpace = 0;
    std::uint16_t reserved = 0;
    if (!readU16(body, offset, coordinateSpace) ||
        !readU16(body, offset, reserved) ||
        !readI32(body, offset, point.x) ||
        !readI32(body, offset, point.y) ||
        !readU32(body, offset, point.surfaceWidth) ||
        !readU32(body, offset, point.surfaceHeight) ||
        !readScale(body, offset, point.scale)) {
        error = "drag coordinate is truncated";
        return false;
    }
    if (reserved != 0) {
        error = "drag coordinate reserved field is not zero";
        return false;
    }
    point.coordinateSpace = static_cast<DragCoordinateSpace>(coordinateSpace);
    if (!validDragCoordinateSpace(point.coordinateSpace)) {
        error = "drag coordinate space is invalid";
        return false;
    }
    return true;
}

bool readCapabilities(const protocol::ByteBuffer& body,
                      std::size_t& offset,
                      FdclCapabilities& capabilities,
                      std::string& error)
{
    std::uint32_t count = 0;
    if (!readU16(body, offset, capabilities.protocolMajor) ||
        !readU16(body, offset, capabilities.protocolMinor) ||
        !readU64(body, offset, capabilities.maxInlineBytes) ||
        !readU64(body, offset, capabilities.maxStreamBytes) ||
        !readU32(body, offset, capabilities.supportedActions) ||
        !readBool(body, offset, capabilities.supportsStreams) ||
        !readBool(body, offset, capabilities.supportsNativePassthrough) ||
        !readBool(body, offset, capabilities.supportsPresentationMetadata) ||
        !readBool(body, offset, capabilities.supportsDrag) ||
        !readU32(body, offset, count)) {
        error = "capabilities body is truncated";
        return false;
    }
    if (capabilities.protocolMajor == 0 || count > MaxFormatRecords) {
        error = "capabilities values are invalid";
        return false;
    }

    capabilities.canonicalFormats.clear();
    capabilities.canonicalFormats.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string canonical;
        if (!readString(body, offset, canonical, error))
            return false;
        if (!validFormatName(canonical)) {
            error = "capabilities canonical format is invalid";
            return false;
        }
        capabilities.canonicalFormats.push_back(std::move(canonical));
    }
    return true;
}

bool readPresentation(const protocol::ByteBuffer& body,
                      std::size_t& offset,
                      TransferPresentation& presentation,
                      std::string& error)
{
    std::uint16_t sourceKind = 0;
    std::uint16_t preferredAction = 0;
    std::uint16_t iconCount = 0;
    bool hasDragImage = false;
    if (!readString(body, offset, presentation.displayName, error) ||
        !readU32(body, offset, presentation.itemCount) ||
        !readU16(body, offset, sourceKind) ||
        !readU32(body, offset, presentation.allowedActions) ||
        !readU16(body, offset, preferredAction) ||
        !readBool(body, offset, presentation.previewAllowedByPolicy) ||
        !readU16(body, offset, iconCount)) {
        if (error.empty())
            error = "presentation body is truncated";
        return false;
    }

    presentation.sourceKind = static_cast<TransferSourceKind>(sourceKind);
    presentation.preferredAction = static_cast<TransferAction>(preferredAction);
    if (!validSourceKind(presentation.sourceKind) ||
        !validAction(presentation.preferredAction) ||
        iconCount > MaxPresentationIcons) {
        error = "presentation values are invalid";
        return false;
    }

    presentation.icons.clear();
    presentation.icons.reserve(iconCount);
    for (std::uint16_t i = 0; i < iconCount; ++i) {
        IconRepresentation icon;
        if (!readString(body, offset, icon.format, error) ||
            !readU32(body, offset, icon.width) ||
            !readU32(body, offset, icon.height) ||
            !readU64(body, offset, icon.bytes) ||
            !readBool(body, offset, icon.sensitive)) {
            if (error.empty())
                error = "presentation icon is truncated";
            return false;
        }
        if (!validFormatName(icon.format)) {
            error = "presentation icon format is invalid";
            return false;
        }
        presentation.icons.push_back(std::move(icon));
    }

    if (!readBool(body, offset, hasDragImage)) {
        error = "presentation drag image flag is truncated";
        return false;
    }
    presentation.dragImage.reset();
    if (hasDragImage) {
        DragImage image;
        if (!readString(body, offset, image.format, error) ||
            !readU32(body, offset, image.width) ||
            !readU32(body, offset, image.height) ||
            !readScale(body, offset, image.scale) ||
            !readI32(body, offset, image.hotspotX) ||
            !readI32(body, offset, image.hotspotY) ||
            !readBool(body, offset, image.generatedPreview) ||
            !readBool(body, offset, image.sensitive)) {
            if (error.empty())
                error = "presentation drag image is truncated";
            return false;
        }
        if (!validFormatName(image.format)) {
            error = "presentation drag image format is invalid";
            return false;
        }
        presentation.dragImage = image;
    }
    return true;
}

bool readFormatRecord(const protocol::ByteBuffer& body,
                      std::size_t& offset,
                      FdclFormatRecord& record,
                      std::string& error)
{
    std::uint16_t reserved = 0;
    if (!readU64(body, offset, record.sourceId) ||
        !readU32(body, offset, record.itemIndex) ||
        !readU64(body, offset, record.formatId) ||
        !readU32(body, offset, record.localFormatToken) ||
        !readU64(body, offset, record.estimatedBytes) ||
        !readBool(body, offset, record.canInline) ||
        !readBool(body, offset, record.canStream) ||
        !readEncodingMode(body, offset, record.preferredEncoding) ||
        !readU16(body, offset, reserved)) {
        error = "format record is truncated";
        return false;
    }
    if (reserved != 0) {
        error = "format record reserved field is not zero";
        return false;
    }
    if (!readString(body, offset, record.canonicalFormat, error) ||
        !readString(body, offset, record.nativeFormatName, error)) {
        return false;
    }
    if (!validFormatName(record.canonicalFormat)) {
        error = "canonical format is invalid";
        return false;
    }
    if (record.sourceId == 0 || record.formatId == 0) {
        error = "format record identity is invalid";
        return false;
    }
    return true;
}

FdclDecodeResult decodeCapabilities(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::Capabilities;
    std::size_t offset = 0;
    if (!readCapabilities(body, offset, result.capabilities, result.error))
        return result;
    if (offset != body.size()) {
        result.error = "capabilities body has trailing bytes";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeFormatList(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::FormatList;
    std::size_t offset = 0;
    std::uint16_t origin = 0;
    std::uint16_t side = 0;
    std::uint32_t count = 0;
    if (!readU64(body, offset, result.formatList.bundleId) ||
        !readU64(body, offset, result.formatList.offerId) ||
        !readU64(body, offset, result.formatList.ownerEpoch) ||
        !readU64(body, offset, result.formatList.sequence) ||
        !readU16(body, offset, origin) ||
        !readU16(body, offset, side) ||
        !readU64(body, offset, result.formatList.originSessionId) ||
        !readU64(body, offset, result.formatList.policyVersion) ||
        !readU32(body, offset, count)) {
        result.error = "format list body is truncated";
        return result;
    }
    if (result.formatList.bundleId == 0 || result.formatList.offerId == 0) {
        result.error = "format list identity is invalid";
        return result;
    }
    if (count == 0 || count > MaxFormatRecords) {
        result.error = "format list count is invalid";
        return result;
    }

    result.formatList.origin = static_cast<TransferOrigin>(origin);
    result.formatList.side = static_cast<TransferSide>(side);
    result.formatList.formats.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        FdclFormatRecord record;
        if (!readFormatRecord(body, offset, record, result.error))
            return result;
        result.formatList.formats.push_back(std::move(record));
    }
    if (offset < body.size()) {
        bool hasPresentation = false;
        if (!readBool(body, offset, hasPresentation)) {
            result.error = "format list presentation flag is truncated";
            return result;
        }
        if (hasPresentation) {
            TransferPresentation presentation;
            if (!readPresentation(body, offset, presentation, result.error))
                return result;
            result.formatList.presentation = std::move(presentation);
        }
    }
    if (offset != body.size()) {
        result.error = "format list has trailing bytes";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeReadRequest(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::ReadFormatRequest;
    std::size_t offset = 0;
    if (!readU64(body, offset, result.readRequest.bundleId) ||
        !readU64(body, offset, result.readRequest.offerId) ||
        !readU64(body, offset, result.readRequest.ownerEpoch) ||
        !readU64(body, offset, result.readRequest.sourceId) ||
        !readU32(body, offset, result.readRequest.itemIndex) ||
        !readU64(body, offset, result.readRequest.formatId) ||
        !readU32(body, offset, result.readRequest.localFormatToken) ||
        !readU64(body, offset, result.readRequest.acceptedMaxBytes) ||
        !readBool(body, offset, result.readRequest.streamAccepted) ||
        !readEncodingMode(body, offset, result.readRequest.requestedEncoding)) {
        result.error = "read request body is truncated";
        return result;
    }
    std::uint16_t reserved = 0;
    if (!readU16(body, offset, reserved)) {
        result.error = "read request body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "read request reserved field is not zero";
        return result;
    }
    if (!readString(body, offset, result.readRequest.canonicalFormat, result.error))
        return result;
    if (offset != body.size()) {
        result.error = "read request has trailing bytes";
        return result;
    }
    if (result.readRequest.bundleId == 0 ||
        result.readRequest.offerId == 0 ||
        result.readRequest.ownerEpoch == 0 ||
        result.readRequest.sourceId == 0 ||
        result.readRequest.formatId == 0 ||
        !validFormatName(result.readRequest.canonicalFormat)) {
        result.error = "read request identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeReadResponse(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::ReadFormatResponse;
    std::size_t offset = 0;
    std::uint32_t bytes = 0;
    if (!readU64(body, offset, result.readResponse.bundleId) ||
        !readU64(body, offset, result.readResponse.offerId) ||
        !readU64(body, offset, result.readResponse.ownerEpoch) ||
        !readU64(body, offset, result.readResponse.sourceId) ||
        !readU32(body, offset, result.readResponse.itemIndex) ||
        !readU64(body, offset, result.readResponse.formatId) ||
        !readEncodingMode(body, offset, result.readResponse.encoding)) {
        result.error = "read response body is truncated";
        return result;
    }
    std::uint16_t reserved = 0;
    if (!readU16(body, offset, reserved)) {
        result.error = "read response body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "read response reserved field is not zero";
        return result;
    }
    if (!readString(body, offset, result.readResponse.canonicalFormat, result.error))
        return result;
    if (!readU32(body, offset, bytes)) {
        result.error = "read response byte length is truncated";
        return result;
    }
    if (bytes > MaxInlineBytes || offset + bytes > body.size()) {
        result.error = "read response bytes are invalid";
        return result;
    }
    result.readResponse.bytes.assign(body.begin() + static_cast<std::ptrdiff_t>(offset),
                                     body.begin() + static_cast<std::ptrdiff_t>(offset + bytes));
    offset += bytes;
    if (offset != body.size()) {
        result.error = "read response has trailing bytes";
        return result;
    }
    if (result.readResponse.bundleId == 0 ||
        result.readResponse.offerId == 0 ||
        result.readResponse.ownerEpoch == 0 ||
        result.readResponse.sourceId == 0 ||
        result.readResponse.formatId == 0 ||
        !validFormatName(result.readResponse.canonicalFormat)) {
        result.error = "read response identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeFileRangeRequest(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::FileRangeRequest;
    std::size_t offset = 0;
    std::uint16_t reserved = 0;
    if (!readU64(body, offset, result.fileRangeRequest.bundleId) ||
        !readU64(body, offset, result.fileRangeRequest.offerId) ||
        !readU64(body, offset, result.fileRangeRequest.ownerEpoch) ||
        !readU64(body, offset, result.fileRangeRequest.sourceId) ||
        !readU64(body, offset, result.fileRangeRequest.objectId) ||
        !readU32(body, offset, result.fileRangeRequest.fileIndex) ||
        !readU64(body, offset, result.fileRangeRequest.offset) ||
        !readU64(body, offset, result.fileRangeRequest.requestedBytes) ||
        !readU16(body, offset, reserved)) {
        result.error = "file range request body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "file range request reserved field is not zero";
        return result;
    }
    if (offset != body.size()) {
        result.error = "file range request has trailing bytes";
        return result;
    }
    if (result.fileRangeRequest.bundleId == 0 ||
        result.fileRangeRequest.offerId == 0 ||
        result.fileRangeRequest.ownerEpoch == 0 ||
        result.fileRangeRequest.sourceId == 0 ||
        result.fileRangeRequest.objectId == 0 ||
        result.fileRangeRequest.requestedBytes == 0 ||
        result.fileRangeRequest.requestedBytes > MaxFileRangeBytes) {
        result.error = "file range request identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeFileRangeResponse(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::FileRangeResponse;
    std::size_t offset = 0;
    std::uint16_t reserved = 0;
    std::uint32_t bytes = 0;
    if (!readU64(body, offset, result.fileRangeResponse.bundleId) ||
        !readU64(body, offset, result.fileRangeResponse.offerId) ||
        !readU64(body, offset, result.fileRangeResponse.ownerEpoch) ||
        !readU64(body, offset, result.fileRangeResponse.sourceId) ||
        !readU64(body, offset, result.fileRangeResponse.objectId) ||
        !readU32(body, offset, result.fileRangeResponse.fileIndex) ||
        !readU64(body, offset, result.fileRangeResponse.offset) ||
        !readBool(body, offset, result.fileRangeResponse.endOfFile) ||
        !readU16(body, offset, reserved) ||
        !readU32(body, offset, bytes)) {
        result.error = "file range response body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "file range response reserved field is not zero";
        return result;
    }
    if (bytes > MaxFileRangeBytes || offset + bytes > body.size()) {
        result.error = "file range response bytes are invalid";
        return result;
    }
    result.fileRangeResponse.bytes.assign(
        body.begin() + static_cast<std::ptrdiff_t>(offset),
        body.begin() + static_cast<std::ptrdiff_t>(offset + bytes));
    offset += bytes;
    if (offset != body.size()) {
        result.error = "file range response has trailing bytes";
        return result;
    }
    if (result.fileRangeResponse.bundleId == 0 ||
        result.fileRangeResponse.offerId == 0 ||
        result.fileRangeResponse.ownerEpoch == 0 ||
        result.fileRangeResponse.sourceId == 0 ||
        result.fileRangeResponse.objectId == 0) {
        result.error = "file range response identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeDragStart(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::DragStart;
    std::size_t offset = 0;
    std::uint16_t preferredAction = 0;
    std::uint16_t reserved = 0;
    if (!readU64(body, offset, result.dragStart.start.dragSessionId) ||
        !readU64(body, offset, result.dragStart.start.bundleId) ||
        !readU64(body, offset, result.dragStart.start.offerId) ||
        !readU64(body, offset, result.dragStart.start.ownerEpoch) ||
        !readU32(body, offset, result.dragStart.start.allowedActions) ||
        !readU16(body, offset, preferredAction) ||
        !readU16(body, offset, reserved)) {
        result.error = "drag start body is truncated";
        return result;
    }
    result.dragStart.start.preferredAction =
        static_cast<TransferAction>(preferredAction);
    if (reserved != 0) {
        result.error = "drag start reserved field is not zero";
        return result;
    }
    if (!validAction(result.dragStart.start.preferredAction)) {
        result.error = "drag start action is invalid";
        return result;
    }
    if (!readDragSurfaceCoordinate(body,
                                   offset,
                                   result.dragStart.start.start,
                                   result.error)) {
        return result;
    }
    if (offset != body.size()) {
        result.error = "drag start has trailing bytes";
        return result;
    }
    if (result.dragStart.start.dragSessionId == 0 ||
        result.dragStart.start.bundleId == 0 ||
        result.dragStart.start.offerId == 0 ||
        result.dragStart.start.ownerEpoch == 0) {
        result.error = "drag start identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeDragPoint(const protocol::ByteBuffer& body,
                                 FdclOperation operation)
{
    FdclDecodeResult result;
    result.operation = operation;
    std::size_t offset = 0;
    std::uint16_t proposedAction = 0;
    std::uint16_t reserved = 0;
    DragSessionId dragSessionId = 0;
    DragSurfaceCoordinate point;
    if (!readU64(body, offset, dragSessionId) ||
        !readU16(body, offset, proposedAction) ||
        !readU16(body, offset, reserved)) {
        result.error = "drag point body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "drag point reserved field is not zero";
        return result;
    }
    const TransferAction action = static_cast<TransferAction>(proposedAction);
    if (!validAction(action)) {
        result.error = "drag point action is invalid";
        return result;
    }
    if (!readDragSurfaceCoordinate(body, offset, point, result.error))
        return result;
    if (offset != body.size()) {
        result.error = "drag point has trailing bytes";
        return result;
    }
    if (dragSessionId == 0) {
        result.error = "drag point identity is invalid";
        return result;
    }

    if (operation == FdclOperation::DragMove) {
        result.dragMove.dragSessionId = dragSessionId;
        result.dragMove.proposedAction = action;
        result.dragMove.point = point;
    } else {
        result.dragDrop.dragSessionId = dragSessionId;
        result.dragDrop.proposedAction = action;
        result.dragDrop.point = point;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeDragCancel(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::DragCancel;
    std::size_t offset = 0;
    std::uint16_t reason = 0;
    std::uint16_t reserved = 0;
    if (!readU64(body, offset, result.dragCancel.dragSessionId) ||
        !readU16(body, offset, reason) ||
        !readU16(body, offset, reserved)) {
        result.error = "drag cancel body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "drag cancel reserved field is not zero";
        return result;
    }
    result.dragCancel.reason = static_cast<DragCancelReason>(reason);
    if (!validDragCancelReason(result.dragCancel.reason)) {
        result.error = "drag cancel reason is invalid";
        return result;
    }
    if (offset != body.size()) {
        result.error = "drag cancel has trailing bytes";
        return result;
    }
    if (result.dragCancel.dragSessionId == 0) {
        result.error = "drag cancel identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeErrorDetail(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::ErrorDetail;
    std::size_t offset = 0;
    std::uint16_t status = 0;
    if (!readU16(body, offset, status)) {
        result.error = "error detail status is truncated";
        return result;
    }
    result.errorDetail.status = static_cast<protocol::ResponseStatus>(status);
    if (!readString(body, offset, result.errorDetail.message, result.error))
        return result;
    if (offset != body.size()) {
        result.error = "error detail has trailing bytes";
        return result;
    }
    result.ok = true;
    return result;
}

FdclDecodeResult decodeCancel(const protocol::ByteBuffer& body)
{
    FdclDecodeResult result;
    result.operation = FdclOperation::Cancel;
    std::size_t offset = 0;
    std::uint64_t correlationId = 0;
    std::uint16_t reason = 0;
    std::uint16_t reserved = 0;
    if (!readU64(body, offset, correlationId) ||
        !readU64(body, offset, result.cancel.bundleId) ||
        !readU64(body, offset, result.cancel.offerId) ||
        !readU64(body, offset, result.cancel.ownerEpoch) ||
        !readU64(body, offset, result.cancel.sourceId) ||
        !readU64(body, offset, result.cancel.formatId) ||
        !readU16(body, offset, reason) ||
        !readU16(body, offset, reserved)) {
        result.error = "cancel body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "cancel reserved field is not zero";
        return result;
    }
    result.cancel.correlationId = static_cast<protocol::MessageId>(correlationId);
    result.cancel.reason = static_cast<FdclCancelReason>(reason);
    if (!readString(body, offset, result.cancel.message, result.error))
        return result;
    if (offset != body.size()) {
        result.error = "cancel body has trailing bytes";
        return result;
    }
    if (result.cancel.correlationId == 0 ||
        result.cancel.bundleId == 0 ||
        result.cancel.offerId == 0 ||
        result.cancel.ownerEpoch == 0) {
        result.error = "cancel identity is invalid";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace

FdclDecodeResult decodeFdclPayload(const protocol::ByteBuffer& payload)
{
    FdclDecodeResult result;
    if (payload.size() < HeaderSize) {
        result.error = "FDCL header is truncated";
        return result;
    }

    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t operation = 0;
    std::uint16_t flags = 0;
    std::uint32_t length = 0;
    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, major) ||
        !readU16(payload, offset, minor) ||
        !readU16(payload, offset, operation) ||
        !readU16(payload, offset, flags) ||
        !readU32(payload, offset, length)) {
        result.error = "FDCL header is truncated";
        return result;
    }
    if (magic != FdclMagic) {
        result.error = "FDCL magic is invalid";
        return result;
    }
    if (major != FdclMajor || minor != FdclMinor) {
        result.error = "FDCL version is unsupported";
        return result;
    }
    if (flags != 0) {
        result.error = "FDCL flags are unsupported";
        return result;
    }
    if (payload.size() - HeaderSize != length) {
        result.error = "FDCL payload length is invalid";
        return result;
    }

    protocol::ByteBuffer body(payload.begin() + static_cast<std::ptrdiff_t>(HeaderSize),
                              payload.end());
    const FdclOperation op = static_cast<FdclOperation>(operation);
    switch (op) {
    case FdclOperation::Capabilities:
        return decodeCapabilities(body);
    case FdclOperation::FormatList:
        return decodeFormatList(body);
    case FdclOperation::ReadFormatRequest:
        return decodeReadRequest(body);
    case FdclOperation::ReadFormatResponse:
        return decodeReadResponse(body);
    case FdclOperation::ErrorDetail:
        return decodeErrorDetail(body);
    case FdclOperation::FileRangeRequest:
        return decodeFileRangeRequest(body);
    case FdclOperation::FileRangeResponse:
        return decodeFileRangeResponse(body);
    case FdclOperation::LockObject:
        return decodeFdclObjectLockBody(body, FdclOperation::LockObject);
    case FdclOperation::UnlockObject:
        return decodeFdclObjectLockBody(body, FdclOperation::UnlockObject);
    case FdclOperation::DragStart:
        return decodeDragStart(body);
    case FdclOperation::DragMove:
        return decodeDragPoint(body, FdclOperation::DragMove);
    case FdclOperation::DragDrop:
        return decodeDragPoint(body, FdclOperation::DragDrop);
    case FdclOperation::DragCancel:
        return decodeDragCancel(body);
    case FdclOperation::Cancel:
        return decodeCancel(body);
    default:
        result.operation = op;
        result.error = "FDCL operation is unsupported";
        return result;
    }
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
