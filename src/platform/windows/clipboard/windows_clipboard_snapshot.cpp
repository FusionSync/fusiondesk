#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_descriptors.h"
#include "windows_clipboard_image_transcoding.h"
#include "windows_clipboard_local_files.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

bool descriptorMatchesNativeTarget(
    const TransferFormatDescriptor& descriptor,
    const std::string& nativeFormatName,
    std::uint32_t localFormatToken)
{
    return (!nativeFormatName.empty() &&
            descriptor.nativeFormatName == nativeFormatName) ||
           (localFormatToken != 0 &&
            descriptor.localFormatToken == localFormatToken);
}

} // namespace

ClipboardSnapshot WindowsClipboardEndpoint::snapshotFromCanonicalText(
    const protocol::ByteBuffer& canonicalUtf8,
    std::uint64_t nativeSequence)
{
    ClipboardSnapshot snapshot;
    if (canonicalUtf8.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = WindowsUnicodeTextName;
    descriptor.localFormatToken = 13;
    descriptor.formatId = nextFormatId_++;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = canonicalUtf8.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = canonicalUtf8;

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = nativeSequence == 0 ? monotonicNowUsec() : nativeSequence;
    bundle.sequence = nativeSequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{entry}));

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot WindowsClipboardEndpoint::snapshotFromCanonicalHtml(
    const protocol::ByteBuffer& canonicalHtml,
    std::uint64_t nativeSequence)
{
    ClipboardSnapshot snapshot;
    if (canonicalHtml.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextHtmlFormat;
    descriptor.nativeFormatName = WindowsHtmlName;
    descriptor.localFormatToken = windowsHtmlFormatToken();
    descriptor.formatId = nextFormatId_++;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = canonicalHtml.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = canonicalHtml;

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = nativeSequence == 0 ? monotonicNowUsec() : nativeSequence;
    bundle.sequence = nativeSequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{entry}));

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot WindowsClipboardEndpoint::snapshotFromCanonicalRtf(
    const protocol::ByteBuffer& canonicalRtf,
    std::uint64_t nativeSequence)
{
    ClipboardSnapshot snapshot;
    if (canonicalRtf.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextRtfFormat;
    descriptor.nativeFormatName = WindowsRtfName;
    descriptor.localFormatToken = windowsRtfFormatToken();
    descriptor.formatId = nextFormatId_++;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = canonicalRtf.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::NativePassthrough;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = canonicalRtf;

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = nativeSequence == 0 ? monotonicNowUsec() : nativeSequence;
    bundle.sequence = nativeSequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{entry}));

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot WindowsClipboardEndpoint::snapshotFromImagePng(
    const protocol::ByteBuffer& png,
    std::uint64_t nativeSequence,
    const std::string& nativeFormatName,
    std::uint32_t localFormatToken,
    TransferEncodingMode preferredEncoding)
{
    ClipboardSnapshot snapshot;
    if (png.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName =
        nativeFormatName.empty() ? WindowsPngName : nativeFormatName;
    descriptor.localFormatToken =
        localFormatToken == 0 ? windowsPngFormatToken() : localFormatToken;
    descriptor.formatId = nextFormatId_++;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = png.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = preferredEncoding;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = png;

    TransferPresentation presentation;
    presentation.displayName = "image";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Image;
    presentation.allowedActions = transfer_action::Copy;
    presentation.preferredAction = TransferAction::Copy;
    presentation.previewAllowedByPolicy = false;
    IconRepresentation icon;
    icon.format = ImagePngFormat;
    icon.bytes = png.size();
    icon.sensitive = false;
    presentation.icons.push_back(icon);

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = nativeSequence == 0 ? monotonicNowUsec() : nativeSequence;
    bundle.sequence = nativeSequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.presentation = presentation;
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{entry}));

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot WindowsClipboardEndpoint::snapshotFromImageDib(
    const protocol::ByteBuffer& dib,
    const protocol::ByteBuffer& png,
    std::uint64_t nativeSequence,
    const std::string& nativeFormatName,
    std::uint32_t localFormatToken)
{
    ClipboardSnapshot snapshot;
    if (dib.empty() || png.empty())
        return snapshot;

    TransferFormatDescriptor pngDescriptor;
    pngDescriptor.canonicalFormat = ImagePngFormat;
    pngDescriptor.nativeFormatName = WindowsPngName;
    pngDescriptor.localFormatToken = windowsPngFormatToken();
    pngDescriptor.formatId = nextFormatId_++;
    pngDescriptor.itemIndex = 0;
    pngDescriptor.estimatedBytes = png.size();
    pngDescriptor.canInline = true;
    pngDescriptor.canStream = false;
    pngDescriptor.preferredEncoding = TransferEncodingMode::Transcoded;

    TransferFormatDescriptor dibDescriptor;
    dibDescriptor.canonicalFormat = ImageDibFormat;
    dibDescriptor.nativeFormatName = nativeFormatName;
    dibDescriptor.localFormatToken = localFormatToken;
    dibDescriptor.formatId = nextFormatId_++;
    dibDescriptor.itemIndex = 0;
    dibDescriptor.estimatedBytes = dib.size();
    dibDescriptor.canInline = true;
    dibDescriptor.canStream = false;
    dibDescriptor.preferredEncoding = TransferEncodingMode::NativePassthrough;

    MaterializedTransferEntry pngEntry;
    pngEntry.descriptor = pngDescriptor;
    pngEntry.bytes = png;
    MaterializedTransferEntry dibEntry;
    dibEntry.descriptor = dibDescriptor;
    dibEntry.bytes = dib;

    TransferPresentation presentation;
    presentation.displayName = "image";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Image;
    presentation.allowedActions = transfer_action::Copy;
    presentation.preferredAction = TransferAction::Copy;
    presentation.previewAllowedByPolicy = false;
    IconRepresentation icon;
    icon.format = ImagePngFormat;
    icon.bytes = png.size();
    icon.sensitive = false;
    presentation.icons.push_back(icon);

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = nativeSequence == 0 ? monotonicNowUsec() : nativeSequence;
    bundle.sequence = nativeSequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.presentation = presentation;
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{pngEntry, dibEntry}));

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot WindowsClipboardEndpoint::snapshotFromFileList(
    const TransferFileList& fileList,
    std::uint64_t nativeSequence,
    const std::string& nativeFormatName,
    std::uint32_t localFormatToken)
{
    ClipboardSnapshot snapshot;
    if (fileList.files.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = nativeFormatName;
    descriptor.localFormatToken = localFormatToken;
    descriptor.formatId = nextFormatId_++;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = encodeTransferFileList(fileList).size();
    descriptor.canInline = true;
    descriptor.canStream =
        options_.dryRun && dryRunFilePaths_.size() == fileList.files.size();
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferPresentation presentation;
    presentation.itemCount = static_cast<std::uint32_t>(fileList.files.size());
    presentation.sourceKind = TransferSourceKind::FileList;
    presentation.allowedActions = transfer_action::Copy;
    presentation.preferredAction = TransferAction::Copy;
    presentation.displayName =
        fileList.files.size() == 1
            ? sanitizeTransferFileDisplayName(fileList.files.front().displayName)
            : std::string("files");

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = nativeSequence == 0 ? monotonicNowUsec() : nativeSequence;
    bundle.sequence = nativeSequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.presentation = presentation;
#if defined(_WIN32)
    if (descriptor.canStream) {
        bundle.sources.push_back(createLocalWindowsFileTransferSource(
            nextSourceId_++,
            descriptor,
            fileList,
            dryRunFilePaths_,
            options_.maxFileRangeBytes,
            options_.maxSingleFileBytes));
    } else
#endif
    {
        bundle.sources.push_back(std::make_shared<FileGroupTransferSource>(
            nextSourceId_++,
            descriptor,
            fileList));
    }

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    ++diagnostics_.fileListSnapshots;
    return snapshot;
}

TransferReadResult WindowsClipboardEndpoint::readBestText(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextPlainUtf8Format,
                          WindowsUnicodeTextName,
                          13);
}

TransferReadResult WindowsClipboardEndpoint::readBestHtml(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextHtmlFormat,
                          WindowsHtmlName,
                          windowsHtmlFormatToken());
}

TransferReadResult WindowsClipboardEndpoint::readBestRtf(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextRtfFormat,
                          WindowsRtfName,
                          windowsRtfFormatToken());
}

TransferReadResult WindowsClipboardEndpoint::readBestImagePng(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          ImagePngFormat,
                          WindowsPngName,
                          windowsPngFormatToken());
}

TransferReadResult WindowsClipboardEndpoint::readBestImageDib(
    const TransferSourceBundle& bundle)
{
    TransferReadResult result = readBestFormat(bundle,
                                               ImageDibFormat,
                                               WindowsDibV5Name,
                                               0);
    if (result.ok())
        return result;
    return readBestFormat(bundle,
                          ImageDibFormat,
                          WindowsDibName,
                          0);
}

TransferReadResult WindowsClipboardEndpoint::readBestFileList(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          FdclFileListFormat,
                          WindowsFileGroupDescriptorName,
                          windowsFileGroupDescriptorFormatToken());
}

TransferReadResult WindowsClipboardEndpoint::readBestFormat(
    const TransferSourceBundle& bundle,
    const std::string& canonicalFormat,
    const std::string& nativeFormatName,
    std::uint32_t localFormatToken)
{
    TransferReadResult failure;
    failure.status = protocol::ResponseStatus::NotFound;
    failure.message = "windows clipboard format is not found";

    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        for (const TransferFormatDescriptor& descriptor : source->formats()) {
            const bool descriptorMatches =
                descriptor.canonicalFormat == canonicalFormat ||
                descriptor.nativeFormatName == nativeFormatName ||
                (localFormatToken != 0 &&
                 descriptor.localFormatToken == localFormatToken);
            if (!descriptorMatches)
                continue;

            TransferReadRequest request;
            request.bundleId = bundle.bundleId;
            request.offerId = bundle.offerId;
            request.ownerEpoch = bundle.ownerEpoch;
            request.sourceId = source->id();
            request.itemIndex = descriptor.itemIndex;
            request.formatId = descriptor.formatId;
            request.localFormatToken = descriptor.localFormatToken;
            request.canonicalFormat = canonicalFormat;
            request.acceptedMaxBytes = options_.maxInlineBytes;
            request.streamAccepted = false;
            request.requestedEncoding = descriptor.preferredEncoding;

            TransferReadResult result = source->read(request);
            if (!result.ok() && remoteReader_ != nullptr) {
                result = remoteReader_->readRemoteFormat(
                    request,
                    options_.delayedReadTimeoutMs == 0
                        ? 1000
                        : options_.delayedReadTimeoutMs);
            }

            if (!result.ok()) {
                failure = result;
                continue;
            }

            if (result.encoding == TransferEncodingMode::NativePassthrough &&
                descriptorMatchesNativeTarget(descriptor,
                                              nativeFormatName,
                                              localFormatToken)) {
                if (result.canonicalFormat.empty())
                    result.canonicalFormat = canonicalFormat;
                return result;
            }

            if (result.encoding != TransferEncodingMode::CanonicalBytes) {
                TransferTranscodeRequest transcodeRequest;
                transcodeRequest.canonicalFormat = canonicalFormat;
                transcodeRequest.sourceNative.platform =
                    TransferPlatformFamily::Windows;
                transcodeRequest.sourceNative.nativeFormatName =
                    descriptor.nativeFormatName;
                transcodeRequest.sourceNative.localFormatToken =
                    descriptor.localFormatToken;
                transcodeRequest.targetNative = transcodeRequest.sourceNative;
                transcodeRequest.sourceEncoding = result.encoding;
                transcodeRequest.targetEncoding =
                    TransferEncodingMode::CanonicalBytes;
                transcodeRequest.bytes = std::move(result.bytes);

                DefaultTransferTranscoder transcoder;
                if (transcoder.canTranscode(transcodeRequest)) {
                    const TransferTranscodeResult transcoded =
                        transcoder.transcode(transcodeRequest);
                    if (!transcoded.ok()) {
                        failure.status = transcoded.status;
                        failure.message = transcoded.message;
                        continue;
                    }
                    result.bytes = transcoded.bytes;
                    result.encoding = transcoded.encoding;
                } else {
                    result.bytes = std::move(transcodeRequest.bytes);
                }
            }
            if (result.canonicalFormat.empty())
                result.canonicalFormat = canonicalFormat;
            return result;
        }
    }
    return failure;
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
