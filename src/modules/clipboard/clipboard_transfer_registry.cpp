#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

TransferObjectLockResult objectLockError(protocol::ResponseStatus status,
                                         std::string message)
{
    TransferObjectLockResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

TransferObjectLockResult validateFileObjectForLock(
    TransferSource& source,
    const TransferObjectLockRequest& request)
{
    bool sawFileList = false;
    for (const TransferFormatDescriptor& descriptor : source.formats()) {
        if (descriptor.canonicalFormat != FdclFileListFormat)
            continue;

        sawFileList = true;
        if (!descriptor.canStream) {
            return objectLockError(
                protocol::ResponseStatus::Unsupported,
                "transfer file object does not allow streaming");
        }

        TransferReadRequest read;
        read.bundleId = request.bundleId;
        read.offerId = request.offerId;
        read.ownerEpoch = request.ownerEpoch;
        read.sourceId = request.sourceId;
        read.itemIndex = descriptor.itemIndex;
        read.formatId = descriptor.formatId;
        read.localFormatToken = descriptor.localFormatToken;
        read.canonicalFormat = FdclFileListFormat;
        read.acceptedMaxBytes = std::numeric_limits<std::uint64_t>::max();
        read.requestedEncoding = TransferEncodingMode::CanonicalBytes;

        TransferReadResult fileListBytes = source.read(read);
        if (!fileListBytes.ok()) {
            return objectLockError(
                fileListBytes.status,
                fileListBytes.message.empty()
                    ? "transfer file list is not readable"
                    : fileListBytes.message);
        }

        const TransferFileListDecodeResult decoded =
            decodeTransferFileList(fileListBytes.bytes);
        if (!decoded.ok)
            return objectLockError(decoded.status, decoded.message);
        if (request.fileIndex >= decoded.fileList.files.size()) {
            return objectLockError(protocol::ResponseStatus::NotFound,
                                   "transfer file index is not found");
        }
        if (decoded.fileList.files[request.fileIndex].objectId !=
            request.objectId) {
            return objectLockError(protocol::ResponseStatus::Conflict,
                                   "transfer file object identity is stale");
        }

        TransferObjectLockResult result;
        result.status = protocol::ResponseStatus::Ok;
        return result;
    }

    return objectLockError(
        sawFileList ? protocol::ResponseStatus::Unsupported
                    : protocol::ResponseStatus::NotFound,
        sawFileList ? "transfer file object is unsupported"
                    : "transfer file-list format is not found");
}

} // namespace

protocol::ResponseStatus InMemoryTransferSourceRegistry::store(
    TransferSourceBundle bundle)
{
    if (bundle.bundleId == 0 || bundle.offerId == 0)
        return protocol::ResponseStatus::InvalidArgument;

    clearOffer(bundle.offerId);
    const auto blocked = std::find_if(
        entries_.begin(),
        entries_.end(),
        [&bundle](const Entry& entry) {
            return entry.bundle.offerId == bundle.offerId;
        });
    if (blocked != entries_.end())
        return protocol::ResponseStatus::Conflict;

    Entry entry;
    entry.bundle = std::move(bundle);
    entries_.push_back(std::move(entry));
    return protocol::ResponseStatus::Ok;
}

std::optional<TransferSourceBundle> InMemoryTransferSourceRegistry::findBundle(
    TransferOfferId offerId) const
{
    for (const Entry& entry : entries_) {
        if (entry.bundle.offerId == offerId)
            return entry.bundle;
    }
    return std::nullopt;
}

TransferSourceLookup InMemoryTransferSourceRegistry::lookupSource(
    const TransferReadRequest& request) const
{
    TransferSourceLookup result;
    if (request.bundleId == 0 ||
        request.offerId == 0 ||
        request.ownerEpoch == 0) {
        result.status = protocol::ResponseStatus::InvalidArgument;
        result.message = "transfer lookup identity is invalid";
        return result;
    }

    std::optional<TransferSourceBundle> bundle = findBundle(request.offerId);
    if (!bundle.has_value()) {
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "transfer offer is not found";
        return result;
    }

    if (bundle->bundleId != request.bundleId ||
        bundle->ownerEpoch != request.ownerEpoch) {
        result.status = protocol::ResponseStatus::Conflict;
        result.message = "transfer offer identity is stale";
        return result;
    }

    for (const std::shared_ptr<TransferSource>& source : bundle->sources) {
        if (source != nullptr && source->id() == request.sourceId) {
            result.status = protocol::ResponseStatus::Ok;
            result.bundle = *bundle;
            result.source = source;
            return result;
        }
    }

    result.status = protocol::ResponseStatus::NotFound;
    result.message = "transfer source is not found";
    return result;
}

bool InMemoryTransferSourceRegistry::clearOffer(TransferOfferId offerId)
{
    bool cleared = false;
    for (Entry& entry : entries_) {
        if (entry.bundle.offerId == offerId) {
            entry.retired = true;
            cleared = true;
        }
    }

    entries_.erase(
        std::remove_if(entries_.begin(),
                       entries_.end(),
                       [](const Entry& entry) {
                           return entry.retired && entry.locks.empty();
                       }),
        entries_.end());
    return cleared;
}

std::size_t InMemoryTransferSourceRegistry::clearAll()
{
    const std::size_t oldSize = entries_.size();
    for (Entry& entry : entries_)
        entry.retired = true;

    entries_.erase(
        std::remove_if(entries_.begin(),
                       entries_.end(),
                       [](const Entry& entry) {
                           return entry.locks.empty();
                       }),
        entries_.end());
    return oldSize;
}

std::size_t InMemoryTransferSourceRegistry::releaseAllLocks()
{
    std::size_t released = 0;
    for (Entry& entry : entries_) {
        released += entry.locks.size();
        entry.locks.clear();
    }

    entries_.erase(
        std::remove_if(entries_.begin(),
                       entries_.end(),
                       [](const Entry& entry) {
                           return entry.retired;
                       }),
        entries_.end());
    return released;
}

std::size_t InMemoryTransferSourceRegistry::size() const
{
    return entries_.size();
}

TransferObjectLockResult InMemoryTransferSourceRegistry::lockObject(
    const TransferObjectLockRequest& request)
{
    if (request.bundleId == 0 ||
        request.offerId == 0 ||
        request.ownerEpoch == 0 ||
        request.sourceId == 0 ||
        request.objectId == 0) {
        return objectLockError(protocol::ResponseStatus::InvalidArgument,
                               "transfer object lock identity is invalid");
    }

    for (Entry& entry : entries_) {
        if (entry.bundle.offerId != request.offerId)
            continue;

        if (entry.retired) {
            return objectLockError(protocol::ResponseStatus::Conflict,
                                   "transfer object lock offer is retired");
        }

        if (entry.bundle.bundleId != request.bundleId ||
            entry.bundle.ownerEpoch != request.ownerEpoch) {
            return objectLockError(protocol::ResponseStatus::Conflict,
                                   "transfer object lock offer is stale");
        }

        for (const TransferObjectLockRequest& lock : entry.locks) {
            if (request.lockId != 0 && lock.lockId == request.lockId) {
                return objectLockError(
                    protocol::ResponseStatus::Conflict,
                    "transfer object lock id is already active");
            }
        }

        for (const std::shared_ptr<TransferSource>& source :
             entry.bundle.sources) {
            if (source == nullptr || source->id() != request.sourceId)
                continue;

            TransferObjectLockResult validation =
                validateFileObjectForLock(*source, request);
            if (!validation.ok())
                return validation;

            TransferObjectLockRequest lock = request;
            if (lock.lockId == 0) {
                if (nextLockId_ == 0)
                    nextLockId_ = 1;
                lock.lockId = nextLockId_++;
            }
            entry.locks.push_back(lock);

            validation.lockId = lock.lockId;
            validation.leaseUsec = lock.leaseUsec;
            return validation;
        }

        return objectLockError(protocol::ResponseStatus::NotFound,
                               "transfer object lock source is not found");
    }

    return objectLockError(protocol::ResponseStatus::NotFound,
                           "transfer object lock offer is not found");
}

TransferObjectLockResult InMemoryTransferSourceRegistry::unlockObject(
    const TransferObjectLockRequest& request)
{
    if (request.bundleId == 0 ||
        request.offerId == 0 ||
        request.ownerEpoch == 0 ||
        request.sourceId == 0 ||
        request.objectId == 0 ||
        request.lockId == 0) {
        return objectLockError(protocol::ResponseStatus::InvalidArgument,
                               "transfer object unlock identity is invalid");
    }

    for (auto entry = entries_.begin(); entry != entries_.end(); ++entry) {
        if (entry->bundle.offerId != request.offerId)
            continue;

        if (entry->bundle.bundleId != request.bundleId ||
            entry->bundle.ownerEpoch != request.ownerEpoch) {
            return objectLockError(protocol::ResponseStatus::Conflict,
                                   "transfer object unlock offer is stale");
        }

        const auto lock = std::find_if(
            entry->locks.begin(),
            entry->locks.end(),
            [&request](const TransferObjectLockRequest& active) {
                return active.lockId == request.lockId &&
                       active.sourceId == request.sourceId &&
                       active.objectId == request.objectId &&
                       active.fileIndex == request.fileIndex;
            });
        if (lock == entry->locks.end()) {
            return objectLockError(protocol::ResponseStatus::NotFound,
                                   "transfer object lock is not found");
        }

        const TransferObjectLockId lockId = lock->lockId;
        const std::uint64_t leaseUsec = lock->leaseUsec;
        entry->locks.erase(lock);
        if (entry->retired && entry->locks.empty())
            entries_.erase(entry);

        TransferObjectLockResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.lockId = lockId;
        result.leaseUsec = leaseUsec;
        return result;
    }

    return objectLockError(protocol::ResponseStatus::NotFound,
                           "transfer object lock offer is not found");
}

std::size_t InMemoryTransferSourceRegistry::lockCount() const
{
    std::size_t result = 0;
    for (const Entry& entry : entries_)
        result += entry.locks.size();
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
