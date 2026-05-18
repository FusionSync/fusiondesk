#ifndef FUSIONDESK_MODULES_CLIPBOARD_MODULE_POLICY_INTERNAL_H
#define FUSIONDESK_MODULES_CLIPBOARD_MODULE_POLICY_INTERNAL_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_modules.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

inline std::uint64_t effectiveMaxFileRangeBytes(const ClipboardPolicy& policy)
{
    return policy.maxFileRangeBytes == 0
               ? policy.maxInlineBytes
               : policy.maxFileRangeBytes;
}

inline bool policyAllowsCanonicalFormat(const ClipboardPolicy& policy,
                                        const std::string& canonicalFormat)
{
    if (canonicalFormat.empty())
        return policy.allowCustomFormats;
    if (canonicalFormat == TextPlainUtf8Format)
        return policy.allowPlainText;
    if (canonicalFormat == TextHtmlFormat)
        return policy.allowHtml;
    if (canonicalFormat == TextRtfFormat)
        return policy.allowRtf;
    if (canonicalFormat == ImagePngFormat ||
        canonicalFormat == ImageDibFormat)
        return policy.allowImage;
    if (canonicalFormat == FdclFileListFormat)
        return policy.allowFileList;
    return policy.allowCustomFormats;
}

inline std::string requestedCanonicalFormat(
    const TransferSourceBundle& bundle,
    const FdclReadFormatRequest& request)
{
    if (!request.canonicalFormat.empty())
        return request.canonicalFormat;

    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        if (request.sourceId != 0 && source->id() != request.sourceId)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        for (const TransferFormatDescriptor& descriptor : formats) {
            if (request.formatId != 0 &&
                descriptor.formatId != request.formatId)
                continue;
            if (request.localFormatToken != 0 &&
                descriptor.localFormatToken != request.localFormatToken)
                continue;
            if (descriptor.itemIndex != request.itemIndex)
                continue;
            return descriptor.canonicalFormat;
        }
    }

    return {};
}

inline bool policyAllowsReadRequest(const ClipboardPolicy& policy,
                                    const TransferSourceBundle& bundle,
                                    const FdclReadFormatRequest& request)
{
    const std::string canonical = requestedCanonicalFormat(bundle, request);
    return !canonical.empty() &&
           policyAllowsCanonicalFormat(policy, canonical);
}

inline protocol::ResponseStatus validateFileRangeRequestAgainstBundle(
    const TransferSourceBundle& bundle,
    const FdclFileRangeRequest& request,
    std::string& message)
{
    if (bundle.bundleId != request.bundleId ||
        bundle.offerId != request.offerId ||
        bundle.ownerEpoch != request.ownerEpoch) {
        message = "clipboard file offer is stale";
        return protocol::ResponseStatus::Conflict;
    }

    bool sourceFound = false;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr || source->id() != request.sourceId)
            continue;

        sourceFound = true;
        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        for (const TransferFormatDescriptor& descriptor : formats) {
            if (descriptor.canonicalFormat != FdclFileListFormat)
                continue;
            if (!descriptor.canStream) {
                message = "clipboard file source does not allow streaming";
                return protocol::ResponseStatus::Unsupported;
            }
            return protocol::ResponseStatus::Ok;
        }
    }

    message = sourceFound
                  ? "clipboard file source has no file-list format"
                  : "clipboard file source is not found";
    return protocol::ResponseStatus::NotFound;
}

inline protocol::ResponseStatus validateObjectLockAgainstBundle(
    const TransferSourceBundle& bundle,
    const FdclObjectLock& request,
    std::string& message)
{
    if (bundle.bundleId != request.bundleId ||
        bundle.offerId != request.offerId ||
        bundle.ownerEpoch != request.ownerEpoch) {
        message = "clipboard file object offer is stale";
        return protocol::ResponseStatus::Conflict;
    }

    bool sourceFound = false;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr || source->id() != request.sourceId)
            continue;

        sourceFound = true;
        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        for (const TransferFormatDescriptor& descriptor : formats) {
            if (descriptor.canonicalFormat != FdclFileListFormat)
                continue;
            if (!descriptor.canStream) {
                message = "clipboard file object does not allow streaming";
                return protocol::ResponseStatus::Unsupported;
            }
            return protocol::ResponseStatus::Ok;
        }
    }

    message = sourceFound
                  ? "clipboard file object source has no file-list format"
                  : "clipboard file object source is not found";
    return protocol::ResponseStatus::NotFound;
}

inline std::size_t maxFileCountForDecode(const ClipboardPolicy& policy)
{
    const std::uint64_t maxSizeT =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    return static_cast<std::size_t>(
        std::min<std::uint64_t>(policy.maxFileCount, maxSizeT));
}

inline protocol::ResponseStatus validateFileListPolicy(
    const ClipboardPolicy& policy,
    const protocol::ByteBuffer& bytes,
    std::string& message)
{
    if (!policy.allowFileList) {
        message = "clipboard file list denied by policy";
        return protocol::ResponseStatus::DeniedByPolicy;
    }

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(bytes, maxFileCountForDecode(policy));
    if (!decoded.ok) {
        message = decoded.message;
        return decoded.status;
    }

    for (const TransferFileDescriptor& file : decoded.fileList.files) {
        if (!file.directory &&
            policy.maxSingleFileBytes != 0 &&
            file.sizeBytes > policy.maxSingleFileBytes) {
            message = "clipboard file list exceeds max single file bytes";
            return protocol::ResponseStatus::TooLarge;
        }
    }

    return protocol::ResponseStatus::Ok;
}

inline void enforceReadResultPolicy(const ClipboardPolicy& policy,
                                    TransferReadResult& result)
{
    if (!result.ok())
        return;

    if (!policyAllowsCanonicalFormat(policy, result.canonicalFormat)) {
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.bytes.clear();
        result.message = "clipboard format denied by policy";
        return;
    }

    if (result.bytes.size() > policy.maxInlineBytes) {
        result.status = protocol::ResponseStatus::TooLarge;
        result.bytes.clear();
        result.message = "clipboard read exceeds policy max inline bytes";
        return;
    }

    if (result.canonicalFormat == FdclFileListFormat) {
        std::string message;
        const protocol::ResponseStatus status =
            validateFileListPolicy(policy, result.bytes, message);
        if (status != protocol::ResponseStatus::Ok) {
            result.status = status;
            result.bytes.clear();
            result.message = std::move(message);
        }
    }
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_MODULE_POLICY_INTERNAL_H
