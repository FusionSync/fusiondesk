#ifndef FUSIONDESK_PLATFORM_LINUX_CLIPBOARD_LINUX_CLIPBOARD_ENDPOINT_H
#define FUSIONDESK_PLATFORM_LINUX_CLIPBOARD_LINUX_CLIPBOARD_ENDPOINT_H

#include <cstdint>
#include <memory>
#include <string>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace platform {
namespace linux_desktop {
namespace clipboard {

enum class LinuxClipboardBackend : std::uint16_t
{
    Auto = 0,
    X11 = 1,
    Fake = 2
};

struct LinuxClipboardEndpointOptions
{
    LinuxClipboardBackend backend = LinuxClipboardBackend::Auto;
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
    std::uint32_t requestTimeoutMs = 30000;
    std::uint32_t delayedReadTimeoutMs = 30000;
    std::uint32_t streamChunkBytes = 256 * 1024;
    bool expandDroppedDirectories = true;
    bool enableFusePromise = true;
    std::string x11DisplayName;
    std::string fusePromiseRuntimeDir;
    std::string ownerWindowName = "FusionDesk Clipboard";
};

struct LinuxClipboardEndpointDiagnostics
{
    bool started = false;
    int snapshots = 0;
    int publishes = 0;
    int clears = 0;
    int delayedPublishes = 0;
    int delayedRenders = 0;
    int streamChunks = 0;
    std::uint64_t streamBytes = 0;
    int targetListReads = 0;
    int targetDataReads = 0;
    int targetReadFailures = 0;
    int readFailures = 0;
    int fileListSnapshots = 0;
    int delayedRenderCacheHits = 0;
    int delayedRenderCacheStores = 0;
    bool fusePromiseAvailable = false;
    bool fusePromiseActive = false;
    int remoteFilePromisePublishes = 0;
    int remoteFilePromiseFailures = 0;
    int remoteFilePromiseReads = 0;
    int remoteFilePromiseReadFailures = 0;
    std::uint64_t remoteFilePromiseReadBytes = 0;
    std::uint64_t remoteFilePromiseProviders = 0;
    int nativeChangeNotifications = 0;
    int ownerLostNotifications = 0;
    int ownerSuppressions = 0;
    bool nativeChangePending = true;
    modules::clipboard::TransferOfferId publishedOfferId = 0;
    std::uint64_t lastNativeSequence = 0;
    std::uint32_t lastClipbusStatus = 0;
    std::string lastMessage;
};

class LinuxClipboardEndpoint final
    : public modules::clipboard::IClipboardEndpoint,
      public modules::clipboard::IClipboardChangeMonitor
{
public:
    explicit LinuxClipboardEndpoint(
        LinuxClipboardEndpointOptions options = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader> remoteFileReader = {},
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker> remoteObjectLocker = {},
        std::shared_ptr<modules::clipboard::ITransferTranscoder> transcoder = {},
        std::shared_ptr<modules::clipboard::IClipboardCallbackDispatcher>
            callbackDispatcher = {});
    ~LinuxClipboardEndpoint() override;

    LinuxClipboardEndpoint(const LinuxClipboardEndpoint&) = delete;
    LinuxClipboardEndpoint& operator=(const LinuxClipboardEndpoint&) = delete;

    modules::clipboard::ClipboardSnapshot snapshot() override;
    protocol::ResponseStatus publishBundle(
        const modules::clipboard::ClipboardPublishRequest& request) override;
    protocol::ResponseStatus clearPublishedBundle(
        modules::clipboard::TransferOfferId offerId) override;

    bool hasPendingClipboardChange() const override;
    void markClipboardChangeConsumed() override;

    LinuxClipboardEndpointDiagnostics diagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clipboard
} // namespace linux_desktop
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_LINUX_CLIPBOARD_LINUX_CLIPBOARD_ENDPOINT_H
