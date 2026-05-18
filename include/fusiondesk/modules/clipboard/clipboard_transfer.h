#ifndef FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_TRANSFER_H
#define FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_TRANSFER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

enum class TransferPlatformFamily : std::uint16_t
{
    Unknown = 0,
    Windows = 1,
    Linux = 2,
    MacOS = 3,
    Android = 4,
    Ios = 5,
    Qt = 6,
    Wayland = 7,
    Gtk = 8
};

enum class TransferFormatClass : std::uint16_t
{
    Unknown = 0,
    PlainText = 1,
    Html = 2,
    Rtf = 3,
    Image = 4,
    FileList = 5,
    OwnerMarker = 6,
    Custom = 7
};

struct NativeTransferFormat
{
    TransferPlatformFamily platform = TransferPlatformFamily::Unknown;
    std::string nativeFormatName;
    std::uint32_t localFormatToken = 0;
};

struct TransferFormatMappingRequest
{
    NativeTransferFormat native;
    TransferFormatId formatId = 0;
    std::uint32_t itemIndex = 0;
    std::uint64_t estimatedBytes = 0;
    bool canInline = true;
    bool canStream = false;
};

struct TransferFormatMappingResult
{
    bool mapped = false;
    TransferFormatDescriptor descriptor;
    TransferFormatClass formatClass = TransferFormatClass::Unknown;
    std::string reason;
};

struct NativeTransferFormatCandidate
{
    NativeTransferFormat native;
    TransferEncodingMode encoding = TransferEncodingMode::Transcoded;
    int priority = 0;
};

class ITransferFormatMapper
{
public:
    virtual ~ITransferFormatMapper() = default;

    virtual TransferFormatMappingResult mapNativeToCanonical(
        const TransferFormatMappingRequest& request) const = 0;
    virtual std::vector<NativeTransferFormatCandidate> nativeCandidates(
        const std::string& canonicalFormat,
        TransferPlatformFamily targetPlatform) const = 0;
    virtual TransferFormatClass formatClass(
        const std::string& canonicalFormat) const = 0;
};

class DefaultTransferFormatMapper : public ITransferFormatMapper
{
public:
    TransferFormatMappingResult mapNativeToCanonical(
        const TransferFormatMappingRequest& request) const override;
    std::vector<NativeTransferFormatCandidate> nativeCandidates(
        const std::string& canonicalFormat,
        TransferPlatformFamily targetPlatform) const override;
    TransferFormatClass formatClass(
        const std::string& canonicalFormat) const override;
};

struct TransferTranscodeRequest
{
    std::string canonicalFormat;
    NativeTransferFormat sourceNative;
    NativeTransferFormat targetNative;
    TransferEncodingMode sourceEncoding = TransferEncodingMode::CanonicalBytes;
    TransferEncodingMode targetEncoding = TransferEncodingMode::CanonicalBytes;
    protocol::ByteBuffer bytes;
};

struct TransferTranscodeResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    TransferEncodingMode encoding = TransferEncodingMode::CanonicalBytes;
    protocol::ByteBuffer bytes;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

struct TransferFileDescriptor
{
    TransferObjectId objectId = 0;
    std::string displayName;
    std::string relativePath;
    std::uint64_t sizeBytes = 0;
    std::uint64_t lastModifiedUnixUsec = 0;
    bool directory = false;
};

struct TransferFileList
{
    std::vector<TransferFileDescriptor> files;
};

struct TransferFileListDecodeResult
{
    bool ok = false;
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    TransferFileList fileList;
    std::string message;
};

struct TransferFileRangeRequest
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

struct TransferFileRangeResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    protocol::ByteBuffer bytes;
    bool endOfFile = false;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

struct TransferObjectLockRequest
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

struct TransferObjectLockResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    TransferObjectLockId lockId = 0;
    std::uint64_t leaseUsec = 0;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

class ITransferFileContentProvider
{
public:
    virtual ~ITransferFileContentProvider() = default;

    virtual TransferFileRangeResult readFileRange(
        const TransferFileRangeRequest& request) = 0;
};

class IClipboardRemoteFileReader
{
public:
    virtual ~IClipboardRemoteFileReader() = default;

    virtual TransferFileRangeResult readRemoteFileRange(
        const TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) = 0;
};

class IClipboardRemoteObjectLocker
{
public:
    virtual ~IClipboardRemoteObjectLocker() = default;

    virtual TransferObjectLockResult lockRemoteObject(
        const TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) = 0;
    virtual TransferObjectLockResult unlockRemoteObject(
        const TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) = 0;
};

class IClipboardChangeMonitor
{
public:
    virtual ~IClipboardChangeMonitor() = default;

    virtual bool hasPendingClipboardChange() const = 0;
    virtual void markClipboardChangeConsumed() = 0;
};

constexpr std::uint64_t DefaultTransferFileRangeChunkBytes =
    4ULL * 1024ULL * 1024ULL;

struct TransferFileDrainOptions
{
    std::uint64_t chunkBytes = DefaultTransferFileRangeChunkBytes;
    std::uint64_t maxTotalBytes = 0;
    std::uint32_t timeoutMs = 30000;
};

struct TransferFileWindowReadOptions
{
    std::uint64_t chunkBytes = DefaultTransferFileRangeChunkBytes;
    std::uint32_t timeoutMs = 30000;
};

struct TransferFileDrainSinkResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

class ITransferFileRangeSink
{
public:
    virtual ~ITransferFileRangeSink() = default;

    virtual TransferFileDrainSinkResult writeRange(
        const TransferFileRangeRequest& request,
        const protocol::ByteBuffer& bytes,
        bool endOfFile) = 0;
};

struct TransferFileDrainResult
{
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    std::uint64_t bytesWritten = 0;
    std::uint64_t chunksWritten = 0;
    bool endOfFile = false;
    std::string message;

    bool ok() const
    {
        return status == protocol::ResponseStatus::Ok;
    }
};

TransferFileDrainResult drainRemoteFileRange(
    IClipboardRemoteFileReader& reader,
    const TransferFileRangeRequest& baseRequest,
    ITransferFileRangeSink& sink,
    const TransferFileDrainOptions& options = TransferFileDrainOptions{});

TransferFileRangeResult readRemoteFileRangeWindow(
    IClipboardRemoteFileReader& reader,
    const TransferFileRangeRequest& baseRequest,
    std::uint64_t offset,
    std::uint64_t requestedBytes,
    const TransferFileWindowReadOptions& options = TransferFileWindowReadOptions{});

std::string sanitizeTransferFileDisplayName(const std::string& value);
std::string sanitizeTransferFileRelativePath(const std::string& value);
protocol::ByteBuffer encodeTransferFileList(const TransferFileList& fileList);
TransferFileListDecodeResult decodeTransferFileList(
    const protocol::ByteBuffer& bytes,
    std::size_t maxFiles = 1024,
    std::size_t maxNameBytes = 255);

class FileGroupTransferSource : public TransferSource
{
public:
    FileGroupTransferSource(TransferSourceId sourceId,
                            TransferFormatDescriptor descriptor,
                            TransferFileList fileList);

    TransferSourceId id() const override;
    std::vector<TransferFormatDescriptor> formats() const override;
    TransferReadResult read(const TransferReadRequest& request) override;
    const TransferFileList& fileList() const;

private:
    TransferSourceId sourceId_ = 0;
    TransferFormatDescriptor descriptor_;
    TransferFileList fileList_;
};

class ITransferTranscoder
{
public:
    virtual ~ITransferTranscoder() = default;

    virtual bool canTranscode(const TransferTranscodeRequest& request) const = 0;
    virtual TransferTranscodeResult transcode(
        const TransferTranscodeRequest& request) const = 0;
};

class IdentityTransferTranscoder : public ITransferTranscoder
{
public:
    bool canTranscode(const TransferTranscodeRequest& request) const override;
    TransferTranscodeResult transcode(
        const TransferTranscodeRequest& request) const override;
};

struct TransferSourceLookup
{
    protocol::ResponseStatus status = protocol::ResponseStatus::NotFound;
    std::string message;
    TransferSourceBundle bundle;
    std::shared_ptr<TransferSource> source;

    bool found() const
    {
        return status == protocol::ResponseStatus::Ok && source != nullptr;
    }
};

class ITransferSourceRegistry
{
public:
    virtual ~ITransferSourceRegistry() = default;

    virtual protocol::ResponseStatus store(TransferSourceBundle bundle) = 0;
    virtual std::optional<TransferSourceBundle> findBundle(
        TransferOfferId offerId) const = 0;
    virtual TransferSourceLookup lookupSource(
        const TransferReadRequest& request) const = 0;
    virtual TransferObjectLockResult lockObject(
        const TransferObjectLockRequest& request) = 0;
    virtual TransferObjectLockResult unlockObject(
        const TransferObjectLockRequest& request) = 0;
    virtual bool clearOffer(TransferOfferId offerId) = 0;
    virtual std::size_t clearAll() = 0;
    virtual std::size_t releaseAllLocks() = 0;
    virtual std::size_t size() const = 0;
    virtual std::size_t lockCount() const = 0;
};

class InMemoryTransferSourceRegistry : public ITransferSourceRegistry
{
public:
    protocol::ResponseStatus store(TransferSourceBundle bundle) override;
    std::optional<TransferSourceBundle> findBundle(
        TransferOfferId offerId) const override;
    TransferSourceLookup lookupSource(
        const TransferReadRequest& request) const override;
    TransferObjectLockResult lockObject(
        const TransferObjectLockRequest& request) override;
    TransferObjectLockResult unlockObject(
        const TransferObjectLockRequest& request) override;
    bool clearOffer(TransferOfferId offerId) override;
    std::size_t clearAll() override;
    std::size_t releaseAllLocks() override;
    std::size_t size() const override;
    std::size_t lockCount() const override;

private:
    struct Entry
    {
        TransferSourceBundle bundle;
        std::vector<TransferObjectLockRequest> locks;
        bool retired = false;
    };

    std::vector<Entry> entries_;
    TransferObjectLockId nextLockId_ = 1;
};

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_TRANSFER_H
