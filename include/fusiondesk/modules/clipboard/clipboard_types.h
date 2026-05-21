#ifndef FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_TYPES_H
#define FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_TYPES_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

using TransferBundleId = std::uint64_t;
using TransferOfferId = std::uint64_t;
using TransferSourceId = std::uint64_t;
using TransferFormatId = std::uint64_t;
using TransferObjectId = std::uint64_t;
using TransferObjectLockId = std::uint64_t;
using DragSessionId = std::uint64_t;
using PolicyVersion = std::uint64_t;
using ClipboardCallbackTask = std::function<void()>;

constexpr const char* TextPlainUtf8Format = "text/plain;charset=utf-8";
constexpr const char* TextHtmlFormat = "text/html";
constexpr const char* TextRtfFormat = "text/rtf";
constexpr const char* ImagePngFormat = "image/png";
constexpr const char* ImageDibFormat = "image/x-dib";
constexpr const char* FdclFileListFormat = "application/x-fdcl-file-list";
constexpr const char* FdclOwnerMarkerFormat = "application/x-fdcl-owner-marker";

enum class TransferOrigin : std::uint16_t
{
    Clipboard = 1,
    Drag = 2,
    Drop = 3,
    RemoteOffer = 4
};

enum class TransferSide : std::uint16_t
{
    Local = 1,
    Remote = 2
};

enum class TransferEncodingMode : std::uint16_t
{
    CanonicalBytes = 1,
    NativePassthrough = 2,
    Transcoded = 3
};

enum class TransferAction : std::uint16_t
{
    None = 0,
    Copy = 1,
    Move = 2,
    Link = 3
};

using TransferActionSet = std::uint32_t;

namespace transfer_action {
constexpr TransferActionSet None = 0x00000000U;
constexpr TransferActionSet Copy = 0x00000001U;
constexpr TransferActionSet Move = 0x00000002U;
constexpr TransferActionSet Link = 0x00000004U;
} // namespace transfer_action

enum class TransferSourceKind : std::uint16_t
{
    Unknown = 0,
    Text = 1,
    Image = 2,
    FileList = 3,
    Custom = 4,
    Mixed = 5
};

struct TransferFormatDescriptor
{
    std::string canonicalFormat;
    std::string nativeFormatName;
    std::uint32_t localFormatToken = 0;
    TransferFormatId formatId = 0;
    std::uint32_t itemIndex = 0;
    std::uint64_t estimatedBytes = 0;
    bool canInline = true;
    bool canStream = false;
    TransferEncodingMode preferredEncoding = TransferEncodingMode::CanonicalBytes;
};

struct TransferReadRequest
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferSourceId sourceId = 0;
    std::uint32_t itemIndex = 0;
    TransferFormatId formatId = 0;
    std::uint32_t localFormatToken = 0;
    std::string canonicalFormat;
    std::uint64_t acceptedMaxBytes = 0;
    bool streamAccepted = false;
    TransferEncodingMode requestedEncoding = TransferEncodingMode::CanonicalBytes;
};

struct TransferReadResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    std::string canonicalFormat;
    TransferEncodingMode encoding = TransferEncodingMode::CanonicalBytes;
    protocol::ByteBuffer bytes;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

class IClipboardCallbackDispatcher
{
public:
    virtual ~IClipboardCallbackDispatcher() = default;

    virtual bool postClipboardTask(ClipboardCallbackTask task) = 0;
    virtual bool runClipboardTaskAndWait(ClipboardCallbackTask task,
                                         std::uint32_t timeoutMs) = 0;
};

class TransferSource
{
public:
    virtual ~TransferSource() = default;

    virtual TransferSourceId id() const = 0;
    virtual std::vector<TransferFormatDescriptor> formats() const = 0;
    virtual TransferReadResult read(const TransferReadRequest& request) = 0;
};

struct IconRepresentation
{
    std::string format;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t bytes = 0;
    bool sensitive = false;
};

struct DragImage
{
    std::string format;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double scale = 1.0;
    std::int32_t hotspotX = 0;
    std::int32_t hotspotY = 0;
    bool generatedPreview = false;
    bool sensitive = false;
};

enum class DragCoordinateSpace : std::uint16_t
{
    RemoteLogical = 1,
    RemotePhysical = 2,
    LocalLogical = 3,
    LocalPhysical = 4
};

enum class DragCancelReason : std::uint16_t
{
    Unknown = 0,
    UserCancelled = 1,
    SourceLost = 2,
    DropFailed = 3,
    PolicyDenied = 4,
    Timeout = 5
};

struct DragSurfaceCoordinate
{
    DragCoordinateSpace coordinateSpace = DragCoordinateSpace::RemoteLogical;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t surfaceWidth = 0;
    std::uint32_t surfaceHeight = 0;
    double scale = 1.0;
};

struct DragSessionStart
{
    DragSessionId dragSessionId = 0;
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    TransferActionSet allowedActions = transfer_action::Copy;
    TransferAction preferredAction = TransferAction::Copy;
    DragSurfaceCoordinate start;
};

struct TransferPresentation
{
    std::string displayName;
    std::uint32_t itemCount = 0;
    TransferSourceKind sourceKind = TransferSourceKind::Unknown;
    std::vector<IconRepresentation> icons;
    std::optional<DragImage> dragImage;
    TransferActionSet allowedActions = transfer_action::Copy;
    TransferAction preferredAction = TransferAction::Copy;
    bool previewAllowedByPolicy = false;
};

struct TransferSourceBundle
{
    TransferBundleId bundleId = 0;
    TransferOfferId offerId = 0;
    std::uint64_t ownerEpoch = 0;
    std::uint64_t sequence = 0;
    TransferOrigin origin = TransferOrigin::Clipboard;
    TransferSide side = TransferSide::Local;
    protocol::SessionId originSessionId = 0;
    PolicyVersion policyVersion = 0;
    std::uint64_t createdMonotonicUsec = 0;
    std::vector<std::shared_ptr<TransferSource>> sources;
    std::optional<TransferPresentation> presentation;
};

using ClipboardSource = TransferSource;
using ClipboardSourceBundle = TransferSourceBundle;

struct ClipboardSnapshot
{
    std::uint64_t ownerEpoch = 0;
    std::uint64_t sequence = 0;
    TransferSourceBundle bundle;
};

struct ClipboardPublishRequest
{
    TransferSourceBundle bundle;
};

class IClipboardEndpoint
{
public:
    virtual ~IClipboardEndpoint() = default;

    virtual ClipboardSnapshot snapshot() = 0;
    virtual protocol::ResponseStatus publishBundle(const ClipboardPublishRequest& request) = 0;
    virtual protocol::ResponseStatus clearPublishedBundle(TransferOfferId offerId) = 0;
};

class IClipboardRemoteReader
{
public:
    virtual ~IClipboardRemoteReader() = default;

    virtual TransferReadResult readRemoteFormat(
        const TransferReadRequest& request,
        std::uint32_t timeoutMs) = 0;
};

class IRemoteDragCoordinateSink
{
public:
    virtual ~IRemoteDragCoordinateSink() = default;

    virtual protocol::ResponseStatus dragStart(
        const DragSessionStart& start) = 0;
    virtual protocol::ResponseStatus dragMove(
        DragSessionId dragSessionId,
        const DragSurfaceCoordinate& point,
        TransferAction proposedAction) = 0;
    virtual protocol::ResponseStatus dragDrop(
        DragSessionId dragSessionId,
        const DragSurfaceCoordinate& point,
        TransferAction proposedAction) = 0;
    virtual protocol::ResponseStatus dragCancel(
        DragSessionId dragSessionId,
        DragCancelReason reason) = 0;
};

struct MaterializedTransferEntry
{
    TransferFormatDescriptor descriptor;
    protocol::ByteBuffer bytes;
};

class MaterializedTransferSource : public TransferSource
{
public:
    MaterializedTransferSource(TransferSourceId sourceId,
                               std::vector<MaterializedTransferEntry> entries);

    TransferSourceId id() const override;
    std::vector<TransferFormatDescriptor> formats() const override;
    TransferReadResult read(const TransferReadRequest& request) override;

private:
    TransferSourceId sourceId_ = 0;
    std::vector<MaterializedTransferEntry> entries_;
};

class RemoteFdclTransferSource : public TransferSource
{
public:
    RemoteFdclTransferSource(TransferSourceId sourceId,
                             std::vector<TransferFormatDescriptor> formats);

    TransferSourceId id() const override;
    std::vector<TransferFormatDescriptor> formats() const override;
    TransferReadResult read(const TransferReadRequest& request) override;

private:
    TransferSourceId sourceId_ = 0;
    std::vector<TransferFormatDescriptor> formats_;
};

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_TYPES_H
