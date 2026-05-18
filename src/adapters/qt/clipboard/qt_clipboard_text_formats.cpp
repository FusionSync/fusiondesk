#include "fusiondesk/adapters/qt/clipboard/qt_clipboard_endpoint.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

constexpr const char* QtRtfMimeName = "text/rtf";
constexpr const char* QtRtfAlternateMimeName = "application/rtf";

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

protocol::ByteBuffer bytesFromString(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

bool descriptorIsText(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == TextPlainUtf8Format ||
           descriptor.nativeFormatName == "text/plain" ||
           descriptor.nativeFormatName == "text/plain;charset=utf-8";
}

bool descriptorIsHtml(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == TextHtmlFormat ||
           descriptor.nativeFormatName == "text/html";
}

bool descriptorIsRtf(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == TextRtfFormat ||
           descriptor.nativeFormatName == QtRtfMimeName ||
           descriptor.nativeFormatName == QtRtfAlternateMimeName;
}

} // namespace

TransferReadResult QtClipboardEndpoint::readBestFormat(
    const TransferSourceBundle& bundle,
    const std::string& canonicalFormat,
    bool (*descriptorMatches)(const TransferFormatDescriptor&),
    const std::string& notFoundMessage)
{
    TransferReadResult result;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        const auto format = std::find_if(formats.begin(),
                                         formats.end(),
                                         descriptorMatches);
        if (format == formats.end())
            continue;

        TransferReadRequest request;
        request.bundleId = bundle.bundleId;
        request.offerId = bundle.offerId;
        request.ownerEpoch = bundle.ownerEpoch;
        request.sourceId = source->id();
        request.itemIndex = format->itemIndex;
        request.formatId = format->formatId;
        request.localFormatToken = format->localFormatToken;
        request.canonicalFormat = canonicalFormat;
        request.acceptedMaxBytes = options_.maxInlineBytes;
        request.requestedEncoding = TransferEncodingMode::CanonicalBytes;

        result = source->read(request);
        if (result.status == protocol::ResponseStatus::Unsupported &&
            remoteReader_ != nullptr) {
            result = remoteReader_->readRemoteFormat(request, 1000);
        }
        if (result.ok() && result.canonicalFormat.empty())
            result.canonicalFormat = canonicalFormat;
        return result;
    }

    result.status = protocol::ResponseStatus::NotFound;
    result.message = notFoundMessage;
    return result;
}

TransferReadResult QtClipboardEndpoint::readBestText(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextPlainUtf8Format,
                          descriptorIsText,
                          "qt clipboard text format is not found");
}

TransferReadResult QtClipboardEndpoint::readBestHtml(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextHtmlFormat,
                          descriptorIsHtml,
                          "qt clipboard html format is not found");
}

TransferReadResult QtClipboardEndpoint::readBestRtf(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextRtfFormat,
                          descriptorIsRtf,
                          "qt clipboard rtf format is not found");
}

ClipboardSnapshot QtClipboardEndpoint::snapshotFromText(
    const std::string& text,
    std::uint64_t sequence)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "text/plain;charset=utf-8";
    descriptor.localFormatToken = 1;
    descriptor.formatId = nextFormatId_++;
    descriptor.estimatedBytes = text.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytesFromString(text);

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = sequence == 0 ? 1 : sequence;
    bundle.sequence = sequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{std::move(entry)}));

    TransferPresentation presentation;
    presentation.displayName = "Qt clipboard text";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    bundle.presentation = std::move(presentation);

    ClipboardSnapshot snapshot;
    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot QtClipboardEndpoint::snapshotFromFormattedText(
    const std::string& text,
    const std::string& html,
    const std::string& rtf,
    std::uint64_t sequence)
{
    std::vector<MaterializedTransferEntry> entries;
    if (!html.empty()) {
        TransferFormatDescriptor htmlDescriptor;
        htmlDescriptor.canonicalFormat = TextHtmlFormat;
        htmlDescriptor.nativeFormatName = "text/html";
        htmlDescriptor.localFormatToken = 3;
        htmlDescriptor.formatId = nextFormatId_++;
        htmlDescriptor.estimatedBytes = html.size();
        htmlDescriptor.canInline = true;
        htmlDescriptor.canStream = false;
        htmlDescriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

        MaterializedTransferEntry htmlEntry;
        htmlEntry.descriptor = htmlDescriptor;
        htmlEntry.bytes = bytesFromString(html);
        entries.push_back(std::move(htmlEntry));
    }

    if (!rtf.empty()) {
        TransferFormatDescriptor rtfDescriptor;
        rtfDescriptor.canonicalFormat = TextRtfFormat;
        rtfDescriptor.nativeFormatName = QtRtfMimeName;
        rtfDescriptor.localFormatToken = 4;
        rtfDescriptor.formatId = nextFormatId_++;
        rtfDescriptor.estimatedBytes = rtf.size();
        rtfDescriptor.canInline = true;
        rtfDescriptor.canStream = false;
        rtfDescriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

        MaterializedTransferEntry rtfEntry;
        rtfEntry.descriptor = rtfDescriptor;
        rtfEntry.bytes = bytesFromString(rtf);
        entries.push_back(std::move(rtfEntry));
    }

    if (!text.empty()) {
        TransferFormatDescriptor textDescriptor;
        textDescriptor.canonicalFormat = TextPlainUtf8Format;
        textDescriptor.nativeFormatName = "text/plain;charset=utf-8";
        textDescriptor.localFormatToken = 1;
        textDescriptor.formatId = nextFormatId_++;
        textDescriptor.itemIndex = 0;
        textDescriptor.estimatedBytes = text.size();
        textDescriptor.canInline = true;
        textDescriptor.canStream = false;
        textDescriptor.preferredEncoding =
            TransferEncodingMode::CanonicalBytes;

        MaterializedTransferEntry textEntry;
        textEntry.descriptor = textDescriptor;
        textEntry.bytes = bytesFromString(text);
        entries.push_back(std::move(textEntry));
    }

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = sequence == 0 ? 1 : sequence;
    bundle.sequence = sequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::move(entries)));

    TransferPresentation presentation;
    presentation.displayName = !html.empty() ? "Qt clipboard HTML"
                                             : "Qt clipboard RTF";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    bundle.presentation = std::move(presentation);

    ClipboardSnapshot snapshot;
    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

} // namespace clipboard
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
