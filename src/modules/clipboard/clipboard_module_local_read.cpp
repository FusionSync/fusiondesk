#include "fusiondesk/modules/clipboard/clipboard_modules.h"

#include <memory>
#include <optional>
#include <vector>

#include "clipboard_module_policy_internal.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

bool descriptorMatchesReadRequest(const TransferFormatDescriptor& descriptor,
                                  const TransferReadRequest& request)
{
    if (!request.canonicalFormat.empty() &&
        descriptor.canonicalFormat != request.canonicalFormat)
        return false;
    if (request.formatId != 0 && descriptor.formatId != request.formatId)
        return false;
    if (descriptor.itemIndex != request.itemIndex)
        return false;
    return request.localFormatToken == 0 ||
           descriptor.localFormatToken == request.localFormatToken;
}

std::optional<TransferFormatDescriptor> findReadDescriptor(
    TransferSource& source,
    const TransferReadRequest& request)
{
    const std::vector<TransferFormatDescriptor> formats = source.formats();
    for (const TransferFormatDescriptor& descriptor : formats) {
        if (descriptorMatchesReadRequest(descriptor, request))
            return descriptor;
    }
    return std::nullopt;
}

TransferReadResult transcodeReadResultIfNeeded(
    ITransferTranscoder* transcoder,
    const TransferFormatDescriptor& descriptor,
    const TransferReadRequest& request,
    TransferReadResult result)
{
    if (!result.ok() || result.encoding == request.requestedEncoding)
        return result;

    if (transcoder == nullptr) {
        result.status = protocol::ResponseStatus::Unsupported;
        result.bytes.clear();
        result.message = "clipboard format transcode is unavailable";
        return result;
    }

    TransferTranscodeRequest transcodeRequest;
    transcodeRequest.canonicalFormat = result.canonicalFormat.empty()
                                           ? request.canonicalFormat
                                           : result.canonicalFormat;
    transcodeRequest.sourceNative.nativeFormatName =
        descriptor.nativeFormatName;
    transcodeRequest.sourceNative.localFormatToken =
        descriptor.localFormatToken;
    if (request.requestedEncoding == TransferEncodingMode::NativePassthrough) {
        transcodeRequest.targetNative = transcodeRequest.sourceNative;
    }
    transcodeRequest.sourceEncoding = result.encoding;
    transcodeRequest.targetEncoding = request.requestedEncoding;
    transcodeRequest.bytes = std::move(result.bytes);

    if (!transcoder->canTranscode(transcodeRequest)) {
        result.status = protocol::ResponseStatus::Unsupported;
        result.bytes.clear();
        result.message = "clipboard format transcode is unsupported";
        return result;
    }

    const TransferTranscodeResult transcoded =
        transcoder->transcode(transcodeRequest);
    if (!transcoded.ok()) {
        result.status = transcoded.status;
        result.bytes.clear();
        result.message = transcoded.message.empty()
                             ? "clipboard format transcode failed"
                             : transcoded.message;
        return result;
    }

    result.status = protocol::ResponseStatus::Ok;
    result.encoding = transcoded.encoding;
    result.bytes = transcoded.bytes;
    result.message.clear();
    if (result.canonicalFormat.empty())
        result.canonicalFormat = transcodeRequest.canonicalFormat;
    return result;
}

} // namespace

TransferReadResult ClipboardModuleBase::readLocal(
    const FdclReadFormatRequest& request)
{
    TransferReadResult result;
    const TransferSourceBundle& bundle = snapshot_.localBundle;
    if (bundle.bundleId != request.bundleId ||
        bundle.offerId != request.offerId ||
        bundle.ownerEpoch != request.ownerEpoch) {
        result.status = protocol::ResponseStatus::Conflict;
        result.message = "clipboard offer is stale";
        ++snapshot_.staleOfferFailures;
        return result;
    }
    if (!policyAllowsReadRequest(dependencies_.policy, bundle, request)) {
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard format denied by policy";
        ++snapshot_.policyDenials;
        return result;
    }

    TransferReadRequest readRequest = makeTransferReadRequest(request);
    if (readRequest.acceptedMaxBytes == 0 ||
        readRequest.acceptedMaxBytes > dependencies_.policy.maxInlineBytes) {
        readRequest.acceptedMaxBytes = dependencies_.policy.maxInlineBytes;
    }

    if (dependencies_.sourceRegistry != nullptr) {
        const TransferSourceLookup lookup =
            dependencies_.sourceRegistry->lookupSource(readRequest);
        if (!lookup.found()) {
            result.status = lookup.status;
            result.message = lookup.message;
            if (result.status == protocol::ResponseStatus::Conflict ||
                result.status == protocol::ResponseStatus::NotFound)
                ++snapshot_.staleOfferFailures;
            return result;
        }

        const std::optional<TransferFormatDescriptor> descriptor =
            findReadDescriptor(*lookup.source, readRequest);
        if (!descriptor.has_value()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "clipboard format descriptor is not found";
            ++snapshot_.staleOfferFailures;
            return result;
        }

        result = lookup.source->read(readRequest);
        if (result.status == protocol::ResponseStatus::TooLarge)
            ++snapshot_.tooLargeFailures;
        result = transcodeReadResultIfNeeded(dependencies_.transcoder.get(),
                                             descriptor.value(),
                                             readRequest,
                                             std::move(result));
        if (result.ok() &&
            readRequest.acceptedMaxBytes != 0 &&
            result.bytes.size() > readRequest.acceptedMaxBytes) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.bytes.clear();
            result.message =
                "clipboard transcoded content exceeds accepted maximum";
            ++snapshot_.tooLargeFailures;
        }
        const protocol::ResponseStatus beforePolicy = result.status;
        enforceReadResultPolicy(dependencies_.policy, result);
        if (beforePolicy != result.status) {
            if (result.status == protocol::ResponseStatus::DeniedByPolicy)
                ++snapshot_.policyDenials;
            else if (result.status == protocol::ResponseStatus::TooLarge)
                ++snapshot_.tooLargeFailures;
            else if (result.status == protocol::ResponseStatus::ProtocolError)
                ++snapshot_.decodeFailures;
        }
        return result;
    }

    result.status = protocol::ResponseStatus::NotFound;
    result.message = "clipboard source is not found";
    ++snapshot_.staleOfferFailures;
    return result;
}

TransferFileRangeResult ClipboardModuleBase::readLocalFileRange(
    const FdclFileRangeRequest& request)
{
    TransferFileRangeResult result;
    const TransferSourceBundle& bundle = snapshot_.localBundle;
    if (bundle.bundleId != request.bundleId ||
        bundle.offerId != request.offerId ||
        bundle.ownerEpoch != request.ownerEpoch) {
        result.status = protocol::ResponseStatus::Conflict;
        result.message = "clipboard file offer is stale";
        ++snapshot_.staleOfferFailures;
        return result;
    }
    if (!dependencies_.policy.allowFileContents) {
        result.status = protocol::ResponseStatus::DeniedByPolicy;
        result.message = "clipboard file contents denied by policy";
        ++snapshot_.policyDenials;
        return result;
    }
    std::string validationMessage;
    const protocol::ResponseStatus validation =
        validateFileRangeRequestAgainstBundle(bundle,
                                              request,
                                              validationMessage);
    if (validation != protocol::ResponseStatus::Ok) {
        result.status = validation;
        result.message = std::move(validationMessage);
        if (validation == protocol::ResponseStatus::Conflict ||
            validation == protocol::ResponseStatus::NotFound)
            ++snapshot_.staleOfferFailures;
        return result;
    }

    TransferFileRangeRequest rangeRequest;
    rangeRequest.bundleId = request.bundleId;
    rangeRequest.offerId = request.offerId;
    rangeRequest.ownerEpoch = request.ownerEpoch;
    rangeRequest.sourceId = request.sourceId;
    rangeRequest.objectId = request.objectId;
    rangeRequest.fileIndex = request.fileIndex;
    rangeRequest.offset = request.offset;
    rangeRequest.requestedBytes = request.requestedBytes;
    const std::uint64_t maxRangeBytes =
        effectiveMaxFileRangeBytes(dependencies_.policy);
    if (rangeRequest.requestedBytes == 0 ||
        rangeRequest.requestedBytes > maxRangeBytes) {
        rangeRequest.requestedBytes = maxRangeBytes;
    }

    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr || source->id() != request.sourceId)
            continue;

        std::shared_ptr<ITransferFileContentProvider> fileProvider =
            std::dynamic_pointer_cast<ITransferFileContentProvider>(source);
        if (fileProvider == nullptr) {
            result.status = protocol::ResponseStatus::Unsupported;
            result.message = "clipboard source does not provide file contents";
            return result;
        }

        result = fileProvider->readFileRange(rangeRequest);
        if (result.status == protocol::ResponseStatus::TooLarge)
            ++snapshot_.tooLargeFailures;
        if (result.ok() &&
            result.bytes.size() > maxRangeBytes) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.bytes.clear();
            result.message = "clipboard file range exceeds policy max bytes";
            ++snapshot_.tooLargeFailures;
        }
        return result;
    }

    result.status = protocol::ResponseStatus::NotFound;
    result.message = "clipboard file source is not found";
    ++snapshot_.staleOfferFailures;
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
