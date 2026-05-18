#ifndef FUSIONDESK_PLATFORM_MACOS_CLIPBOARD_MAC_CLIPBOARD_ENDPOINT_H
#define FUSIONDESK_PLATFORM_MACOS_CLIPBOARD_MAC_CLIPBOARD_ENDPOINT_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace platform {
namespace macos {
namespace clipboard {

struct MacClipboardEndpointOptions
{
    bool suppressOwnClipboardUpdates = true;
    bool enableChangeMonitor = true;
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
    std::uint32_t delayedReadTimeoutMs = 1000;
    bool expandDroppedDirectories = true;
};

struct MacClipboardEndpointDiagnostics
{
    int snapshots = 0;
    int publishes = 0;
    int clears = 0;
    int delayedPublishes = 0;
    int delayedRenders = 0;
    int readFailures = 0;
    int fileListSnapshots = 0;
    int remoteFilePromisePublishes = 0;
    int remoteFilePromiseFailures = 0;
    std::uint64_t remoteFilePromiseProviders = 0;
    bool nativeChangePending = true;
    std::int64_t lastNativeChangeCount = -1;
    modules::clipboard::TransferOfferId publishedOfferId = 0;
    std::uint64_t lastNativeSequence = 0;
    std::string lastMessage;
};

class MacClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IClipboardChangeMonitor
{
public:
    explicit MacClipboardEndpoint(
        MacClipboardEndpointOptions options = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader> remoteFileReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker> remoteObjectLocker = {});
    ~MacClipboardEndpoint() override;

    modules::clipboard::ClipboardSnapshot snapshot() override;
    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override;
    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId offerId) override;

    bool hasPendingClipboardChange() const override;
    void markClipboardChangeConsumed() override;

    MacClipboardEndpointDiagnostics diagnostics() const;

    modules::clipboard::TransferReadResult renderPasteboardDataForNativeType(
        const std::string& nativeType);

private:
    modules::clipboard::TransferReadResult readBestFormat(
        const modules::clipboard::TransferSourceBundle& bundle,
        const std::string& canonicalFormat,
        bool (*descriptorMatches)(
            const modules::clipboard::TransferFormatDescriptor&),
        const std::string& notFoundMessage);
    modules::clipboard::TransferReadResult readBestText(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestHtml(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestRtf(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::TransferReadResult readBestImagePng(
        const modules::clipboard::TransferSourceBundle& bundle);
    protocol::ResponseStatus publishFileListBundle(
        const modules::clipboard::TransferSourceBundle& bundle);
    protocol::ResponseStatus publishDelayedBundle(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::ClipboardSnapshot snapshotFromText(
        const std::string& text,
        std::uint64_t sequence);
    modules::clipboard::ClipboardSnapshot snapshotFromFormattedText(
        const std::string& text,
        const protocol::ByteBuffer& html,
        const protocol::ByteBuffer& rtf,
        std::uint64_t sequence);
    modules::clipboard::ClipboardSnapshot snapshotFromImagePng(
        const protocol::ByteBuffer& pngBytes,
        std::uint32_t width,
        std::uint32_t height,
        std::uint64_t sequence);
    modules::clipboard::ClipboardSnapshot snapshotFromFileList(
        const modules::clipboard::TransferFileList& fileList,
        std::vector<std::filesystem::path> paths,
        std::uint64_t sequence);
    bool hasTextFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasHtmlFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasRtfFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasImagePngFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    bool hasSupportedDelayedFormat(
        const modules::clipboard::TransferSourceBundle& bundle) const;
    void cleanupRemoteFilePublicationState();

private:
    MacClipboardEndpointOptions options_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
        remoteFileReader_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
        remoteObjectLocker_;
    modules::clipboard::TransferSourceBundle publishedBundle_;
    MacClipboardEndpointDiagnostics diagnostics_;
    modules::clipboard::TransferBundleId nextBundleId_ = 1;
    modules::clipboard::TransferOfferId nextOfferId_ = 1;
    modules::clipboard::TransferSourceId nextSourceId_ = 1;
    modules::clipboard::TransferFormatId nextFormatId_ = 1;
    mutable bool pendingNativeClipboardChange_ = true;
    mutable std::int64_t lastObservedChangeCount_ = -1;
    std::int64_t lastPublishedChangeCount_ = -1;
    void* pasteboardProvider_ = nullptr;
    void* filePromiseDelegates_ = nullptr;
};

} // namespace clipboard
} // namespace macos
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_MACOS_CLIPBOARD_MAC_CLIPBOARD_ENDPOINT_H
