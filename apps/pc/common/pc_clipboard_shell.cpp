#include "pc_clipboard_shell.h"

#include "pc_clipboard_drag_mapper.h"
#include "pc_shell_options.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>

#include "fusiondesk/adapters/qt/qt_tcp_transport_socket.h"
#include "fusiondesk/core/session/session.h"
#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
#include "fusiondesk/adapters/qt/clipboard/qt_clipboard_endpoint.h"
#endif
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"
#endif
#if defined(FUSIONDESK_PC_HAS_MACOS_FEATURE_ADAPTERS)
#include "fusiondesk/platform/macos/clipboard/mac_clipboard_endpoint.h"
#endif
#if defined(FUSIONDESK_PC_HAS_LINUX_FEATURE_ADAPTERS)
#include "fusiondesk/platform/linux/clipboard/linux_clipboard_endpoint.h"
#endif
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"
#include "fusiondesk/runtime/qt/qt_event_loop_bridge.h"
#include "fusiondesk/runtime/feature/clipboard_product_presenter.h"

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

std::uint32_t uint32FromUint64(std::uint64_t value,
                               std::uint32_t fallback)
{
    if (value == 0)
        return fallback;
    if (value > std::numeric_limits<std::uint32_t>::max())
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(value);
}

std::string bytesToString(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

class QtClipboardCallbackDispatcher final
    : public modules::clipboard::IClipboardCallbackDispatcher
{
public:
    bool postClipboardTask(
        modules::clipboard::ClipboardCallbackTask task) override
    {
        return runtime::qt::QtEventLoopBridge::post(std::move(task));
    }

    bool runClipboardTaskAndWait(
        modules::clipboard::ClipboardCallbackTask task,
        std::uint32_t) override
    {
        QCoreApplication* application = QCoreApplication::instance();
        if (!task || application == nullptr)
            return false;

        if (application->thread() == QThread::currentThread()) {
            task();
            return true;
        }

        return QMetaObject::invokeMethod(application,
                                         [task = std::move(task)]() mutable {
                                             task();
                                         },
                                         Qt::BlockingQueuedConnection);
    }
};

modules::clipboard::TransferReadResult readEndpointPlainText(
    modules::clipboard::IClipboardEndpoint& endpoint)
{
    using namespace modules::clipboard;

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    for (const std::shared_ptr<TransferSource>& source :
         snapshot.bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        auto it = std::find_if(
            formats.begin(),
            formats.end(),
            [](const TransferFormatDescriptor& descriptor) {
                return descriptor.canonicalFormat == TextPlainUtf8Format;
            });
        if (it == formats.end())
            continue;

        TransferReadRequest request;
        request.bundleId = snapshot.bundle.bundleId;
        request.offerId = snapshot.bundle.offerId;
        request.ownerEpoch = snapshot.bundle.ownerEpoch;
        request.sourceId = source->id();
        request.itemIndex = it->itemIndex;
        request.formatId = it->formatId;
        request.localFormatToken = it->localFormatToken;
        request.canonicalFormat = TextPlainUtf8Format;
        request.acceptedMaxBytes = it->estimatedBytes == 0
                                       ? 1024 * 1024
                                       : it->estimatedBytes;
        request.streamAccepted = false;
        request.requestedEncoding = TransferEncodingMode::CanonicalBytes;
        TransferReadResult result = source->read(request);
        if (result.ok())
            return result;
        return result;
    }

    TransferReadResult result;
    result.status = protocol::ResponseStatus::NotFound;
    result.message = "clipboard endpoint text/plain format is not available";
    return result;
}

protocol::ByteBuffer stringToBytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

const char* clipboardSessionRoleName(session::SessionRole role)
{
    switch (role) {
    case session::SessionRole::Client:
        return "client";
    case session::SessionRole::Agent:
        return "agent";
    case session::SessionRole::Auth:
        return "auth";
    case session::SessionRole::Relay:
        return "relay";
    case session::SessionRole::Standalone:
        return "standalone";
    }
    return "unknown";
}

modules::clipboard::TransferSourceBundle makeTextSeedBundle(
    const std::string& text)
{
    using namespace modules::clipboard;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 1;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = text.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = stringToBytes(text);

    TransferSourceBundle bundle;
    bundle.bundleId = 1;
    bundle.offerId = 1;
    bundle.ownerEpoch = 1;
    bundle.sequence = 1;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        1,
        std::vector<MaterializedTransferEntry>{std::move(entry)}));

    TransferPresentation presentation;
    presentation.displayName = "PC clipboard seed text";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    bundle.presentation = std::move(presentation);
    return bundle;
}

bool windowsClipboardNativeRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--windows-clipboard-native") ||
           envFlagEnabled("FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE");
}

bool windowsClipboardNativeDragLoopRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--windows-clipboard-native-drag-loop") ||
           envFlagEnabled("FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE_DRAG_LOOP");
}

bool windowsClipboardNativeDragPreflightRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--windows-clipboard-native-drag-preflight") ||
           envFlagEnabled("FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE_DRAG_PREFLIGHT");
}

void pumpPcSessionTransports(
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId)
{
    runtime::qt::QtSessionTransportConnector* connector =
        transportManager.connector(sessionId);
    if (connector == nullptr)
        return;

    for (const std::shared_ptr<adapters::qt::QtTcpTransportSocket>& transport :
         connector->transports())
        transport->poll();
}

class PcClipboardRuntimeReadPump final
    : public runtime::feature::IClipboardRuntimeReadPump
{
public:
    PcClipboardRuntimeReadPump(
        runtime::qt::QtRuntimeTransportManager& transportManager,
        protocol::SessionId sessionId)
        : transportManager_(transportManager),
          sessionId_(sessionId)
    {
    }

    void pumpOnce() override
    {
        pumpPcSessionTransports(transportManager_, sessionId_);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    std::uint64_t monotonicNowUsec() const override
    {
        return runtime::qt::QtTimerBridge::monotonicNowUsec();
    }

private:
    runtime::qt::QtRuntimeTransportManager& transportManager_;
    protocol::SessionId sessionId_ = 0;
};

} // namespace

PcClipboardRuntimeContext::PcClipboardRuntimeContext() = default;

PcClipboardRuntimeContext::~PcClipboardRuntimeContext() = default;

bool clipboardProfileRequested(int argc, char** argv)
{
    if (hasArg(argc, argv, "--mount-clipboard") ||
        hasArg(argc, argv, "--start-clipboard") ||
        hasArg(argc, argv, "--pump-clipboard") ||
        hasArg(argc, argv, "--clipboard-endpoint") ||
        hasArg(argc, argv, "--clipboard-seed-text") ||
        hasArg(argc, argv, "--clipboard-seed-html-file") ||
        hasArg(argc, argv, "--clipboard-seed-rtf-file") ||
        hasArg(argc, argv, "--clipboard-seed-image-png") ||
        hasArg(argc, argv, "--clipboard-seed-file") ||
        hasArg(argc, argv, "--clipboard-policy-file") ||
        hasArg(argc, argv, "--clipboard-policy-export-file") ||
        hasArg(argc, argv, "--clipboard-send-drag-drop") ||
        hasArg(argc, argv, "--clipboard-dry-run-text") ||
        hasArg(argc, argv, "--require-clipboard-text") ||
        hasArg(argc, argv, "--require-clipboard-html-file") ||
        hasArg(argc, argv, "--require-clipboard-rtf-file") ||
        hasArg(argc, argv, "--require-clipboard-image-png") ||
        hasArg(argc, argv, "--require-clipboard-endpoint-file-text") ||
        hasArg(argc, argv, "--require-clipboard-file-text")) {
        return true;
    }

    for (const std::string& moduleId : optionValues(argc, argv, "--profile-module")) {
        if (moduleId == "clipboard.redirect" ||
            moduleId == "clipboard.redirect.client" ||
            moduleId == "clipboard.redirect.agent") {
            return true;
        }
    }

    return false;
}

std::string clipboardEndpointKindOptionValue(int argc, char** argv)
{
    const std::string endpointKind =
        optionValue(argc, argv, "--clipboard-endpoint");
    return endpointKind.empty() ? "auto" : endpointKind;
}

bool qtClipboardEndpointRequested(int argc, char** argv)
{
    return clipboardEndpointKindOptionValue(argc, argv) == "qt";
}

runtime::ProductClipboardPolicy productClipboardPolicyOptionValue(
    int argc,
    char** argv,
    runtime::ProductClipboardPolicy policy)
{
    modules::clipboard::ClipboardPolicy& modulePolicy = policy.modulePolicy;
    runtime::feature::ClipboardRuntimePolicyRules& runtimeRules =
        policy.runtimeRules;

    if (hasArg(argc, argv, "--clipboard-no-announce"))
        modulePolicy.allowAnnounce = false;
    if (hasArg(argc, argv, "--clipboard-no-receive"))
        modulePolicy.allowReceive = false;
    if (hasArg(argc, argv, "--clipboard-no-send-content"))
        modulePolicy.allowSendContent = false;
    if (hasArg(argc, argv, "--clipboard-no-write-local"))
        modulePolicy.allowWriteLocal = false;
    if (hasArg(argc, argv, "--clipboard-no-plain-text"))
        modulePolicy.allowPlainText = false;
    if (hasArg(argc, argv, "--clipboard-no-html"))
        modulePolicy.allowHtml = false;
    if (hasArg(argc, argv, "--clipboard-no-rtf"))
        modulePolicy.allowRtf = false;
    if (hasArg(argc, argv, "--clipboard-no-image"))
        modulePolicy.allowImage = false;
    if (hasArg(argc, argv, "--clipboard-no-file-list"))
        modulePolicy.allowFileList = false;
    if (hasArg(argc, argv, "--clipboard-no-file-contents"))
        modulePolicy.allowFileContents = false;
    if (hasArg(argc, argv, "--clipboard-no-drag"))
        modulePolicy.allowDrag = false;
    if (hasArg(argc, argv, "--clipboard-allow-custom-formats"))
        modulePolicy.allowCustomFormats = true;
    modulePolicy.maxInlineBytes =
        uint64OptionValue(argc,
                          argv,
                          "--clipboard-max-inline-bytes",
                          modulePolicy.maxInlineBytes);
    modulePolicy.maxFileRangeBytes =
        uint64OptionValue(argc,
                          argv,
                          "--clipboard-max-file-range-bytes",
                          modulePolicy.maxFileRangeBytes);
    modulePolicy.maxFileCount =
        uint64OptionValue(argc,
                          argv,
                          "--clipboard-max-file-count",
                          modulePolicy.maxFileCount);
    modulePolicy.maxSingleFileBytes =
        uint64OptionValue(argc,
                          argv,
                          "--clipboard-max-single-file-bytes",
                          modulePolicy.maxSingleFileBytes);

    if (hasArg(argc, argv, "--clipboard-runtime-audit"))
        runtimeRules.auditAllowed = true;
    const std::uint64_t maxRecentAuditEvents =
        uint64OptionValue(argc,
                          argv,
                          "--clipboard-runtime-max-audit-events",
                          runtimeRules.maxRecentAuditEvents);
    runtimeRules.maxRecentAuditEvents =
        maxRecentAuditEvents >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(maxRecentAuditEvents);
    if (!modulePolicy.allowFileContents) {
        runtimeRules.allowRemoteFileRangeRead = false;
        runtimeRules.allowRemoteObjectLock = false;
        runtimeRules.allowRemoteObjectUnlock = false;
    }
    if (hasArg(argc, argv, "--clipboard-runtime-deny-announce"))
        runtimeRules.allowLocalSnapshotAnnounce = false;
    if (hasArg(argc, argv, "--clipboard-runtime-deny-read"))
        runtimeRules.allowRemoteFormatRead = false;
    if (hasArg(argc, argv, "--clipboard-runtime-deny-file-range"))
        runtimeRules.allowRemoteFileRangeRead = false;
    if (hasArg(argc, argv, "--clipboard-runtime-deny-object-lock"))
        runtimeRules.allowRemoteObjectLock = false;
    if (hasArg(argc, argv, "--clipboard-runtime-deny-object-unlock"))
        runtimeRules.allowRemoteObjectUnlock = false;
    if (hasArg(argc, argv, "--clipboard-runtime-deny-expiry"))
        runtimeRules.allowPendingReadExpiry = false;

    return policy;
}

modules::clipboard::ClipboardPolicy clipboardPolicyOptionValue(int argc,
                                                               char** argv)
{
    return productClipboardPolicyOptionValue(argc, argv).modulePolicy;
}

modules::clipboard::ClipboardPolicy clipboardPolicyOptionValue(
    int argc,
    char** argv,
    runtime::ProductClipboardPolicy policy)
{
    return productClipboardPolicyOptionValue(argc, argv, std::move(policy))
        .modulePolicy;
}

std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>
makeClipboardRuntimePolicy(
    int argc,
    char** argv,
    const modules::clipboard::ClipboardPolicy& clipboardPolicy)
{
    runtime::ProductClipboardPolicy productPolicy;
    productPolicy.modulePolicy = clipboardPolicy;
    productPolicy =
        productClipboardPolicyOptionValue(argc, argv, std::move(productPolicy));
    return makeClipboardRuntimePolicy(productPolicy);
}

std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>
makeClipboardRuntimePolicy(const runtime::ProductClipboardPolicy& policy)
{
    return runtime::feature::makeClipboardRuntimePolicyFromProductPolicy(policy);
}

std::shared_ptr<modules::clipboard::IClipboardEndpoint>
makeClipboardEndpoint(
    int argc,
    char** argv,
    const modules::clipboard::ClipboardPolicy& policy,
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> remoteReader,
    std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
        dragCoordinateMapper)
{
    const std::string endpointKind = clipboardEndpointKindOptionValue(argc, argv);
    if (endpointKind != "auto" && endpointKind != "windows" &&
        endpointKind != "macos" && endpointKind != "linux" &&
        endpointKind != "qt") {
        writeShellError("unsupported clipboard endpoint: " + endpointKind);
        return nullptr;
    }

#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
    if (endpointKind == "auto" || endpointKind == "windows") {
        platform::windows::clipboard::WindowsClipboardEndpointOptions options;
        const bool nativeDragPreflight =
            windowsClipboardNativeDragPreflightRequested(argc, argv);
        options.dryRun = !windowsClipboardNativeRequested(argc, argv);
        options.suppressOwnClipboardUpdates =
            !hasArg(argc, argv, "--clipboard-no-owner-suppression");
        options.writeOwnerMarker =
            !hasArg(argc, argv, "--clipboard-no-owner-marker");
        options.useDelayedTextRendering =
            !hasArg(argc, argv, "--clipboard-no-delayed-rendering");
        options.enableNativeDragLoop =
            windowsClipboardNativeDragLoopRequested(argc, argv) ||
            nativeDragPreflight;
        options.nativeDragPreflightOnly = nativeDragPreflight;
        options.openRetryCount =
            uint32FromUint64(uint64OptionValue(argc,
                                               argv,
                                               "--clipboard-open-retry-count",
                                               options.openRetryCount),
                             options.openRetryCount);
        options.openRetryDelayMs =
            uint32FromUint64(uint64OptionValue(argc,
                                               argv,
                                               "--clipboard-open-retry-delay-ms",
                                               options.openRetryDelayMs),
                             options.openRetryDelayMs);
        options.maxInlineBytes = policy.maxInlineBytes;
        options.maxFileRangeBytes = policy.maxFileRangeBytes;
        options.maxSingleFileBytes = policy.maxSingleFileBytes;
        options.maxFileCount =
            uint32FromUint64(policy.maxFileCount, options.maxFileCount);
        options.maxDirectoryDepth =
            uint32FromUint64(
                uint64OptionValue(argc,
                                  argv,
                                  "--clipboard-max-directory-depth",
                                  options.maxDirectoryDepth),
                options.maxDirectoryDepth);
        options.expandDroppedDirectories =
            !hasArg(argc, argv, "--clipboard-no-expand-directories");
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
            remoteFileReader =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteFileReader>(
                    remoteReader);
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
            remoteObjectLocker =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteObjectLocker>(
                    remoteReader);
        auto endpoint =
            std::make_shared<
                platform::windows::clipboard::WindowsClipboardEndpoint>(
                options,
                std::move(remoteReader),
                std::move(remoteFileReader),
                std::move(remoteObjectLocker),
                dragCoordinateMapper != nullptr
                    ? std::move(dragCoordinateMapper)
                    : makeClipboardDragCoordinateMapper(argc, argv));
        const std::string dryRunText =
            optionValue(argc, argv, "--clipboard-dry-run-text");
        if (!dryRunText.empty())
            endpoint->setDryRunClipboardText(dryRunText);
        return endpoint;
    }
#else
    (void)dragCoordinateMapper;
#endif

#if defined(FUSIONDESK_PC_HAS_MACOS_FEATURE_ADAPTERS)
    if (endpointKind == "auto" || endpointKind == "macos") {
        platform::macos::clipboard::MacClipboardEndpointOptions options;
        options.suppressOwnClipboardUpdates =
            !hasArg(argc, argv, "--clipboard-no-owner-suppression");
        options.maxInlineBytes = policy.maxInlineBytes;
        options.maxFileRangeBytes = policy.maxFileRangeBytes;
        options.maxSingleFileBytes = policy.maxSingleFileBytes;
        options.maxFileCount =
            uint32FromUint64(policy.maxFileCount, options.maxFileCount);
        options.maxDirectoryDepth =
            uint32FromUint64(
                uint64OptionValue(argc,
                                  argv,
                                  "--clipboard-max-directory-depth",
                                  options.maxDirectoryDepth),
                options.maxDirectoryDepth);
        options.expandDroppedDirectories =
            !hasArg(argc, argv, "--clipboard-no-expand-directories");
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
            remoteFileReader =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteFileReader>(
                    remoteReader);
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
            remoteObjectLocker =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteObjectLocker>(
                    remoteReader);
        return std::make_shared<
            platform::macos::clipboard::MacClipboardEndpoint>(
            options,
            std::move(remoteReader),
            std::move(remoteFileReader),
            std::move(remoteObjectLocker));
    }
#endif

#if defined(FUSIONDESK_PC_HAS_LINUX_FEATURE_ADAPTERS)
    if (endpointKind == "auto" || endpointKind == "linux") {
        platform::linux_desktop::clipboard::LinuxClipboardEndpointOptions
            options;
        options.suppressOwnClipboardUpdates =
            !hasArg(argc, argv, "--clipboard-no-owner-suppression");
        options.maxInlineBytes = policy.maxInlineBytes;
        options.maxFileRangeBytes = policy.maxFileRangeBytes;
        options.maxSingleFileBytes = policy.maxSingleFileBytes;
        options.maxFileCount =
            uint32FromUint64(policy.maxFileCount, options.maxFileCount);
        options.maxDirectoryDepth =
            uint32FromUint64(
                uint64OptionValue(argc,
                                  argv,
                                  "--clipboard-max-directory-depth",
                                  options.maxDirectoryDepth),
                options.maxDirectoryDepth);
        options.expandDroppedDirectories =
            !hasArg(argc, argv, "--clipboard-no-expand-directories");
        const std::string ownerWindowName =
            optionValue(argc, argv, "--clipboard-owner-window-name");
        if (!ownerWindowName.empty())
            options.ownerWindowName = ownerWindowName;
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
            remoteFileReader =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteFileReader>(
                    remoteReader);
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
            remoteObjectLocker =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteObjectLocker>(
                    remoteReader);
        return std::make_shared<
            platform::linux_desktop::clipboard::LinuxClipboardEndpoint>(
            options,
            std::move(remoteReader),
            std::move(remoteFileReader),
            std::move(remoteObjectLocker),
            std::shared_ptr<modules::clipboard::ITransferTranscoder>{},
            std::make_shared<QtClipboardCallbackDispatcher>());
    }
#endif

#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
    if (endpointKind == "auto" || endpointKind == "qt") {
        adapters::qt::clipboard::QtClipboardEndpointOptions options;
        options.suppressOwnClipboardUpdates =
            !hasArg(argc, argv, "--clipboard-no-owner-suppression");
        options.maxInlineBytes = policy.maxInlineBytes;
        options.maxFileRangeBytes = policy.maxFileRangeBytes;
        options.maxSingleFileBytes = policy.maxSingleFileBytes;
        options.maxFileCount =
            uint32FromUint64(policy.maxFileCount, options.maxFileCount);
        options.maxDirectoryDepth =
            uint32FromUint64(
                uint64OptionValue(argc,
                                  argv,
                                  "--clipboard-max-directory-depth",
                                  options.maxDirectoryDepth),
                options.maxDirectoryDepth);
        options.expandDroppedDirectories =
            !hasArg(argc, argv, "--clipboard-no-expand-directories");
        std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
            remoteFileReader =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteFileReader>(
                    remoteReader);
        std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
            remoteObjectLocker =
                std::dynamic_pointer_cast<
                    modules::clipboard::IClipboardRemoteObjectLocker>(
                    remoteReader);
        return std::make_shared<
            adapters::qt::clipboard::QtClipboardEndpoint>(
            options,
            std::move(remoteReader),
            std::move(remoteFileReader),
            std::move(remoteObjectLocker));
    }
#endif

    (void)policy;
    (void)remoteReader;
    if (endpointKind == "windows")
        writeShellError("clipboard endpoint windows is not available in this build");
    else if (endpointKind == "macos")
        writeShellError("clipboard endpoint macos is not available in this build");
    else if (endpointKind == "linux")
        writeShellError("clipboard endpoint linux is not available in this build");
    else if (endpointKind == "qt")
        writeShellError("clipboard endpoint qt is not available in this build");
    else
        writeShellError("no clipboard endpoint adapter is available in this build");
    return nullptr;
}

std::shared_ptr<runtime::feature::IClipboardRuntimeReadPump>
makeClipboardRuntimeReadPump(
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId)
{
    return std::make_shared<PcClipboardRuntimeReadPump>(transportManager,
                                                        sessionId);
}

bool clipboardRuntimeRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--pump-clipboard");
}

bool clipboardTextRequirementRequested(int argc, char** argv)
{
    return !optionValue(argc, argv, "--require-clipboard-text").empty();
}

bool clipboardFileTextRequirementRequested(int argc, char** argv)
{
    return !optionValues(argc, argv, "--require-clipboard-file-text").empty();
}

bool clipboardEndpointFileTextRequirementRequested(int argc, char** argv)
{
    return !optionValues(argc,
                         argv,
                         "--require-clipboard-endpoint-file-text").empty();
}

namespace {

struct FileTextRequirement final {
    std::string relativePath;
    std::string expectedText;
};

FileTextRequirement parseFileTextRequirement(const std::string& value)
{
    const std::size_t separator = value.find('=');
    if (separator == std::string::npos)
        return FileTextRequirement{{}, value};

    return FileTextRequirement{value.substr(0, separator),
                               value.substr(separator + 1)};
}

modules::clipboard::ClipboardModuleBase* clipboardModuleForSession(
    session::Session& session)
{
    if (session.moduleHost() == nullptr)
        return nullptr;

    const std::string moduleId =
        session.role() == session::SessionRole::Client
            ? "clipboard.redirect.client"
            : "clipboard.redirect.agent";
    return dynamic_cast<modules::clipboard::ClipboardModuleBase*>(
        session.moduleHost()->module(moduleId));
}

struct RemoteFileListView final {
    modules::clipboard::TransferSourceBundle bundle;
    modules::clipboard::TransferSourceId sourceId = 0;
    modules::clipboard::TransferFileList fileList;
};

RemoteFileListView remoteFileListView(
    session::Session& session,
    modules::clipboard::IClipboardRemoteReader* remoteReader,
    std::uint32_t timeoutMs,
    std::string* errorMessage)
{
    using namespace modules::clipboard;

    RemoteFileListView view;
    ClipboardModuleBase* module = clipboardModuleForSession(session);
    if (module == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard file requirement needs a running module";
        return view;
    }

    const ClipboardModuleSnapshot snapshot = module->snapshot();
    if (snapshot.remoteBundle.offerId == 0 ||
        snapshot.remoteBundle.sources.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard remote file list is not available";
        return view;
    }

    for (const std::shared_ptr<TransferSource>& source :
         snapshot.remoteBundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        for (const TransferFormatDescriptor& descriptor : formats) {
            if (descriptor.canonicalFormat != FdclFileListFormat)
                continue;

            TransferReadRequest request;
            request.bundleId = snapshot.remoteBundle.bundleId;
            request.offerId = snapshot.remoteBundle.offerId;
            request.ownerEpoch = snapshot.remoteBundle.ownerEpoch;
            request.sourceId = source->id();
            request.itemIndex = descriptor.itemIndex;
            request.formatId = descriptor.formatId;
            request.localFormatToken = descriptor.localFormatToken;
            request.canonicalFormat = FdclFileListFormat;
            request.acceptedMaxBytes = descriptor.estimatedBytes == 0
                                           ? 1024 * 1024
                                           : descriptor.estimatedBytes;
            request.streamAccepted = false;
            request.requestedEncoding = TransferEncodingMode::CanonicalBytes;

            TransferReadResult result = source->read(request);
            if (!result.ok() && remoteReader != nullptr)
                result = remoteReader->readRemoteFormat(request, timeoutMs);
            if (!result.ok()) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard remote file list read failed: " +
                        result.message;
                }
                return view;
            }

            const TransferFileListDecodeResult decoded =
                decodeTransferFileList(result.bytes);
            if (!decoded.ok) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard remote file list decode failed: " +
                        decoded.message;
                }
                return view;
            }

            view.bundle = snapshot.remoteBundle;
            view.sourceId = source->id();
            view.fileList = decoded.fileList;
            return view;
        }
    }

    if (errorMessage != nullptr)
        *errorMessage = "clipboard remote file-list format is not available";
    return view;
}

struct EndpointFileListView final {
    modules::clipboard::TransferSourceBundle bundle;
    std::shared_ptr<modules::clipboard::TransferSource> source;
    modules::clipboard::ITransferFileContentProvider* contentProvider = nullptr;
    modules::clipboard::TransferFileList fileList;
};

EndpointFileListView endpointFileListView(
    modules::clipboard::IClipboardEndpoint& endpoint,
    std::string* errorMessage)
{
    using namespace modules::clipboard;

    EndpointFileListView view;
    const ClipboardSnapshot snapshot = endpoint.snapshot();
    if (snapshot.bundle.offerId == 0 || snapshot.bundle.sources.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard endpoint file list is not available";
        return view;
    }

    for (const std::shared_ptr<TransferSource>& source :
         snapshot.bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        for (const TransferFormatDescriptor& descriptor : formats) {
            if (descriptor.canonicalFormat != FdclFileListFormat)
                continue;

            TransferReadRequest request;
            request.bundleId = snapshot.bundle.bundleId;
            request.offerId = snapshot.bundle.offerId;
            request.ownerEpoch = snapshot.bundle.ownerEpoch;
            request.sourceId = source->id();
            request.itemIndex = descriptor.itemIndex;
            request.formatId = descriptor.formatId;
            request.localFormatToken = descriptor.localFormatToken;
            request.canonicalFormat = FdclFileListFormat;
            request.acceptedMaxBytes = descriptor.estimatedBytes == 0
                                           ? 1024 * 1024
                                           : descriptor.estimatedBytes;
            request.streamAccepted = false;
            request.requestedEncoding = TransferEncodingMode::CanonicalBytes;

            const TransferReadResult result = source->read(request);
            if (!result.ok()) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard endpoint file list read failed: " +
                        result.message;
                }
                return view;
            }

            const TransferFileListDecodeResult decoded =
                decodeTransferFileList(result.bytes);
            if (!decoded.ok) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard endpoint file list decode failed: " +
                        decoded.message;
                }
                return view;
            }

            auto* provider =
                dynamic_cast<ITransferFileContentProvider*>(source.get());
            if (provider == nullptr) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard endpoint file source cannot provide file contents";
                }
                return view;
            }

            view.bundle = snapshot.bundle;
            view.source = source;
            view.contentProvider = provider;
            view.fileList = decoded.fileList;
            return view;
        }
    }

    if (errorMessage != nullptr)
        *errorMessage = "clipboard endpoint file-list format is not available";
    return view;
}

std::size_t findRequiredFileIndex(
    const modules::clipboard::TransferFileList& fileList,
    const std::string& relativePath)
{
    for (std::size_t index = 0; index < fileList.files.size(); ++index) {
        const modules::clipboard::TransferFileDescriptor& descriptor =
            fileList.files[index];
        if (descriptor.directory)
            continue;
        if (relativePath.empty() ||
            descriptor.relativePath == relativePath ||
            descriptor.displayName == relativePath) {
            return index;
        }
    }
    return fileList.files.size();
}

struct RemoteObjectLockGuard final {
    ~RemoteObjectLockGuard()
    {
        unlockSilently();
    }

    RemoteObjectLockGuard() = default;
    RemoteObjectLockGuard(const RemoteObjectLockGuard&) = delete;
    RemoteObjectLockGuard& operator=(const RemoteObjectLockGuard&) = delete;

    bool lock(modules::clipboard::IClipboardRemoteObjectLocker& locker,
              modules::clipboard::TransferObjectLockRequest request,
              std::uint32_t timeoutMs,
              std::string* errorMessage)
    {
        locker_ = &locker;
        request_ = request;
        timeoutMs_ = timeoutMs;

        const modules::clipboard::TransferObjectLockResult result =
            locker_->lockRemoteObject(request_, timeoutMs_);
        if (!result.ok()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file object lock failed: " +
                    result.message;
            }
            return false;
        }
        if (result.lockId == 0) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file object lock returned empty lock id";
            }
            return false;
        }

        request_.lockId = result.lockId;
        request_.leaseUsec = result.leaseUsec;
        locked_ = true;
        return true;
    }

    bool unlock(std::string* errorMessage)
    {
        if (!locked_ || locker_ == nullptr)
            return true;

        const modules::clipboard::TransferObjectLockResult result =
            locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
        if (result.ok())
            return true;

        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard remote file object unlock failed: " +
                result.message;
        }
        return false;
    }

private:
    void unlockSilently()
    {
        if (!locked_ || locker_ == nullptr)
            return;

        locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
    }

    modules::clipboard::IClipboardRemoteObjectLocker* locker_ = nullptr;
    modules::clipboard::TransferObjectLockRequest request_;
    std::uint32_t timeoutMs_ = 0;
    bool locked_ = false;
};

} // namespace

bool clipboardTextRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage)
{
    const std::string expected =
        optionValue(argc, argv, "--require-clipboard-text");
    if (expected.empty())
        return true;

    if (endpoint == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard text requirement needs a clipboard endpoint";
        return false;
    }

    const modules::clipboard::TransferReadResult result =
        readEndpointPlainText(*endpoint);
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard text requirement read failed: " + result.message;
        }
        return false;
    }

    const std::string actual = bytesToString(result.bytes);
    if (actual == expected)
        return true;

    if (errorMessage != nullptr) {
        *errorMessage = "clipboard text requirement failed: expected=\"" +
                        expected +
                        "\" actual=\"" +
                        actual +
                        "\"";
    }
    return false;
}

bool clipboardFileTextRequirementSatisfied(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader,
    std::string* errorMessage)
{
    const std::vector<std::string> requirementValues =
        optionValues(argc, argv, "--require-clipboard-file-text");
    if (requirementValues.empty())
        return true;

    std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
        fileReader =
            std::dynamic_pointer_cast<
                modules::clipboard::IClipboardRemoteFileReader>(
                remoteReader);
    if (fileReader == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage =
                "clipboard file requirement needs a remote file reader";
        return false;
    }
    std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
        objectLocker =
            std::dynamic_pointer_cast<
                modules::clipboard::IClipboardRemoteObjectLocker>(
                remoteReader);
    if (objectLocker == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage =
                "clipboard file requirement needs a remote object locker";
        return false;
    }

    std::string listError;
    const std::uint32_t timeoutMs = static_cast<std::uint32_t>(
        intOptionValue(argc, argv, "--clipboard-file-read-timeout-ms", 3000));
    modules::clipboard::TransferFileWindowReadOptions windowOptions;
    windowOptions.timeoutMs = timeoutMs;
    const std::uint64_t chunkBytes =
        uint64OptionValue(argc, argv, "--clipboard-file-read-chunk-bytes", 0);
    if (chunkBytes != 0)
        windowOptions.chunkBytes = chunkBytes;

    const RemoteFileListView view =
        remoteFileListView(session, remoteReader.get(), timeoutMs, &listError);
    if (view.bundle.offerId == 0) {
        if (errorMessage != nullptr)
            *errorMessage = listError;
        return false;
    }

    for (const std::string& requirementValue : requirementValues) {
        const FileTextRequirement requirement =
            parseFileTextRequirement(requirementValue);
        if (requirement.expectedText.empty()) {
            if (errorMessage != nullptr)
                *errorMessage =
                    "clipboard file text requirement cannot be empty";
            return false;
        }

        const std::size_t index =
            findRequiredFileIndex(view.fileList, requirement.relativePath);
        if (index >= view.fileList.files.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = requirement.relativePath.empty()
                                    ? "clipboard remote file list has no file"
                                    : "clipboard remote file was not found: " +
                                          requirement.relativePath;
            }
            return false;
        }

        const modules::clipboard::TransferFileDescriptor& descriptor =
            view.fileList.files[index];
        modules::clipboard::TransferObjectLockRequest lockRequest;
        lockRequest.bundleId = view.bundle.bundleId;
        lockRequest.offerId = view.bundle.offerId;
        lockRequest.ownerEpoch = view.bundle.ownerEpoch;
        lockRequest.sourceId = view.sourceId;
        lockRequest.objectId = descriptor.objectId;
        lockRequest.fileIndex = static_cast<std::uint32_t>(index);

        RemoteObjectLockGuard lockGuard;
        if (!lockGuard.lock(*objectLocker,
                            lockRequest,
                            timeoutMs,
                            errorMessage)) {
            return false;
        }

        modules::clipboard::TransferFileRangeRequest request;
        request.bundleId = view.bundle.bundleId;
        request.offerId = view.bundle.offerId;
        request.ownerEpoch = view.bundle.ownerEpoch;
        request.sourceId = view.sourceId;
        request.objectId = descriptor.objectId;
        request.fileIndex = static_cast<std::uint32_t>(index);
        request.offset = 0;

        const modules::clipboard::TransferFileRangeResult result =
            modules::clipboard::readRemoteFileRangeWindow(
                *fileReader,
                request,
                0,
                requirement.expectedText.size(),
                windowOptions);
        if (!result.ok()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file range read failed: " +
                    result.message;
            }
            return false;
        }

        const std::string actual = bytesToString(result.bytes);
        if (actual == requirement.expectedText && result.endOfFile) {
            if (lockGuard.unlock(errorMessage))
                continue;
            return false;
        }

        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard file text requirement failed for \"" +
                requirement.relativePath +
                "\": expected=\"" +
                requirement.expectedText +
                "\" actual=\"" +
                actual +
                "\" eof=" +
                (result.endOfFile ? "true" : "false");
        }
        return false;
    }
    return true;
}

bool clipboardEndpointFileTextRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage)
{
    using namespace modules::clipboard;

    const std::vector<std::string> requirementValues =
        optionValues(argc, argv, "--require-clipboard-endpoint-file-text");
    if (requirementValues.empty())
        return true;

    if (endpoint == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage =
                "clipboard endpoint file requirement needs a clipboard endpoint";
        return false;
    }

    EndpointFileListView view = endpointFileListView(*endpoint, errorMessage);
    if (view.bundle.offerId == 0 || view.source == nullptr ||
        view.contentProvider == nullptr) {
        return false;
    }

    for (const std::string& requirementValue : requirementValues) {
        const FileTextRequirement requirement =
            parseFileTextRequirement(requirementValue);
        if (requirement.expectedText.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard endpoint file text requirement cannot be empty";
            }
            return false;
        }

        const std::size_t index =
            findRequiredFileIndex(view.fileList, requirement.relativePath);
        if (index >= view.fileList.files.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = requirement.relativePath.empty()
                                    ? "clipboard endpoint file list has no file"
                                    : "clipboard endpoint file was not found: " +
                                          requirement.relativePath;
            }
            return false;
        }

        const TransferFileDescriptor& descriptor = view.fileList.files[index];
        TransferFileRangeRequest request;
        request.bundleId = view.bundle.bundleId;
        request.offerId = view.bundle.offerId;
        request.ownerEpoch = view.bundle.ownerEpoch;
        request.sourceId = view.source->id();
        request.objectId = descriptor.objectId;
        request.fileIndex = static_cast<std::uint32_t>(index);
        request.offset = 0;
        request.requestedBytes = requirement.expectedText.size();

        const TransferFileRangeResult result =
            view.contentProvider->readFileRange(request);
        if (!result.ok()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard endpoint file range read failed: " +
                    result.message;
            }
            return false;
        }

        const std::string actual = bytesToString(result.bytes);
        if (actual == requirement.expectedText && result.endOfFile)
            continue;

        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard endpoint file text requirement failed for \"" +
                requirement.relativePath +
                "\": expected=\"" +
                requirement.expectedText +
                "\" actual=\"" +
                actual +
                "\" eof=" +
                (result.endOfFile ? "true" : "false");
        }
        return false;
    }

    return true;
}

bool verifyClipboardTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    std::string errorMessage;
    if (clipboardTextRequirementSatisfied(argc, argv, endpoint, &errorMessage))
        return true;

    writeShellError(errorMessage);
    return false;
}

bool verifyClipboardFileTextRequirement(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader)
{
    std::string errorMessage;
    if (clipboardFileTextRequirementSatisfied(argc,
                                              argv,
                                              session,
                                              remoteReader,
                                              &errorMessage)) {
        return true;
    }

    writeShellError(errorMessage);
    return false;
}

bool verifyClipboardEndpointFileTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    std::string errorMessage;
    if (clipboardEndpointFileTextRequirementSatisfied(argc,
                                                      argv,
                                                      endpoint,
                                                      &errorMessage)) {
        return true;
    }

    writeShellError(errorMessage);
    return false;
}

bool seedClipboardTextIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    const std::string seed = optionValue(argc, argv, "--clipboard-seed-text");
    if (seed.empty())
        return true;

    if (endpoint == nullptr) {
        writeShellError("clipboard seed text needs a clipboard endpoint");
        return false;
    }

    const protocol::ResponseStatus status = endpoint->publishBundle(
        modules::clipboard::ClipboardPublishRequest{makeTextSeedBundle(seed)});
    if (status == protocol::ResponseStatus::Ok)
        return true;

    std::string message =
        "clipboard seed text publish failed: status=" +
        std::to_string(static_cast<int>(status));
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
    auto windowsEndpoint =
        std::dynamic_pointer_cast<
            platform::windows::clipboard::WindowsClipboardEndpoint>(endpoint);
    if (windowsEndpoint != nullptr) {
        const platform::windows::clipboard::WindowsClipboardEndpointDiagnostics
            diagnostics = windowsEndpoint->diagnostics();
        message += " nativeFailures=" +
                   std::to_string(diagnostics.nativeFailures) +
                   " lastNativeError=" +
                   std::to_string(diagnostics.lastNativeError) +
                   " message=" + diagnostics.lastMessage;
    }
#endif
    writeShellError(message);
    return false;
}

bool seedClipboardFilesIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    const std::vector<std::string> paths =
        optionValues(argc, argv, "--clipboard-seed-file");
    if (paths.empty())
        return true;

#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
    auto windowsEndpoint =
        std::dynamic_pointer_cast<
            platform::windows::clipboard::WindowsClipboardEndpoint>(endpoint);
    if (windowsEndpoint == nullptr) {
        writeShellError("clipboard seed file needs a Windows clipboard endpoint");
        return false;
    }

    std::vector<std::wstring> nativePaths;
    nativePaths.reserve(paths.size());
    for (const std::string& path : paths)
        nativePaths.push_back(QString::fromUtf8(path.c_str()).toStdWString());

    const protocol::ResponseStatus status =
        windowsEndpoint->setDryRunClipboardLocalFiles(std::move(nativePaths));
    if (status == protocol::ResponseStatus::Ok)
        return true;

    const platform::windows::clipboard::WindowsClipboardEndpointDiagnostics
        diagnostics = windowsEndpoint->diagnostics();
    writeShellError("clipboard seed file failed: status=" +
                    std::to_string(static_cast<int>(status)) +
                    " message=" + diagnostics.lastMessage);
    return false;
#else
    (void)argc;
    (void)argv;
    (void)endpoint;
    writeShellError("clipboard seed file is not available in this build");
    return false;
#endif
}

void writeClipboardDiagnosticsIfRequested(
    int argc,
    char** argv,
    session::Session* session,
    runtime::feature::ClipboardRuntimeService* service,
    const std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>& policy,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    const char* phase)
{
    if (!hasArg(argc, argv, "--print-clipboard-diagnostics"))
        return;

    auto configurablePolicy =
        std::dynamic_pointer_cast<
            runtime::feature::ConfigurableClipboardRuntimePolicy>(policy);
    runtime::feature::ClipboardRuntimePolicySnapshot policySnapshot;
    if (configurablePolicy != nullptr)
        policySnapshot = configurablePolicy->snapshot();

    if (service != nullptr) {
        const runtime::feature::ClipboardRuntimeServiceSnapshot snapshot =
            service->snapshot();
        std::cout << "clipboard.runtime"
                  << " phase=" << phase
                  << " active=" << (snapshot.active ? "true" : "false")
                  << " endpoint=" << (snapshot.endpointAttached ? "true" : "false")
                  << " pumpCount=" << snapshot.pumpCount
                  << " announcementsSent=" << snapshot.announcementsSent
                  << " idlePolls=" << snapshot.idlePolls
                  << " duplicates=" << snapshot.duplicateSnapshots
                  << " missingEndpoints=" << snapshot.missingEndpoints
                  << " missingModules=" << snapshot.missingModules
                  << " policyDenials=" << snapshot.policyDenials
                  << " sendFailures=" << snapshot.sendFailures
                  << " auditEvents=" << snapshot.auditEvents
                  << " lastOffer=" << snapshot.lastOfferId
                  << " lastBundle=" << snapshot.lastBundleId
                  << " lastSequence=" << snapshot.lastSequence
                  << std::endl;

        const runtime::feature::ClipboardProductHealthPresentation health =
            runtime::feature::buildClipboardProductHealthPresentation(
                snapshot,
                policySnapshot);
        std::cout << "clipboard.health"
                  << " phase=" << phase
                  << " usable=" << (health.usable ? "true" : "false")
                  << " health=" << health.healthName
                  << " status=" << health.statusCode
                  << " action=" << health.primaryActionCode
                  << " runtimeState=" << health.runtimeState
                  << " policyState=" << health.policyState
                  << " policyWarning="
                  << (health.showPolicyDenialWarning ? "true" : "false")
                  << " audit="
                  << (health.showAuditIndicator ? "true" : "false")
                  << " transferWarning="
                  << (health.showTransferWarning ? "true" : "false")
                  << std::endl;
    }

    if (configurablePolicy != nullptr) {
        std::cout << "clipboard.runtime_policy"
                  << " phase=" << phase
                  << " authorizeCalls=" << policySnapshot.authorizeCalls
                  << " allowed=" << policySnapshot.allowed
                  << " denied=" << policySnapshot.denied
                  << " auditEvents=" << policySnapshot.auditEvents
                  << " auditedAllowed=" << policySnapshot.auditedAllowed
                  << " auditedDenied=" << policySnapshot.auditedDenied
                  << " lastAllowed="
                  << (policySnapshot.lastAllowed ? "true" : "false")
                  << " lastOperation="
                  << runtime::feature::clipboardRuntimeOperationName(
                         policySnapshot.lastOperation)
                  << " lastStatus="
                  << static_cast<int>(policySnapshot.lastStatus)
                  << " lastOffer=" << policySnapshot.lastOfferId
                  << " lastBundle=" << policySnapshot.lastBundleId
                  << " lastObject=" << policySnapshot.lastObjectId
                  << " lastFileIndex=" << policySnapshot.lastFileIndex
                  << " lastRequestedBytes=" << policySnapshot.lastRequestedBytes
                  << " lastFormat=" << policySnapshot.lastCanonicalFormat
                  << " lastReason=" << policySnapshot.lastReason
                  << std::endl;

        for (std::size_t index = 0;
             index < policySnapshot.recentAuditEvents.size();
             ++index) {
            const runtime::feature::ClipboardRuntimeAuditEvent& event =
                policySnapshot.recentAuditEvents[index];
            std::cout << "clipboard.audit"
                      << " phase=" << phase
                      << " index=" << index
                      << " allowed=" << (event.allowed ? "true" : "false")
                      << " operation="
                      << runtime::feature::clipboardRuntimeOperationName(
                             event.context.operation)
                      << " status=" << static_cast<int>(event.responseStatus)
                      << " session=" << event.context.sessionId
                      << " trace=" << event.context.traceId
                      << " role="
                      << clipboardSessionRoleName(event.context.role)
                      << " module=" << event.context.moduleId
                      << " policyVersion=" << event.context.policyVersion
                      << " offer=" << event.context.offerId
                      << " bundle=" << event.context.bundleId
                      << " ownerEpoch=" << event.context.ownerEpoch
                      << " sequence=" << event.context.sequence
                      << " object=" << event.context.objectId
                      << " fileIndex=" << event.context.fileIndex
                      << " requestedBytes=" << event.context.requestedBytes
                      << " formatCount=" << event.context.formatCount
                      << " format=" << event.context.canonicalFormat
                      << " reason=" << event.reason
                      << std::endl;
        }
    }

    if (session != nullptr && session->moduleHost() != nullptr) {
        const std::string moduleId =
            session->role() == session::SessionRole::Client
                ? "clipboard.redirect.client"
                : "clipboard.redirect.agent";
        auto* module = dynamic_cast<modules::clipboard::ClipboardModuleBase*>(
            session->moduleHost()->module(moduleId));
        if (module != nullptr) {
            const modules::clipboard::ClipboardModuleSnapshot snapshot =
                module->snapshot();
            std::cout << "clipboard.module"
                      << " phase=" << phase
                      << " module=" << snapshot.moduleId
                      << " state=" << static_cast<int>(snapshot.state)
                      << " formatListsSent=" << snapshot.formatListsSent
                      << " formatListsReceived=" << snapshot.formatListsReceived
                      << " readRequestsSent=" << snapshot.readRequestsSent
                      << " readRequestsReceived=" << snapshot.readRequestsReceived
                      << " inlineResponsesSent=" << snapshot.inlineResponsesSent
                      << " inlineResponsesReceived=" << snapshot.inlineResponsesReceived
                      << " fileRangeRequestsSent=" << snapshot.fileRangeRequestsSent
                      << " fileRangeRequestsReceived=" << snapshot.fileRangeRequestsReceived
                      << " fileRangeResponsesSent=" << snapshot.fileRangeResponsesSent
                      << " fileRangeResponsesReceived=" << snapshot.fileRangeResponsesReceived
                      << " fileRangeBytesSent=" << snapshot.fileRangeBytesSent
                      << " fileRangeBytesReceived=" << snapshot.fileRangeBytesReceived
                      << " objectLockRequestsSent=" << snapshot.objectLockRequestsSent
                      << " objectLockRequestsReceived=" << snapshot.objectLockRequestsReceived
                      << " objectLockResponsesSent=" << snapshot.objectLockResponsesSent
                      << " objectLockResponsesReceived=" << snapshot.objectLockResponsesReceived
                      << " objectUnlockRequestsSent=" << snapshot.objectUnlockRequestsSent
                      << " objectUnlockRequestsReceived=" << snapshot.objectUnlockRequestsReceived
                      << " objectUnlockResponsesSent=" << snapshot.objectUnlockResponsesSent
                      << " objectUnlockResponsesReceived=" << snapshot.objectUnlockResponsesReceived
                      << " objectLocksReleased=" << snapshot.objectLocksReleased
                      << " dragStartsSent=" << snapshot.dragStartsSent
                      << " dragStartsReceived=" << snapshot.dragStartsReceived
                      << " dragMovesSent=" << snapshot.dragMovesSent
                      << " dragMovesReceived=" << snapshot.dragMovesReceived
                      << " dragDropsSent=" << snapshot.dragDropsSent
                      << " dragDropsReceived=" << snapshot.dragDropsReceived
                      << " dragNativePublicationFailures=" << snapshot.dragNativePublicationFailures
                      << " policyDenials=" << snapshot.policyDenials
                      << " staleOfferFailures=" << snapshot.staleOfferFailures
                      << " decodeFailures=" << snapshot.decodeFailures
                      << " sendFailures=" << snapshot.sendFailures
                      << " readResponseMisses=" << snapshot.readResponseMisses
                      << " lastReadRequest="
                      << snapshot.lastReadRequestMessageId
                      << " lastReadResponse="
                      << snapshot.lastReadResponseMessageId
                      << " lastReadResponseTo="
                      << snapshot.lastReadResponseTo
                      << " lastReadStatus="
                      << snapshot.lastReadResponseStatus
                      << " lastReadPayloadBytes="
                      << snapshot.lastReadResponsePayloadBytes
                      << " pendingReads=" << snapshot.pendingReads
                      << " localOffer=" << snapshot.localBundle.offerId
                      << " remoteOffer=" << snapshot.remoteBundle.offerId
                      << std::endl;
        }
    }

#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
    auto windowsEndpoint =
        std::dynamic_pointer_cast<
            platform::windows::clipboard::WindowsClipboardEndpoint>(endpoint);
    if (windowsEndpoint != nullptr) {
        const platform::windows::clipboard::WindowsClipboardEndpointDiagnostics
            diagnostics = windowsEndpoint->diagnostics();
        std::cout << "clipboard.endpoint"
                  << " phase=" << phase
                  << " kind=windows"
                  << " dryRun=" << (diagnostics.dryRun ? "true" : "false")
                  << " snapshots=" << diagnostics.snapshots
                  << " publishes=" << diagnostics.publishes
                  << " delayedPublishes=" << diagnostics.delayedPublishes
                  << " delayedRenders=" << diagnostics.delayedRenders
                  << " readFailures=" << diagnostics.readFailures
                  << " nativeFailures=" << diagnostics.nativeFailures
                  << " pending=" << (diagnostics.nativeChangePending ? "true" : "false")
                  << " notifications=" << diagnostics.nativeChangeNotifications
                  << " ownerSuppressions=" << diagnostics.ownerSuppressions
                  << " publishedOffer=" << diagnostics.publishedOfferId
                  << " dragStarts=" << diagnostics.dragStarts
                  << " dragMoves=" << diagnostics.dragMoves
                  << " dragDrops=" << diagnostics.dragDrops
                  << " dragCancels=" << diagnostics.dragCancels
                  << " nativeDragLoops=" << diagnostics.nativeDragLoops
                  << " nativeDragPreflights=" << diagnostics.nativeDragPreflights
                  << " nativeDragPreflightReads=" << diagnostics.nativeDragPreflightReads
                  << " nativeDragPreflightBytes=" << diagnostics.nativeDragPreflightBytes
                  << " nativeDragDrops=" << diagnostics.nativeDragDrops
                  << " nativeDragCancels=" << diagnostics.nativeDragCancels
                  << " activeDrag=" << diagnostics.activeDragSessionId
                  << " lastDragSession=" << diagnostics.lastDragSessionId
                  << " lastDragX=" << diagnostics.lastDragX
                  << " lastDragY=" << diagnostics.lastDragY
                  << " message=" << diagnostics.lastMessage
                  << std::endl;
    }
#endif

#if defined(FUSIONDESK_PC_HAS_MACOS_FEATURE_ADAPTERS)
    auto macEndpoint =
        std::dynamic_pointer_cast<
            platform::macos::clipboard::MacClipboardEndpoint>(endpoint);
    if (macEndpoint != nullptr) {
        const platform::macos::clipboard::MacClipboardEndpointDiagnostics
            diagnostics = macEndpoint->diagnostics();
        std::cout << "clipboard.endpoint"
                  << " phase=" << phase
                  << " kind=macos"
                  << " snapshots=" << diagnostics.snapshots
                  << " publishes=" << diagnostics.publishes
                  << " clears=" << diagnostics.clears
                  << " delayedPublishes=" << diagnostics.delayedPublishes
                  << " delayedRenders=" << diagnostics.delayedRenders
                  << " readFailures=" << diagnostics.readFailures
                  << " fileListSnapshots=" << diagnostics.fileListSnapshots
                  << " remoteFilePromisePublishes="
                  << diagnostics.remoteFilePromisePublishes
                  << " remoteFilePromiseFailures="
                  << diagnostics.remoteFilePromiseFailures
                  << " remoteFilePromiseProviders="
                  << diagnostics.remoteFilePromiseProviders
                  << " pending=" << (diagnostics.nativeChangePending ? "true" : "false")
                  << " changeCount=" << diagnostics.lastNativeChangeCount
                  << " publishedOffer=" << diagnostics.publishedOfferId
                  << " message=" << diagnostics.lastMessage
                  << std::endl;
    }
#endif

#if defined(FUSIONDESK_PC_HAS_LINUX_FEATURE_ADAPTERS)
    auto linuxEndpoint =
        std::dynamic_pointer_cast<
            platform::linux_desktop::clipboard::LinuxClipboardEndpoint>(
            endpoint);
    if (linuxEndpoint != nullptr) {
        const platform::linux_desktop::clipboard::LinuxClipboardEndpointDiagnostics
            diagnostics = linuxEndpoint->diagnostics();
        std::cout << "clipboard.endpoint"
                  << " phase=" << phase
                  << " kind=linux"
                  << " started=" << (diagnostics.started ? "true" : "false")
                  << " snapshots=" << diagnostics.snapshots
                  << " publishes=" << diagnostics.publishes
                  << " clears=" << diagnostics.clears
                  << " delayedPublishes=" << diagnostics.delayedPublishes
                  << " delayedRenders=" << diagnostics.delayedRenders
                  << " streamChunks=" << diagnostics.streamChunks
                  << " streamBytes=" << diagnostics.streamBytes
                  << " targetListReads=" << diagnostics.targetListReads
                  << " targetDataReads=" << diagnostics.targetDataReads
                  << " targetReadFailures=" << diagnostics.targetReadFailures
                  << " readFailures=" << diagnostics.readFailures
                  << " fileListSnapshots=" << diagnostics.fileListSnapshots
                  << " delayedRenderCacheHits="
                  << diagnostics.delayedRenderCacheHits
                  << " delayedRenderCacheStores="
                  << diagnostics.delayedRenderCacheStores
                  << " fusePromiseAvailable="
                  << (diagnostics.fusePromiseAvailable ? "true" : "false")
                  << " fusePromiseActive="
                  << (diagnostics.fusePromiseActive ? "true" : "false")
                  << " remoteFilePromisePublishes="
                  << diagnostics.remoteFilePromisePublishes
                  << " remoteFilePromiseFailures="
                  << diagnostics.remoteFilePromiseFailures
                  << " remoteFilePromiseReads="
                  << diagnostics.remoteFilePromiseReads
                  << " remoteFilePromiseReadFailures="
                  << diagnostics.remoteFilePromiseReadFailures
                  << " remoteFilePromiseReadBytes="
                  << diagnostics.remoteFilePromiseReadBytes
                  << " remoteFilePromiseProviders="
                  << diagnostics.remoteFilePromiseProviders
                  << " pending="
                  << (diagnostics.nativeChangePending ? "true" : "false")
                  << " notifications="
                  << diagnostics.nativeChangeNotifications
                  << " ownerLost=" << diagnostics.ownerLostNotifications
                  << " ownerSuppressions=" << diagnostics.ownerSuppressions
                  << " publishedOffer=" << diagnostics.publishedOfferId
                  << " clipbusStatus=" << diagnostics.lastClipbusStatus
                  << " message=" << diagnostics.lastMessage
                  << std::endl;
    }
#endif

#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
    auto qtEndpoint =
        std::dynamic_pointer_cast<
            adapters::qt::clipboard::QtClipboardEndpoint>(endpoint);
    if (qtEndpoint != nullptr) {
        const adapters::qt::clipboard::QtClipboardEndpointDiagnostics
            diagnostics = qtEndpoint->diagnostics();
        std::cout << "clipboard.endpoint"
                  << " phase=" << phase
                  << " kind=qt"
                  << " snapshots=" << diagnostics.snapshots
                  << " publishes=" << diagnostics.publishes
                  << " clears=" << diagnostics.clears
                  << " fileListSnapshots=" << diagnostics.fileListSnapshots
                  << " remoteFilePublishes=" << diagnostics.remoteFilePublishes
                  << " remoteFileMaterializationFailures="
                  << diagnostics.remoteFileMaterializationFailures
                  << " remoteFilesMaterialized="
                  << diagnostics.remoteFilesMaterialized
                  << " remoteDirectoriesMaterialized="
                  << diagnostics.remoteDirectoriesMaterialized
                  << " remoteFileBytesMaterialized="
                  << diagnostics.remoteFileBytesMaterialized
                  << " readFailures=" << diagnostics.readFailures
                  << " pending="
                  << (diagnostics.nativeChangePending ? "true" : "false")
                  << " notifications="
                  << diagnostics.nativeChangeNotifications
                  << " ownerSuppressions=" << diagnostics.ownerSuppressions
                  << " publishedOffer=" << diagnostics.publishedOfferId
                  << " message=" << diagnostics.lastMessage
                  << std::endl;
    }
#endif
}

void writeClipboardProductPolicyDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::ProductClipboardPolicy& policy,
    const char* phase)
{
    if (!hasArg(argc, argv, "--print-clipboard-diagnostics"))
        return;

    const runtime::feature::ClipboardProductPolicyPresentation presentation =
        runtime::feature::buildClipboardProductPolicyPresentation(policy);
    std::cout << "clipboard.policy"
              << " phase=" << phase
              << " usable=" << (presentation.usable ? "true" : "false")
              << " mode=" << presentation.modeCode
              << " action=" << presentation.primaryActionCode
              << " direction=" << presentation.directionState
              << " content=" << presentation.contentState
              << " file=" << presentation.fileState
              << " drag=" << presentation.dragState
              << " custom=" << presentation.customFormatState
              << " runtime=" << presentation.runtimeState
              << " audit=" << presentation.auditState
              << " plainText="
              << (presentation.allowPlainText ? "true" : "false")
              << " richText="
              << (presentation.allowRichText ? "true" : "false")
              << " image=" << (presentation.allowImage ? "true" : "false")
              << " files=" << (presentation.allowFiles ? "true" : "false")
              << " fileContents="
              << (presentation.allowFileContents ? "true" : "false")
              << " dragAllowed="
              << (presentation.allowDrag ? "true" : "false")
              << " customFormats="
              << (presentation.allowCustomFormats ? "true" : "false")
              << " maxInlineBytes=" << presentation.maxInlineBytes
              << " maxFileRangeBytes=" << presentation.maxFileRangeBytes
              << " maxFileCount=" << presentation.maxFileCount
              << " maxSingleFileBytes=" << presentation.maxSingleFileBytes
              << std::endl;
}

PcClipboardRuntimeStartResult startClipboardRuntime(
    int argc,
    char** argv,
    session::Session& session,
    std::shared_ptr<modules::clipboard::IClipboardEndpoint> endpoint,
    std::shared_ptr<runtime::feature::IClipboardRuntimePolicy> policy,
    PcClipboardRuntimeContext& context)
{
    PcClipboardRuntimeStartResult result;
    if (endpoint == nullptr) {
        result.messages.push_back(
            "clipboard runtime requires a clipboard endpoint");
        return result;
    }

    runtime::feature::ClipboardRuntimeServiceOptions options;
    options.session = &session;
    options.endpoint = std::move(endpoint);
    options.policy = policy;
    context.policy = std::move(policy);
    context.service =
        std::make_unique<runtime::feature::ClipboardRuntimeService>(options);

    const runtime::feature::ClipboardRuntimeServiceStartResult started =
        context.service->start();
    if (!started.ok) {
        result.messages = started.messages;
        context.service.reset();
        return result;
    }

    context.pumpTimer = std::make_unique<QTimer>();
    const int pumpIntervalMs =
        intOptionValue(argc, argv, "--clipboard-pump-interval-ms", 100);
    context.pumpTimer->setInterval(pumpIntervalMs > 0 ? pumpIntervalMs : 100);
    runtime::feature::ClipboardRuntimeService* service = context.service.get();
    QObject::connect(context.pumpTimer.get(),
                     &QTimer::timeout,
                     [service]() {
                         service->pumpOnce();
                         service->expirePendingReads(
                             runtime::qt::QtTimerBridge::monotonicNowUsec());
                     });
    context.pumpTimer->start();
    result.ok = true;
    return result;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
