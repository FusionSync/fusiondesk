#include "fusiondesk/modules/clipboard/clipboard_types.h"

#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

bool matchesFormat(const TransferFormatDescriptor& descriptor,
                   const TransferReadRequest& request)
{
    if (!request.canonicalFormat.empty() &&
        descriptor.canonicalFormat != request.canonicalFormat)
        return false;

    if (request.formatId != 0 && descriptor.formatId != request.formatId)
        return false;

    if (request.itemIndex != descriptor.itemIndex)
        return false;

    return request.localFormatToken == 0 ||
           descriptor.localFormatToken == request.localFormatToken;
}

} // namespace

MaterializedTransferSource::MaterializedTransferSource(
    TransferSourceId sourceId,
    std::vector<MaterializedTransferEntry> entries)
    : sourceId_(sourceId),
      entries_(std::move(entries))
{
}

TransferSourceId MaterializedTransferSource::id() const
{
    return sourceId_;
}

std::vector<TransferFormatDescriptor> MaterializedTransferSource::formats() const
{
    std::vector<TransferFormatDescriptor> result;
    result.reserve(entries_.size());
    for (const MaterializedTransferEntry& entry : entries_)
        result.push_back(entry.descriptor);
    return result;
}

TransferReadResult MaterializedTransferSource::read(const TransferReadRequest& request)
{
    TransferReadResult result;
    if (request.sourceId != 0 && request.sourceId != sourceId_) {
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "transfer source id is not found";
        return result;
    }

    for (const MaterializedTransferEntry& entry : entries_) {
        if (!matchesFormat(entry.descriptor, request))
            continue;

        if (request.acceptedMaxBytes != 0 &&
            entry.bytes.size() > request.acceptedMaxBytes) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "transfer content exceeds accepted maximum";
            return result;
        }

        result.status = protocol::ResponseStatus::Ok;
        result.canonicalFormat = entry.descriptor.canonicalFormat;
        result.encoding = entry.descriptor.preferredEncoding;
        result.bytes = entry.bytes;
        return result;
    }

    result.status = protocol::ResponseStatus::NotFound;
    result.message = "transfer format is not found";
    return result;
}

RemoteFdclTransferSource::RemoteFdclTransferSource(
    TransferSourceId sourceId,
    std::vector<TransferFormatDescriptor> formats)
    : sourceId_(sourceId),
      formats_(std::move(formats))
{
}

TransferSourceId RemoteFdclTransferSource::id() const
{
    return sourceId_;
}

std::vector<TransferFormatDescriptor> RemoteFdclTransferSource::formats() const
{
    return formats_;
}

TransferReadResult RemoteFdclTransferSource::read(const TransferReadRequest&)
{
    TransferReadResult result;
    result.status = protocol::ResponseStatus::Unsupported;
    result.message = "remote FDCL source requires module-mediated read";
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
