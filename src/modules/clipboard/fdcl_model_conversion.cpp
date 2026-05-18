#include "fusiondesk/modules/clipboard/fdcl_codec.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace fusiondesk {
namespace modules {
namespace clipboard {

FdclFormatList makeFormatListFromBundle(const TransferSourceBundle& bundle)
{
    FdclFormatList list;
    list.bundleId = bundle.bundleId;
    list.offerId = bundle.offerId;
    list.ownerEpoch = bundle.ownerEpoch;
    list.sequence = bundle.sequence;
    list.origin = bundle.origin;
    list.side = bundle.side;
    list.originSessionId = bundle.originSessionId;
    list.policyVersion = bundle.policyVersion;
    list.presentation = bundle.presentation;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        for (const TransferFormatDescriptor& descriptor : source->formats()) {
            FdclFormatRecord record;
            record.sourceId = source->id();
            record.itemIndex = descriptor.itemIndex;
            record.formatId = descriptor.formatId;
            record.localFormatToken = descriptor.localFormatToken;
            record.estimatedBytes = descriptor.estimatedBytes;
            record.canInline = descriptor.canInline;
            record.canStream = descriptor.canStream;
            record.preferredEncoding = descriptor.preferredEncoding;
            record.canonicalFormat = descriptor.canonicalFormat;
            record.nativeFormatName = descriptor.nativeFormatName;
            list.formats.push_back(std::move(record));
        }
    }
    return list;
}

TransferSourceBundle makeRemoteBundleFromFormatList(const FdclFormatList& list)
{
    TransferSourceBundle bundle;
    bundle.bundleId = list.bundleId;
    bundle.offerId = list.offerId;
    bundle.ownerEpoch = list.ownerEpoch;
    bundle.sequence = list.sequence;
    bundle.origin = TransferOrigin::RemoteOffer;
    bundle.side = TransferSide::Remote;
    bundle.originSessionId = list.originSessionId;
    bundle.policyVersion = list.policyVersion;
    bundle.presentation = list.presentation;

    std::map<TransferSourceId, std::vector<TransferFormatDescriptor>> bySource;
    for (const FdclFormatRecord& record : list.formats) {
        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = record.canonicalFormat;
        descriptor.nativeFormatName = record.nativeFormatName;
        descriptor.localFormatToken = record.localFormatToken;
        descriptor.formatId = record.formatId;
        descriptor.itemIndex = record.itemIndex;
        descriptor.estimatedBytes = record.estimatedBytes;
        descriptor.canInline = record.canInline;
        descriptor.canStream = record.canStream;
        descriptor.preferredEncoding = record.preferredEncoding;
        bySource[record.sourceId].push_back(std::move(descriptor));
    }

    for (auto& item : bySource) {
        bundle.sources.push_back(
            std::make_shared<RemoteFdclTransferSource>(item.first,
                                                       std::move(item.second)));
    }
    return bundle;
}

TransferReadRequest makeTransferReadRequest(
    const FdclReadFormatRequest& request)
{
    TransferReadRequest result;
    result.bundleId = request.bundleId;
    result.offerId = request.offerId;
    result.ownerEpoch = request.ownerEpoch;
    result.sourceId = request.sourceId;
    result.itemIndex = request.itemIndex;
    result.formatId = request.formatId;
    result.localFormatToken = request.localFormatToken;
    result.canonicalFormat = request.canonicalFormat;
    result.acceptedMaxBytes = request.acceptedMaxBytes;
    result.streamAccepted = request.streamAccepted;
    result.requestedEncoding = request.requestedEncoding;
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
