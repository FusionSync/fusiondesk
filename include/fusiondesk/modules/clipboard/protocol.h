#ifndef FUSIONDESK_MODULES_CLIPBOARD_PROTOCOL_H
#define FUSIONDESK_MODULES_CLIPBOARD_PROTOCOL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

enum class FdclOperation : std::uint16_t
{
    Unknown = 0,
    Capabilities = 1,
    FormatList = 2,
    ReadFormatRequest = 3,
    ReadFormatResponse = 4,
    Cancel = 5,
    ErrorDetail = 6,
    FileRangeRequest = 7,
    FileRangeResponse = 8,
    DragStart = 9,
    DragMove = 10,
    DragDrop = 11,
    DragCancel = 12,
    LockObject = 13,
    UnlockObject = 14
};

enum class FdclCancelReason : std::uint16_t
{
    Unknown = 0,
    UserCancelled = 1,
    Timeout = 2,
    StaleOffer = 3,
    ChannelLost = 4,
    PolicyRevoked = 5,
    ReplacedByNewOffer = 6
};

// FDCL payload structs are the protocol schema source of truth. The codec
// must serialize these fields explicitly instead of memcpying them, because
// C++ object layout, padding, alignment, enum size, and host byte order are
// not portable wire contracts.
struct FdclCapabilities
{
    std::uint16_t protocolMajor = 1;
    std::uint16_t protocolMinor = 0;
    std::uint64_t maxInlineBytes = 0;
    std::uint64_t maxStreamBytes = 0;
    TransferActionSet supportedActions = transfer_action::Copy;
    bool supportsStreams = false;
    bool supportsNativePassthrough = true;
    bool supportsPresentationMetadata = false;
    bool supportsDrag = false;
    std::vector<std::string> canonicalFormats;
};

struct FdclFormatRecord
{
    TransferSourceId sourceId = 0;
    std::uint32_t itemIndex = 0;
    TransferFormatId formatId = 0;
    std::uint32_t localFormatToken = 0;
    std::uint64_t estimatedBytes = 0;
    bool canInline = true;
    bool canStream = false;
    TransferEncodingMode preferredEncoding = TransferEncodingMode::CanonicalBytes;
    std::string canonicalFormat;
    std::string nativeFormatName;
};

struct FdclFormatList
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    std::uint64_t sequence = 0;
    TransferOrigin origin = TransferOrigin::Clipboard;
    TransferSide side = TransferSide::Local;
    protocol::SessionId originSessionId = 0;
    PolicyVersion policyVersion = 0;
    std::vector<FdclFormatRecord> formats;
    std::optional<TransferPresentation> presentation;
};

struct FdclReadFormatRequest
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    std::uint32_t itemIndex = 0;
    TransferFormatId formatId = 0;
    std::uint32_t localFormatToken = 0;
    std::uint64_t acceptedMaxBytes = 0;
    bool streamAccepted = false;
    TransferEncodingMode requestedEncoding = TransferEncodingMode::CanonicalBytes;
    std::string canonicalFormat;
};

struct FdclReadFormatResponse
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    std::uint32_t itemIndex = 0;
    TransferFormatId formatId = 0;
    TransferEncodingMode encoding = TransferEncodingMode::CanonicalBytes;
    std::string canonicalFormat;
    protocol::ByteBuffer bytes;
};

struct FdclErrorDetail
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    std::string message;
};

struct FdclFileRangeRequest
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    TransferObjectId objectId = 0;
    std::uint32_t fileIndex = 0;
    std::uint64_t offset = 0;
    std::uint64_t requestedBytes = 0;
};

struct FdclFileRangeResponse
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    TransferObjectId objectId = 0;
    std::uint32_t fileIndex = 0;
    std::uint64_t offset = 0;
    bool endOfFile = false;
    protocol::ByteBuffer bytes;
};

struct FdclObjectLock
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    TransferObjectId objectId = 0;
    std::uint32_t fileIndex = 0;
    TransferObjectLockId lockId = 0;
    std::uint64_t leaseUsec = 0;
};

struct FdclDragStart
{
    DragSessionStart start;
};

struct FdclDragMove
{
    DragSessionId dragSessionId = 0;
    DragSurfaceCoordinate point;
    TransferAction proposedAction = TransferAction::Copy;
};

struct FdclDragDrop
{
    DragSessionId dragSessionId = 0;
    DragSurfaceCoordinate point;
    TransferAction proposedAction = TransferAction::Copy;
};

struct FdclDragCancel
{
    DragSessionId dragSessionId = 0;
    DragCancelReason reason = DragCancelReason::Unknown;
};

struct FdclCancel
{
    protocol::MessageId correlationId = 0;
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    TransferFormatId formatId = 0;
    FdclCancelReason reason = FdclCancelReason::Unknown;
    std::string message;
};

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_PROTOCOL_H
