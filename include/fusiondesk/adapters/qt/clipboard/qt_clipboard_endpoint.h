#ifndef FUSIONDESK_ADAPTERS_QT_CLIPBOARD_QT_CLIPBOARD_ENDPOINT_H
#define FUSIONDESK_ADAPTERS_QT_CLIPBOARD_QT_CLIPBOARD_ENDPOINT_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace clipboard {

struct QtClipboardEndpointOptions
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
    bool expandDroppedDirectories = true;
};

struct QtClipboardEndpointDiagnostics
{
    int snapshots = 0;
    int publishes = 0;
    int clears = 0;
    int nativeChangeNotifications = 0;
    int ownerSuppressions = 0;
    int readFailures = 0;
    int fileListSnapshots = 0;
    int remoteFilePublishes = 0;
    int remoteFileMaterializationFailures = 0;
    std::uint64_t remoteFilesMaterialized = 0;
    std::uint64_t remoteDirectoriesMaterialized = 0;
    std::uint64_t remoteFileBytesMaterialized = 0;
    bool nativeChangePending = true;
    modules::clipboard::TransferOfferId publishedOfferId = 0;
    std::uint64_t lastNativeSequence = 0;
    std::string lastMessage;
};

class QtClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IClipboardChangeMonitor
{
public:
    explicit QtClipboardEndpoint(
        QtClipboardEndpointOptions options = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader> remoteFileReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker> remoteObjectLocker = {});
    ~QtClipboardEndpoint() override;

    modules::clipboard::ClipboardSnapshot snapshot() override;
    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override;
    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId offerId) override;

    bool hasPendingClipboardChange() const override;
    void markClipboardChangeConsumed() override;

    QtClipboardEndpointDiagnostics diagnostics() const;

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
    protocol::ResponseStatus publishImagePngBundle(
        const modules::clipboard::TransferSourceBundle& bundle);
    modules::clipboard::ClipboardSnapshot snapshotFromText(
        const std::string& text,
        std::uint64_t sequence);
    modules::clipboard::ClipboardSnapshot snapshotFromFormattedText(
        const std::string& text,
        const std::string& html,
        const std::string& rtf,
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
    void handleNativeClipboardChanged();
    void cleanupMaterializedRemoteFiles();

private:
    QtClipboardEndpointOptions options_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
        remoteFileReader_;
    std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
        remoteObjectLocker_;
    QtClipboardEndpointDiagnostics diagnostics_;
    std::string lastPublishedText_;
    std::string lastPublishedHtml_;
    std::string lastPublishedRtf_;
    protocol::ByteBuffer lastPublishedImagePng_;
    std::filesystem::path materializedRemoteFilesRoot_;
    std::uint64_t nextBundleId_ = 1;
    std::uint64_t nextOfferId_ = 1;
    std::uint64_t nextSourceId_ = 1;
    std::uint64_t nextFormatId_ = 1;
    bool pendingNativeClipboardChange_ = true;
    bool settingClipboard_ = false;
    void* changeConnection_ = nullptr;
};

} // namespace clipboard
} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_CLIPBOARD_QT_CLIPBOARD_ENDPOINT_H
