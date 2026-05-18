#ifndef FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_ENDPOINT_H
#define FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_ENDPOINT_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_drag_coordinates.h"
#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

struct WindowsClipboardEndpointOptions
{
    bool dryRun = true;
    bool suppressOwnClipboardUpdates = true;
    bool writeOwnerMarker = true;
    bool materializeTextOnPublish = true;
    bool useDelayedTextRendering = true;
    bool enableNativeDragLoop = false;
    bool nativeDragPreflightOnly = false;
    bool enableNativeClipboardWatcher = true;
    std::uint32_t openRetryCount = 50;
    std::uint32_t openRetryDelayMs = 10;
    std::uint32_t delayedReadTimeoutMs = 1000;
    modules::clipboard::TransferBundleId firstBundleId = 1;
    modules::clipboard::TransferOfferId firstOfferId = 1;
    modules::clipboard::TransferSourceId firstSourceId = 1;
    modules::clipboard::TransferFormatId firstFormatId = 1;
    protocol::SessionId originSessionId = 0;
    modules::clipboard::PolicyVersion policyVersion = 0;
    std::uint64_t maxInlineBytes = 1024 * 1024;
    std::uint64_t maxFileRangeBytes = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t maxSingleFileBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    std::uint32_t maxFileCount = 1024;
    std::uint32_t maxDirectoryDepth = 32;
    bool expandDroppedDirectories = true;
};

struct WindowsClipboardEndpointDiagnostics
{
    bool dryRun = true;
    int snapshots = 0;
    int nativeSnapshots = 0;
    int publishes = 0;
    int clears = 0;
    int delayedPublishes = 0;
    int delayedRenders = 0;
    int delayedRenderAlls = 0;
    int htmlRenders = 0;
    int rtfRenders = 0;
    int imagePngRenders = 0;
    int imageDibRenders = 0;
    int fileListSnapshots = 0;
    int fileListRenders = 0;
    int dragStarts = 0;
    int dragMoves = 0;
    int dragDrops = 0;
    int dragCancels = 0;
    int nativeDragLoops = 0;
    int nativeDragPreflights = 0;
    int nativeDragPreflightReads = 0;
    std::uint64_t nativeDragPreflightBytes = 0;
    int nativeDragDrops = 0;
    int nativeDragCancels = 0;
    bool nativeWatcherRegistered = false;
    bool nativeChangePending = true;
    int nativeChangeNotifications = 0;
    int ownerSuppressions = 0;
    int readFailures = 0;
    int nativeFailures = 0;
    std::uint32_t lastNativeError = 0;
    std::uint64_t lastNativeSequence = 0;
    modules::clipboard::DragSessionId lastDragSessionId = 0;
    modules::clipboard::DragSessionId activeDragSessionId = 0;
    std::int32_t lastDragX = 0;
    std::int32_t lastDragY = 0;
    modules::clipboard::TransferAction lastDragAction =
        modules::clipboard::TransferAction::None;
    modules::clipboard::TransferOfferId publishedOfferId = 0;
    std::string lastMessage;
};

protocol::ByteBuffer windowsCfUnicodeTextFromCanonicalUtf8(
    const protocol::ByteBuffer& canonicalUtf8);
protocol::ByteBuffer canonicalUtf8FromWindowsCfUnicodeText(
    const protocol::ByteBuffer& cfUnicodeText);
protocol::ByteBuffer windowsHtmlFromCanonicalHtml(
    const protocol::ByteBuffer& canonicalHtml);
protocol::ByteBuffer canonicalHtmlFromWindowsHtml(
    const protocol::ByteBuffer& windowsHtml);
protocol::ByteBuffer windowsFileGroupDescriptorFromTransferFileList(
    const modules::clipboard::TransferFileList& fileList);
modules::clipboard::TransferFileListDecodeResult
transferFileListFromWindowsFileGroupDescriptor(
    const protocol::ByteBuffer& fileGroupDescriptor);
std::uint32_t windowsHtmlFormatToken();
std::uint32_t windowsRtfFormatToken();
std::uint32_t windowsPngFormatToken();
std::uint32_t windowsFileGroupDescriptorFormatToken();

class WindowsClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IRemoteDragCoordinateSink,
      public modules::clipboard::IClipboardChangeMonitor
{
public:
    explicit WindowsClipboardEndpoint(
        WindowsClipboardEndpointOptions options = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader> remoteFileReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker> remoteObjectLocker = {},
        std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper> dragCoordinateMapper = {});
    ~WindowsClipboardEndpoint() override;

    modules::clipboard::ClipboardSnapshot snapshot() override;
    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override;
    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId offerId) override;
    protocol::ResponseStatus dragStart(
        const modules::clipboard::DragSessionStart& start) override;
    protocol::ResponseStatus dragMove(
        modules::clipboard::DragSessionId dragSessionId,
        const modules::clipboard::DragSurfaceCoordinate& point,
        modules::clipboard::TransferAction proposedAction) override;
    protocol::ResponseStatus dragDrop(
        modules::clipboard::DragSessionId dragSessionId,
        const modules::clipboard::DragSurfaceCoordinate& point,
        modules::clipboard::TransferAction proposedAction) override;
    protocol::ResponseStatus dragCancel(
        modules::clipboard::DragSessionId dragSessionId,
        modules::clipboard::DragCancelReason reason) override;
    protocol::ResponseStatus renderDelayedFormatForNative(
        std::uint32_t nativeFormatToken);
    protocol::ResponseStatus renderAllDelayedFormatsForNative();
    bool hasPendingClipboardChange() const override;
    void markClipboardChangeConsumed() override;
    void notifyNativeClipboardChanged();

    void setDryRunClipboardText(std::string text);
    void setDryRunClipboardRtf(std::string rtf);
    void setDryRunClipboardImagePng(protocol::ByteBuffer png);
    void setDryRunClipboardImageDib(protocol::ByteBuffer dib,
                                    std::string nativeFormatName,
                                    std::uint32_t localFormatToken);
    void setDryRunClipboardFileList(
        modules::clipboard::TransferFileList fileList);
#if defined(_WIN32)
    protocol::ResponseStatus setDryRunClipboardLocalFiles(
        std::vector<std::wstring> nativePaths);
#endif
    std::string dryRunClipboardText() const;
    std::string dryRunClipboardHtml() const;
    std::string dryRunClipboardRtf() const;
    protocol::ByteBuffer dryRunClipboardImagePng() const;
    protocol::ByteBuffer dryRunClipboardImageDib() const;
    modules::clipboard::TransferFileList dryRunClipboardFileList() const;
    protocol::ByteBuffer dryRunFileGroupDescriptor() const;
    WindowsClipboardEndpointDiagnostics diagnostics() const;

private:
    modules::clipboard::ClipboardSnapshot snapshotFromCanonicalText(
        const protocol::ByteBuffer& canonicalUtf8,
        std::uint64_t nativeSequence);
    modules::clipboard::ClipboardSnapshot snapshotFromCanonicalHtml(
        const protocol::ByteBuffer& canonicalHtml,
        std::uint64_t nativeSequence);
    modules::clipboard::ClipboardSnapshot snapshotFromCanonicalRtf(
        const protocol::ByteBuffer& canonicalRtf,
        std::uint64_t nativeSequence);
    modules::clipboard::ClipboardSnapshot snapshotFromImagePng(
        const protocol::ByteBuffer& png,
        std::uint64_t nativeSequence,
        const std::string& nativeFormatName = {},
        std::uint32_t localFormatToken = 0,
        modules::clipboard::TransferEncodingMode preferredEncoding =
            modules::clipboard::TransferEncodingMode::CanonicalBytes);
    modules::clipboard::ClipboardSnapshot snapshotFromImageDib(
        const protocol::ByteBuffer& dib,
        const protocol::ByteBuffer& png,
        std::uint64_t nativeSequence,
        const std::string& nativeFormatName,
        std::uint32_t localFormatToken);
    modules::clipboard::ClipboardSnapshot snapshotFromFileList(
        const modules::clipboard::TransferFileList& fileList,
        std::uint64_t nativeSequence,
        const std::string& nativeFormatName,
        std::uint32_t localFormatToken);
    modules::clipboard::TransferReadResult readBestText(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestHtml(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestRtf(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestImagePng(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestImageDib(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestFileList(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestFormat(
        const modules::clipboard::TransferSourceBundle& bundle,
        const std::string& canonicalFormat,
        const std::string& nativeFormatName,
        std::uint32_t localFormatToken);
    protocol::ResponseStatus publishCanonicalText(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence,
        const protocol::ByteBuffer& canonicalUtf8);
    protocol::ResponseStatus nativeSnapshot(
        modules::clipboard::ClipboardSnapshot& output);
    protocol::ResponseStatus nativePublishText(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence,
        const protocol::ByteBuffer& canonicalUtf8);
    protocol::ResponseStatus nativePublishRtf(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence,
        const protocol::ByteBuffer& canonicalRtf);
    protocol::ResponseStatus nativePublishImagePng(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence,
        const protocol::ByteBuffer& png);
    protocol::ResponseStatus nativePublishImageDib(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence,
        const protocol::ByteBuffer& dib);
    protocol::ResponseStatus nativePublishDelayedTextOffer(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence);
    protocol::ResponseStatus nativePublishFileList(
        modules::clipboard::TransferOfferId offerId,
        std::uint64_t ownerEpoch,
        std::uint64_t sequence,
        const protocol::ByteBuffer& canonicalFileList);
    protocol::ResponseStatus nativeSetRenderedText(
        const protocol::ByteBuffer& canonicalUtf8);
    protocol::ResponseStatus nativeSetRenderedTextWithOpenClipboard(
        const protocol::ByteBuffer& canonicalUtf8);
    protocol::ResponseStatus nativeSetRenderedHtml(
        const protocol::ByteBuffer& canonicalHtml);
    protocol::ResponseStatus nativeSetRenderedHtmlWithOpenClipboard(
        const protocol::ByteBuffer& canonicalHtml);
    protocol::ResponseStatus nativeSetRenderedRtf(
        const protocol::ByteBuffer& canonicalRtf);
    protocol::ResponseStatus nativeSetRenderedRtfWithOpenClipboard(
        const protocol::ByteBuffer& canonicalRtf);
    protocol::ResponseStatus nativeSetRenderedImagePng(
        const protocol::ByteBuffer& png);
    protocol::ResponseStatus nativeSetRenderedImagePngWithOpenClipboard(
        const protocol::ByteBuffer& png);
    protocol::ResponseStatus nativeSetRenderedImageDib(
        const protocol::ByteBuffer& png,
        std::uint32_t nativeFormatToken);
    protocol::ResponseStatus nativeSetRenderedImageDibBytes(
        const protocol::ByteBuffer& dib,
        std::uint32_t nativeFormatToken);
    protocol::ResponseStatus nativeSetRenderedImageDibWithOpenClipboard(
        const protocol::ByteBuffer& png,
        std::uint32_t nativeFormatToken);
    protocol::ResponseStatus nativeSetRenderedImageDibBytesWithOpenClipboard(
        const protocol::ByteBuffer& dib,
        std::uint32_t nativeFormatToken);
    protocol::ResponseStatus nativeSetRenderedFileList(
        const protocol::ByteBuffer& canonicalFileList);
    protocol::ResponseStatus nativeStartDrag(
        const modules::clipboard::DragSessionStart& start);
    bool hasTextFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasHtmlFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasRtfFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasImagePngFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasImageDibFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasFileListFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasSupportedFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    modules::clipboard::DragCoordinateMapResult mapDragPoint(
        const modules::clipboard::DragSurfaceCoordinate& point) const;
    void recordNativeFailure(std::uint32_t nativeError,
                             std::string message);

private:
    WindowsClipboardEndpointOptions options_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader> remoteFileReader_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker> remoteObjectLocker_;
    std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper> dragCoordinateMapper_;
    modules::clipboard::TransferSourceBundle publishedBundle_;
    WindowsClipboardEndpointDiagnostics diagnostics_;
    std::string dryRunText_;
    std::string dryRunHtml_;
    std::string dryRunRtf_;
    protocol::ByteBuffer dryRunImagePng_;
    protocol::ByteBuffer dryRunImageDib_;
    std::string dryRunImageDibNativeFormatName_;
    std::uint32_t dryRunImageDibFormatToken_ = 0;
    modules::clipboard::TransferFileList dryRunFileList_;
    std::vector<std::wstring> dryRunFilePaths_;
    protocol::ByteBuffer dryRunFileGroupDescriptor_;
    modules::clipboard::DragSessionId activeDragSessionId_ = 0;
    std::uint64_t dryRunSequence_ = 0;
    std::uint64_t nextBundleId_ = 1;
    std::uint64_t nextOfferId_ = 1;
    std::uint64_t nextSourceId_ = 1;
    std::uint64_t nextFormatId_ = 1;
    bool nativeClipboardWatcherRegistered_ = false;
    bool pendingNativeClipboardChange_ = true;
};

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_ENDPOINT_H
