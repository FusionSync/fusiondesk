#include "fusiondesk/modules/clipboard/fdcl_codec.h"

#include <cstddef>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

constexpr std::uint32_t FdclMagic = 0x4644434c; // "FDCL"
constexpr std::uint16_t FdclMajor = 1;
constexpr std::uint16_t FdclMinor = 0;
constexpr std::size_t HeaderSize = 16;

void appendU16(protocol::ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendU32(protocol::ByteBuffer& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendI32(protocol::ByteBuffer& output, std::int32_t value)
{
    appendU32(output, static_cast<std::uint32_t>(value));
}

void appendU64(protocol::ByteBuffer& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

void appendScale(protocol::ByteBuffer& output, double value)
{
    if (value <= 0.0)
        value = 1.0;
    appendU32(output, static_cast<std::uint32_t>(value * 1000.0));
}

void appendBool(protocol::ByteBuffer& output, bool value)
{
    output.push_back(value ? 1U : 0U);
}

void appendString(protocol::ByteBuffer& output, const std::string& value)
{
    const std::size_t length = value.size();
    appendU16(output, static_cast<std::uint16_t>(length));
    output.insert(output.end(), value.begin(), value.end());
}

protocol::ByteBuffer wrap(FdclOperation operation, const protocol::ByteBuffer& body)
{
    protocol::ByteBuffer output;
    output.reserve(HeaderSize + body.size());
    appendU32(output, FdclMagic);
    appendU16(output, FdclMajor);
    appendU16(output, FdclMinor);
    appendU16(output, static_cast<std::uint16_t>(operation));
    appendU16(output, 0);
    appendU32(output, static_cast<std::uint32_t>(body.size()));
    output.insert(output.end(), body.begin(), body.end());
    return output;
}

void appendDragSurfaceCoordinate(protocol::ByteBuffer& body,
                                 const DragSurfaceCoordinate& point)
{
    appendU16(body, static_cast<std::uint16_t>(point.coordinateSpace));
    appendU16(body, 0);
    appendI32(body, point.x);
    appendI32(body, point.y);
    appendU32(body, point.surfaceWidth);
    appendU32(body, point.surfaceHeight);
    appendScale(body, point.scale);
}

void appendCapabilities(protocol::ByteBuffer& body,
                        const FdclCapabilities& capabilities)
{
    appendU16(body, capabilities.protocolMajor);
    appendU16(body, capabilities.protocolMinor);
    appendU64(body, capabilities.maxInlineBytes);
    appendU64(body, capabilities.maxStreamBytes);
    appendU32(body, capabilities.supportedActions);
    appendBool(body, capabilities.supportsStreams);
    appendBool(body, capabilities.supportsNativePassthrough);
    appendBool(body, capabilities.supportsPresentationMetadata);
    appendBool(body, capabilities.supportsDrag);
    appendU32(body, static_cast<std::uint32_t>(capabilities.canonicalFormats.size()));
    for (const std::string& canonical : capabilities.canonicalFormats)
        appendString(body, canonical);
}

void appendPresentation(protocol::ByteBuffer& body,
                        const TransferPresentation& presentation)
{
    appendString(body, presentation.displayName);
    appendU32(body, presentation.itemCount);
    appendU16(body, static_cast<std::uint16_t>(presentation.sourceKind));
    appendU32(body, presentation.allowedActions);
    appendU16(body, static_cast<std::uint16_t>(presentation.preferredAction));
    appendBool(body, presentation.previewAllowedByPolicy);

    appendU16(body, static_cast<std::uint16_t>(presentation.icons.size()));
    for (const IconRepresentation& icon : presentation.icons) {
        appendString(body, icon.format);
        appendU32(body, icon.width);
        appendU32(body, icon.height);
        appendU64(body, icon.bytes);
        appendBool(body, icon.sensitive);
    }

    appendBool(body, presentation.dragImage.has_value());
    if (presentation.dragImage.has_value()) {
        const DragImage& image = presentation.dragImage.value();
        appendString(body, image.format);
        appendU32(body, image.width);
        appendU32(body, image.height);
        appendScale(body, image.scale);
        appendI32(body, image.hotspotX);
        appendI32(body, image.hotspotY);
        appendBool(body, image.generatedPreview);
        appendBool(body, image.sensitive);
    }
}

void appendFormatRecord(protocol::ByteBuffer& body,
                        const FdclFormatRecord& record)
{
    appendU64(body, record.sourceId);
    appendU32(body, record.itemIndex);
    appendU64(body, record.formatId);
    appendU32(body, record.localFormatToken);
    appendU64(body, record.estimatedBytes);
    appendBool(body, record.canInline);
    appendBool(body, record.canStream);
    appendU16(body, static_cast<std::uint16_t>(record.preferredEncoding));
    appendU16(body, 0);
    appendString(body, record.canonicalFormat);
    appendString(body, record.nativeFormatName);
}

} // namespace

protocol::ByteBuffer encodeFdclCapabilities(const FdclCapabilities& payload)
{
    protocol::ByteBuffer body;
    body.reserve(32 + payload.canonicalFormats.size() * 32);
    appendCapabilities(body, payload);
    return wrap(FdclOperation::Capabilities, body);
}

protocol::ByteBuffer encodeFdclFormatList(const FdclFormatList& payload)
{
    protocol::ByteBuffer body;
    body.reserve(64 + payload.formats.size() * 64);
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sequence);
    appendU16(body, static_cast<std::uint16_t>(payload.origin));
    appendU16(body, static_cast<std::uint16_t>(payload.side));
    appendU64(body, payload.originSessionId);
    appendU64(body, payload.policyVersion);
    appendU32(body, static_cast<std::uint32_t>(payload.formats.size()));
    for (const FdclFormatRecord& record : payload.formats)
        appendFormatRecord(body, record);
    if (payload.presentation.has_value()) {
        appendBool(body, true);
        appendPresentation(body, payload.presentation.value());
    }
    return wrap(FdclOperation::FormatList, body);
}

protocol::ByteBuffer encodeFdclReadFormatRequest(
    const FdclReadFormatRequest& payload)
{
    protocol::ByteBuffer body;
    body.reserve(80 + payload.canonicalFormat.size());
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sourceId);
    appendU32(body, payload.itemIndex);
    appendU64(body, payload.formatId);
    appendU32(body, payload.localFormatToken);
    appendU64(body, payload.acceptedMaxBytes);
    appendBool(body, payload.streamAccepted);
    appendU16(body, static_cast<std::uint16_t>(payload.requestedEncoding));
    appendU16(body, 0);
    appendString(body, payload.canonicalFormat);
    return wrap(FdclOperation::ReadFormatRequest, body);
}

protocol::ByteBuffer encodeFdclReadFormatResponse(
    const FdclReadFormatResponse& payload)
{
    protocol::ByteBuffer body;
    body.reserve(88 + payload.canonicalFormat.size() + payload.bytes.size());
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sourceId);
    appendU32(body, payload.itemIndex);
    appendU64(body, payload.formatId);
    appendU16(body, static_cast<std::uint16_t>(payload.encoding));
    appendU16(body, 0);
    appendString(body, payload.canonicalFormat);
    appendU32(body, static_cast<std::uint32_t>(payload.bytes.size()));
    body.insert(body.end(), payload.bytes.begin(), payload.bytes.end());
    return wrap(FdclOperation::ReadFormatResponse, body);
}

protocol::ByteBuffer encodeFdclErrorDetail(const FdclErrorDetail& payload)
{
    protocol::ByteBuffer body;
    body.reserve(4 + payload.message.size());
    appendU16(body, static_cast<std::uint16_t>(payload.status));
    appendString(body, payload.message);
    return wrap(FdclOperation::ErrorDetail, body);
}

protocol::ByteBuffer encodeFdclFileRangeRequest(
    const FdclFileRangeRequest& payload)
{
    protocol::ByteBuffer body;
    body.reserve(64);
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sourceId);
    appendU64(body, payload.objectId);
    appendU32(body, payload.fileIndex);
    appendU64(body, payload.offset);
    appendU64(body, payload.requestedBytes);
    appendU16(body, 0);
    return wrap(FdclOperation::FileRangeRequest, body);
}

protocol::ByteBuffer encodeFdclFileRangeResponse(
    const FdclFileRangeResponse& payload)
{
    protocol::ByteBuffer body;
    body.reserve(72 + payload.bytes.size());
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sourceId);
    appendU64(body, payload.objectId);
    appendU32(body, payload.fileIndex);
    appendU64(body, payload.offset);
    appendBool(body, payload.endOfFile);
    appendU16(body, 0);
    appendU32(body, static_cast<std::uint32_t>(payload.bytes.size()));
    body.insert(body.end(), payload.bytes.begin(), payload.bytes.end());
    return wrap(FdclOperation::FileRangeResponse, body);
}

namespace {

protocol::ByteBuffer encodeObjectLock(FdclOperation operation,
                                      const FdclObjectLock& payload)
{
    protocol::ByteBuffer body;
    body.reserve(64);
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sourceId);
    appendU64(body, payload.objectId);
    appendU32(body, payload.fileIndex);
    appendU64(body, payload.lockId);
    appendU64(body, payload.leaseUsec);
    appendU16(body, 0);
    return wrap(operation, body);
}

} // namespace

protocol::ByteBuffer encodeFdclLockObject(const FdclObjectLock& payload)
{
    return encodeObjectLock(FdclOperation::LockObject, payload);
}

protocol::ByteBuffer encodeFdclUnlockObject(const FdclObjectLock& payload)
{
    return encodeObjectLock(FdclOperation::UnlockObject, payload);
}

protocol::ByteBuffer encodeFdclDragStart(const FdclDragStart& payload)
{
    protocol::ByteBuffer body;
    body.reserve(64);
    appendU64(body, payload.start.dragSessionId);
    appendU64(body, payload.start.bundleId);
    appendU64(body, payload.start.offerId);
    appendU64(body, payload.start.ownerEpoch);
    appendU32(body, payload.start.allowedActions);
    appendU16(body, static_cast<std::uint16_t>(payload.start.preferredAction));
    appendU16(body, 0);
    appendDragSurfaceCoordinate(body, payload.start.start);
    return wrap(FdclOperation::DragStart, body);
}

protocol::ByteBuffer encodeFdclDragMove(const FdclDragMove& payload)
{
    protocol::ByteBuffer body;
    body.reserve(40);
    appendU64(body, payload.dragSessionId);
    appendU16(body, static_cast<std::uint16_t>(payload.proposedAction));
    appendU16(body, 0);
    appendDragSurfaceCoordinate(body, payload.point);
    return wrap(FdclOperation::DragMove, body);
}

protocol::ByteBuffer encodeFdclDragDrop(const FdclDragDrop& payload)
{
    protocol::ByteBuffer body;
    body.reserve(40);
    appendU64(body, payload.dragSessionId);
    appendU16(body, static_cast<std::uint16_t>(payload.proposedAction));
    appendU16(body, 0);
    appendDragSurfaceCoordinate(body, payload.point);
    return wrap(FdclOperation::DragDrop, body);
}

protocol::ByteBuffer encodeFdclDragCancel(const FdclDragCancel& payload)
{
    protocol::ByteBuffer body;
    body.reserve(16);
    appendU64(body, payload.dragSessionId);
    appendU16(body, static_cast<std::uint16_t>(payload.reason));
    appendU16(body, 0);
    return wrap(FdclOperation::DragCancel, body);
}

protocol::ByteBuffer encodeFdclCancel(const FdclCancel& payload)
{
    protocol::ByteBuffer body;
    body.reserve(64 + payload.message.size());
    appendU64(body, static_cast<std::uint64_t>(payload.correlationId));
    appendU64(body, payload.bundleId);
    appendU64(body, payload.offerId);
    appendU64(body, payload.ownerEpoch);
    appendU64(body, payload.sourceId);
    appendU64(body, payload.formatId);
    appendU16(body, static_cast<std::uint16_t>(payload.reason));
    appendU16(body, 0);
    appendString(body, payload.message);
    return wrap(FdclOperation::Cancel, body);
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
