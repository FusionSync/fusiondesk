#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_descriptors.h"
#include "windows_clipboard_image_transcoding.h"
#include "windows_clipboard_native_helpers.h"

#include <algorithm>
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

std::string bytesToString(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

protocol::ByteBuffer stringToBytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

std::uint32_t defaultDibFormatToken()
{
#if defined(_WIN32)
    return CF_DIB;
#else
    return 0;
#endif
}

} // namespace

WindowsClipboardEndpoint::WindowsClipboardEndpoint(
    WindowsClipboardEndpointOptions options,
    std::shared_ptr<IClipboardRemoteReader> remoteReader,
    std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader,
    std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker,
    std::shared_ptr<IRemoteDisplayCoordinateMapper> dragCoordinateMapper)
    : options_(options),
      remoteReader_(std::move(remoteReader)),
      remoteFileReader_(std::move(remoteFileReader)),
      remoteObjectLocker_(std::move(remoteObjectLocker)),
      dragCoordinateMapper_(std::move(dragCoordinateMapper)),
      nextBundleId_(std::max<std::uint64_t>(1, options.firstBundleId)),
      nextOfferId_(std::max<std::uint64_t>(1, options.firstOfferId)),
      nextSourceId_(std::max<std::uint64_t>(1, options.firstSourceId)),
      nextFormatId_(std::max<std::uint64_t>(1, options.firstFormatId))
{
    if (remoteFileReader_ == nullptr)
        remoteFileReader_ =
            std::dynamic_pointer_cast<IClipboardRemoteFileReader>(remoteReader_);
    if (remoteObjectLocker_ == nullptr)
        remoteObjectLocker_ =
            std::dynamic_pointer_cast<IClipboardRemoteObjectLocker>(remoteReader_);
    diagnostics_.dryRun = options_.dryRun;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;

#if defined(_WIN32)
    if (!options_.dryRun && options_.enableNativeClipboardWatcher) {
        const HWND owner = clipboardOwnerWindow();
        if (owner == nullptr) {
            recordNativeFailure(GetLastError(),
                                "clipboard watcher owner window creation failed");
        } else {
            SetWindowLongPtrW(owner,
                              GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(this));
            if (AddClipboardFormatListener(owner)) {
                nativeClipboardWatcherRegistered_ = true;
                diagnostics_.nativeWatcherRegistered = true;
            } else {
                recordNativeFailure(
                    GetLastError(),
                    "AddClipboardFormatListener failed for clipboard watcher");
            }
        }
    }
#endif
}

WindowsClipboardEndpoint::~WindowsClipboardEndpoint()
{
#if defined(_WIN32)
    if (!options_.dryRun) {
        const HWND owner = clipboardOwnerWindow();
        const bool ownsWindow =
            owner != nullptr &&
            reinterpret_cast<WindowsClipboardEndpoint*>(
                GetWindowLongPtrW(owner, GWLP_USERDATA)) == this;
        if (nativeClipboardWatcherRegistered_ && owner != nullptr)
            RemoveClipboardFormatListener(owner);
        if (ownsWindow && diagnostics_.delayedPublishes > 0)
            renderAllDelayedFormatsForNative();
        if (ownsWindow) {
            SetWindowLongPtrW(owner, GWLP_USERDATA, 0);
        }
    }
#endif
}

ClipboardSnapshot WindowsClipboardEndpoint::snapshot()
{
    ++diagnostics_.snapshots;
    if (options_.dryRun) {
        if (!dryRunFileList_.files.empty()) {
            return snapshotFromFileList(dryRunFileList_,
                                        dryRunSequence_,
                                        WindowsFileDropName,
                                        15);
        }
        if (!dryRunImagePng_.empty()) {
            return snapshotFromImagePng(dryRunImagePng_, dryRunSequence_);
        }
        if (!dryRunImageDib_.empty()) {
            const protocol::ByteBuffer png =
                windowsPngFromDibBytes(dryRunImageDib_);
            return snapshotFromImageDib(
                dryRunImageDib_,
                png,
                dryRunSequence_,
                dryRunImageDibNativeFormatName_.empty()
                    ? WindowsDibName
                    : dryRunImageDibNativeFormatName_,
                dryRunImageDibFormatToken_ == 0 ? defaultDibFormatToken() :
                                                   dryRunImageDibFormatToken_);
        }
        if (!dryRunHtml_.empty()) {
            return snapshotFromCanonicalHtml(stringToBytes(dryRunHtml_),
                                             dryRunSequence_);
        }
        if (!dryRunRtf_.empty()) {
            return snapshotFromCanonicalRtf(stringToBytes(dryRunRtf_),
                                            dryRunSequence_);
        }
        return snapshotFromCanonicalText(stringToBytes(dryRunText_), dryRunSequence_);
    }

    ClipboardSnapshot result;
    const protocol::ResponseStatus status = nativeSnapshot(result);
    if (status != protocol::ResponseStatus::Ok)
        return {};
    return result;
}

protocol::ResponseStatus WindowsClipboardEndpoint::publishBundle(
    const ClipboardPublishRequest& request)
{
    ++diagnostics_.publishes;
    if (request.bundle.offerId == 0 || !hasSupportedFormat(request.bundle))
        return protocol::ResponseStatus::InvalidArgument;

    publishedBundle_ = request.bundle;
    diagnostics_.publishedOfferId = request.bundle.offerId;

    if (options_.useDelayedTextRendering && !options_.dryRun) {
        if (hasFileListFormat(request.bundle) &&
            !hasTextFormat(request.bundle) &&
            !hasHtmlFormat(request.bundle) &&
            !hasRtfFormat(request.bundle) &&
            !hasImagePngFormat(request.bundle) &&
            !hasImageDibFormat(request.bundle)) {
            const TransferReadResult fileList = readBestFileList(request.bundle);
            if (!fileList.ok()) {
                ++diagnostics_.readFailures;
                diagnostics_.lastMessage = fileList.message;
                return fileList.status;
            }
            return nativePublishFileList(request.bundle.offerId,
                                         request.bundle.ownerEpoch,
                                         request.bundle.sequence,
                                         fileList.bytes);
        }
        if (!hasTextFormat(request.bundle) &&
            !hasHtmlFormat(request.bundle) &&
            !hasRtfFormat(request.bundle) &&
            !hasImagePngFormat(request.bundle) &&
            !hasImageDibFormat(request.bundle)) {
            diagnostics_.lastMessage =
                "windows file clipboard publishing requires OLE IDataObject";
            return protocol::ResponseStatus::Unsupported;
        }
        return nativePublishDelayedTextOffer(request.bundle.offerId,
                                            request.bundle.ownerEpoch,
                                            request.bundle.sequence);
    }

    if (!options_.materializeTextOnPublish)
        return protocol::ResponseStatus::Ok;

    TransferReadResult result = readBestText(request.bundle);
    if (!result.ok())
        result = readBestHtml(request.bundle);
    if (!result.ok())
        result = readBestRtf(request.bundle);
    if (!result.ok())
        result = readBestImageDib(request.bundle);
    if (!result.ok())
        result = readBestImagePng(request.bundle);
    if (!result.ok())
        result = readBestImageDib(request.bundle);
    if (!result.ok()) {
        TransferReadResult fileListResult = readBestFileList(request.bundle);
        if (fileListResult.ok())
            return nativeSetRenderedFileList(fileListResult.bytes);

        ++diagnostics_.readFailures;
        diagnostics_.lastMessage =
            fileListResult.message.empty() ? result.message : fileListResult.message;
        return fileListResult.status == protocol::ResponseStatus::NotFound
                   ? result.status
                   : fileListResult.status;
    }

    if (result.canonicalFormat == TextHtmlFormat) {
        if (options_.dryRun) {
            dryRunHtml_ = bytesToString(result.bytes);
            dryRunText_.clear();
            dryRunRtf_.clear();
            dryRunImagePng_.clear();
            dryRunFileList_ = {};
            dryRunFilePaths_.clear();
            dryRunFileGroupDescriptor_.clear();
            ++dryRunSequence_;
            return protocol::ResponseStatus::Ok;
        }
        return nativeSetRenderedHtmlWithOpenClipboard(result.bytes);
    }

    if (result.canonicalFormat == TextRtfFormat) {
        if (options_.dryRun) {
            dryRunRtf_ = bytesToString(result.bytes);
            dryRunText_.clear();
            dryRunHtml_.clear();
            dryRunImagePng_.clear();
            dryRunFileList_ = {};
            dryRunFilePaths_.clear();
            dryRunFileGroupDescriptor_.clear();
            ++dryRunSequence_;
            return protocol::ResponseStatus::Ok;
        }
        return nativePublishRtf(request.bundle.offerId,
                                request.bundle.ownerEpoch,
                                request.bundle.sequence,
                                result.bytes);
    }

    if (result.canonicalFormat == ImagePngFormat) {
        if (options_.dryRun) {
            dryRunImagePng_ = result.bytes;
            dryRunImageDib_.clear();
            dryRunImageDibNativeFormatName_.clear();
            dryRunImageDibFormatToken_ = 0;
            dryRunText_.clear();
            dryRunHtml_.clear();
            dryRunRtf_.clear();
            dryRunFileList_ = {};
            dryRunFilePaths_.clear();
            dryRunFileGroupDescriptor_.clear();
            ++dryRunSequence_;
            return protocol::ResponseStatus::Ok;
        }
        return nativePublishImagePng(request.bundle.offerId,
                                     request.bundle.ownerEpoch,
                                     request.bundle.sequence,
                                     result.bytes);
    }

    if (result.canonicalFormat == ImageDibFormat) {
        if (options_.dryRun) {
            dryRunImageDib_ = result.bytes;
            dryRunImageDibNativeFormatName_ = WindowsDibName;
            dryRunImageDibFormatToken_ = defaultDibFormatToken();
            dryRunImagePng_.clear();
            dryRunText_.clear();
            dryRunHtml_.clear();
            dryRunRtf_.clear();
            dryRunFileList_ = {};
            dryRunFilePaths_.clear();
            dryRunFileGroupDescriptor_.clear();
            ++dryRunSequence_;
            return protocol::ResponseStatus::Ok;
        }
        return nativePublishImageDib(request.bundle.offerId,
                                     request.bundle.ownerEpoch,
                                     request.bundle.sequence,
                                     result.bytes);
    }

    return publishCanonicalText(request.bundle.offerId,
                                request.bundle.ownerEpoch,
                                request.bundle.sequence,
                                result.bytes);
}

protocol::ResponseStatus WindowsClipboardEndpoint::clearPublishedBundle(
    TransferOfferId offerId)
{
    ++diagnostics_.clears;
    if (publishedBundle_.offerId != offerId)
        return protocol::ResponseStatus::NotFound;

    publishedBundle_ = {};
    if (options_.dryRun) {
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while clearing clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    EmptyClipboard();
    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::publishCanonicalText(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence,
    const protocol::ByteBuffer& canonicalUtf8)
{
    if (options_.dryRun) {
        dryRunText_ = bytesToString(canonicalUtf8);
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

    return nativePublishText(offerId, ownerEpoch, sequence, canonicalUtf8);
}

bool WindowsClipboardEndpoint::hasTextFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsTextDescriptorMatches))
            return true;
    }
    return false;
}

bool WindowsClipboardEndpoint::hasHtmlFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsHtmlDescriptorMatches))
            return true;
    }
    return false;
}

bool WindowsClipboardEndpoint::hasFileListFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsFileListDescriptorMatches))
            return true;
    }
    return false;
}

bool WindowsClipboardEndpoint::hasRtfFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsRtfDescriptorMatches))
            return true;
    }
    return false;
}

bool WindowsClipboardEndpoint::hasImagePngFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsPngDescriptorMatches))
            return true;
    }
    return false;
}

bool WindowsClipboardEndpoint::hasImageDibFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsDibDescriptorMatches))
            return true;
    }
    return false;
}

bool WindowsClipboardEndpoint::hasSupportedFormat(
    const TransferSourceBundle& bundle) const
{
    return hasTextFormat(bundle) || hasHtmlFormat(bundle) ||
           hasRtfFormat(bundle) || hasImagePngFormat(bundle) ||
           hasImageDibFormat(bundle) || hasFileListFormat(bundle);
}

DragCoordinateMapResult WindowsClipboardEndpoint::mapDragPoint(
    const DragSurfaceCoordinate& point) const
{
    if (dragCoordinateMapper_ != nullptr)
        return dragCoordinateMapper_->mapToLocalDragPoint(point);

    DragCoordinateMapResult result;
    result.status = protocol::ResponseStatus::Ok;
    result.point = point;
    return result;
}

bool WindowsClipboardEndpoint::hasPendingClipboardChange() const
{
    return pendingNativeClipboardChange_;
}

void WindowsClipboardEndpoint::markClipboardChangeConsumed()
{
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = false;
}

void WindowsClipboardEndpoint::notifyNativeClipboardChanged()
{
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
    ++diagnostics_.nativeChangeNotifications;
}

void WindowsClipboardEndpoint::recordNativeFailure(std::uint32_t nativeError,
                                                   std::string message)
{
    ++diagnostics_.nativeFailures;
    diagnostics_.lastNativeError = nativeError;
    diagnostics_.lastMessage = std::move(message);
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
