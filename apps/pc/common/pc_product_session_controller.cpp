#include "pc_product_session_controller.h"

#include <algorithm>
#include <utility>

#include <QObject>
#include <QTimer>

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
#include <QApplication>
#include <QString>

#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"
#include "fusiondesk/adapters/qt/display/qt_image_display_window.h"
#endif
#include "fusiondesk/runtime/display/display_runtime_service.h"
#include "fusiondesk/runtime/feature/clipboard_product_policy.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_reconnect_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

protocol::NegotiatedCapabilities makeNegotiatedCapabilities()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video,
                                protocol::PacketType::Clipboard,
                                protocol::PacketType::Mouse,
                                protocol::PacketType::Keyboard,
                                protocol::PacketType::Filesystem,
                                protocol::PacketType::FilesystemControl,
                                protocol::PacketType::FilesystemIrp,
                                protocol::PacketType::Printer};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Ack,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::Cancel};
    return capabilities;
}

session::SessionRole sessionRoleFor(PcShellRole role)
{
    return role == PcShellRole::Client ?
           session::SessionRole::Client :
           session::SessionRole::Agent;
}

session::SessionCreateOptions makeDefaultSessionOptions(
    const runtime::RuntimeHost& host,
    PcShellRole role)
{
    session::SessionCreateOptions options;
    options.context.userId =
        role == PcShellRole::Client ? "pc-client-user" : "pc-agent-user";
    options.context.tenantId = "pc-product";
    options.context.deviceId =
        role == PcShellRole::Client ? "pc-client-device" : "pc-agent-device";
    options.context.clientDeviceId = "pc-client-device";
    options.context.agentDeviceId = "pc-agent-device";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiatedCapabilities();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

void appendUnique(std::vector<network::ChannelKey>& target,
                  const std::vector<network::ChannelKey>& source)
{
    for (network::ChannelKey key : source) {
        if (std::find(target.begin(), target.end(), key) == target.end())
            target.push_back(key);
    }
}

runtime::LinkChannelBindingReportOptions withListeningChannels(
    runtime::LinkChannelBindingReportOptions options,
    const std::vector<network::ChannelKey>& listeningChannels)
{
    appendUnique(options.listeningChannels, listeningChannels);
    return options;
}

PcProductSessionOperationResult failure(int code, std::string message)
{
    PcProductSessionOperationResult result;
    result.code = code;
    result.messages.push_back(std::move(message));
    return result;
}

PcProductSessionOperationResult fromMainline(
    runtime::SessionMainlineReport report,
    int failureCode)
{
    PcProductSessionOperationResult result;
    result.ok = report.ok;
    result.code = report.ok ? 0 : failureCode;
    result.messages = report.messages;
    result.mainline = std::move(report);
    return result;
}

int displayPumpIntervalMs(
    const runtime::display::DisplayRuntimeServiceOptions& options)
{
    if (options.pumpAgentFrames) {
        const std::uint32_t fps = options.targetFps == 0 ? 15 : options.targetFps;
        const int interval = 1000 / static_cast<int>(fps);
        return interval > 0 ? interval : 1;
    }

    int interval = static_cast<int>(options.firstFrameTimeoutUsec / 1000ULL / 4ULL);
    if (interval <= 0)
        interval = 1;
    if (interval > 100)
        interval = 100;
    return interval;
}

std::string displayWindowStatusText(
    const runtime::DisplayProductHealthPresentation& presentation)
{
    std::string text = presentation.statusCode;
    text += " | ";
    text += presentation.primaryActionCode;
    text += " | ";
    text += presentation.healthName;
    return text;
}

PcProductDisplayWindowResult unsupportedDisplayWindowResult()
{
    PcProductDisplayWindowResult result;
    result.code = 18;
    result.messages.push_back("display window support is not available in this build");
    return result;
}

} // namespace

struct PcProductSessionController::Impl
{
    runtime::RuntimeHost host;
    std::unique_ptr<runtime::qt::QtRuntimeTransportManager> transportManager;
    std::unique_ptr<runtime::qt::QtPeerProfileRuntimeService> peerProfileService;
    std::unique_ptr<runtime::qt::QtReconnectRuntimeService> reconnectService;
    std::unique_ptr<runtime::display::DisplayRuntimeService> displayService;
    std::unique_ptr<runtime::feature::ClipboardRuntimeService> clipboardService;
    std::shared_ptr<runtime::feature::IClipboardRuntimePolicy> clipboardPolicy;
    std::unique_ptr<QTimer> displayPumpTimer;
    std::unique_ptr<QTimer> clipboardPumpTimer;
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    std::unique_ptr<adapters::qt::display::QtImageDisplayWindow> displayWindow;
#endif
    protocol::SessionId sessionId = 0;
    session::Session* session = nullptr;
    std::vector<network::ChannelKey> listeningChannels;
    bool active = false;
};

PcProductSessionController::PcProductSessionController()
    : impl_(std::make_unique<Impl>())
{
}

PcProductSessionController::~PcProductSessionController()
{
    stop("pc product session controller destroyed");
}

PcProductSessionOperationResult PcProductSessionController::start(
    const PcProductSessionStartOptions& options)
{
    if (impl_->active)
        return failure(1, "pc product session controller is already active");

    if (!impl_->host.initialize(options.runtimeOptions))
        return failure(2, "runtime host initialize failed");

    runtime::SessionMainlineOptions mainlineOptions;
    mainlineOptions.host = &impl_->host;
    mainlineOptions.role = sessionRoleFor(options.role);
    mainlineOptions.sessionOptions =
        options.useDefaultSessionOptions
            ? makeDefaultSessionOptions(impl_->host, options.role)
            : options.sessionOptions;
    mainlineOptions.mountProfileModules = false;
    mainlineOptions.startModules = false;

    runtime::SessionMainlineReport started =
        runtime::SessionMainline::start(std::move(mainlineOptions));
    if (!started.ok || started.session == nullptr) {
        PcProductSessionOperationResult result =
            fromMainline(std::move(started), 3);
        if (result.messages.empty())
            result.messages.push_back("session mainline start failed");
        return result;
    }

    impl_->sessionId = started.sessionId;
    impl_->session = started.session;
    impl_->transportManager =
        std::make_unique<runtime::qt::QtRuntimeTransportManager>(impl_->host);

    if (impl_->session->network() == nullptr)
        return failure(12, "session network is missing");

    if (options.startReconnectRuntime) {
        impl_->reconnectService =
            std::make_unique<runtime::qt::QtReconnectRuntimeService>(
                *impl_->transportManager,
                impl_->session->network()->router(),
                options.firstReconnectMessageId);
        const runtime::qt::QtReconnectRuntimeServiceStartResult
            reconnectStarted = impl_->reconnectService->start();
        if (!reconnectStarted.ok) {
            PcProductSessionOperationResult result;
            result.code = 12;
            result.messages = reconnectStarted.messages;
            if (result.messages.empty())
                result.messages.push_back("reconnect runtime service start failed");
            return result;
        }
    }

    impl_->active = true;

    PcProductSessionOperationResult result = fromMainline(std::move(started), 0);
    result.ok = true;
    result.code = 0;
    return result;
}

void PcProductSessionController::stop(const std::string& reason)
{
    if (!impl_)
        return;

    closeDisplayWindow();
    stopDisplayRuntime();
    stopClipboardRuntime();
    stopPeerProfileService();

    if (impl_->reconnectService)
        impl_->reconnectService->stop();
    impl_->reconnectService.reset();

    if (impl_->transportManager && impl_->sessionId != 0)
        impl_->transportManager->releaseSession(impl_->sessionId);
    impl_->transportManager.reset();

    if (impl_->sessionId != 0)
        impl_->host.sessions().close(impl_->sessionId,
                                     session::SessionStopReason{reason});
    impl_->sessionId = 0;
    impl_->session = nullptr;
    impl_->listeningChannels.clear();
    impl_->active = false;

    if (impl_->host.state() != runtime::RuntimeState::Created &&
        impl_->host.state() != runtime::RuntimeState::Stopped) {
        impl_->host.shutdown(reason);
    }
}

PcProductSessionOperationResult PcProductSessionController::applyTransportProfile(
    const std::string& path)
{
    if (!impl_->active || impl_->transportManager == nullptr)
        return failure(5, "pc product session controller is not active");
    if (path.empty())
        return failure(5, "transport profile path is required");

    const runtime::qt::QtTransportConnectResult applied =
        impl_->transportManager->applyProfilesFromJsonFileForSession(
            impl_->sessionId,
            path,
            impl_->host.profile().minimumChannels);

    PcProductSessionOperationResult result;
    result.ok = applied.ok;
    result.code = applied.ok ? 0 : 5;
    result.messages = applied.messages;
    result.readyChannels = applied.readyChannels;
    result.listeningChannels = applied.listeningChannels;
    appendUnique(impl_->listeningChannels, applied.listeningChannels);
    return result;
}

PcProductSessionOperationResult PcProductSessionController::applyListenProfile(
    const std::string& path)
{
    if (!impl_->active || impl_->transportManager == nullptr)
        return failure(9, "pc product session controller is not active");
    if (path.empty())
        return failure(9, "listen profile path is required");

    const runtime::qt::QtTransportConnectResult applied =
        impl_->transportManager->applyListenProfilesFromJsonFileForSession(
            impl_->sessionId,
            path,
            impl_->host.profile().minimumChannels);

    PcProductSessionOperationResult result;
    result.ok = applied.ok;
    result.code = applied.ok ? 0 : 9;
    result.messages = applied.messages;
    result.readyChannels = applied.readyChannels;
    result.listeningChannels = applied.listeningChannels;
    appendUnique(impl_->listeningChannels, applied.listeningChannels);
    return result;
}

PcProductSessionOperationResult PcProductSessionController::mountProfileModules(
    const runtime::DisplayMvpDependencies& dependencies,
    const runtime::LinkChannelBindingReportOptions& linkOptions)
{
    if (!impl_->active || impl_->session == nullptr)
        return failure(7, "pc product session controller is not active");

    runtime::SessionMainlineModuleOptions options;
    options.host = &impl_->host;
    options.session = impl_->session;
    options.moduleDependencies = dependencies;
    options.linkReportOptions =
        withListeningChannels(linkOptions, impl_->listeningChannels);
    options.mountProfileModules = true;
    options.startModules = false;
    return fromMainline(runtime::SessionMainline::mountAndStart(options), 7);
}

PcProductSessionOperationResult PcProductSessionController::startProfileModules(
    const module::ModuleStartOptions& startOptions,
    const runtime::LinkChannelBindingReportOptions& linkOptions)
{
    if (!impl_->active || impl_->session == nullptr)
        return failure(8, "pc product session controller is not active");

    runtime::SessionMainlineModuleOptions options;
    options.host = &impl_->host;
    options.session = impl_->session;
    options.linkReportOptions =
        withListeningChannels(linkOptions, impl_->listeningChannels);
    options.moduleStartOptions = startOptions;
    options.mountProfileModules = false;
    options.startModules = true;
    return fromMainline(runtime::SessionMainline::mountAndStart(options), 8);
}

PcProductPeerProfileResult PcProductSessionController::startPeerProfileService(
    const PcProductPeerProfileStartOptions& options)
{
    if (!impl_->active || impl_->session == nullptr ||
        impl_->transportManager == nullptr) {
        PcProductPeerProfileResult result;
        result.code = 13;
        result.messages.push_back("pc product session controller is not active");
        return result;
    }
    if (impl_->session->network() == nullptr) {
        PcProductPeerProfileResult result;
        result.code = 13;
        result.messages.push_back("peer profile service requires session network");
        return result;
    }
    if (impl_->peerProfileService != nullptr) {
        PcProductPeerProfileResult result;
        result.code = 13;
        result.messages.push_back("peer profile service is already active");
        result.snapshot = impl_->peerProfileService->snapshot();
        return result;
    }

    impl_->peerProfileService =
        std::make_unique<runtime::qt::QtPeerProfileRuntimeService>(
            *impl_->transportManager,
            impl_->session->network()->router(),
            options.firstMessageId);

    runtime::qt::QtPeerProfileRuntimeServiceStartOptions startOptions;
    startOptions.runtime = options.runtime;
    startOptions.autoApplyAgentListenProfile =
        options.autoApplyAgentListenProfile;
    startOptions.startTimer = options.startTimer;
    startOptions.timerIntervalMs = options.timerIntervalMs;

    const runtime::qt::QtPeerProfileRuntimeServiceStartResult started =
        impl_->peerProfileService->start(startOptions);
    PcProductPeerProfileResult result;
    result.ok = started.ok;
    result.code = started.ok ? 0 : 13;
    result.messages = started.messages;
    result.snapshot = impl_->peerProfileService->snapshot();
    if (!started.ok)
        impl_->peerProfileService.reset();
    return result;
}

PcProductPeerProfileResult PcProductSessionController::requestPeerProfile(
    const runtime::connection::PeerProfileExchangeRequest& request,
    const runtime::connection::PeerProfileRuntimeExchangeOptions& options)
{
    if (impl_->peerProfileService == nullptr) {
        PcProductPeerProfileResult result;
        result.code = 13;
        result.messages.push_back("peer profile service is not active");
        return result;
    }

    const runtime::connection::PeerProfileRuntimeDispatchResult dispatched =
        impl_->peerProfileService->requestPeerProfile(request, options);
    PcProductPeerProfileResult result;
    result.ok = dispatched.ok;
    result.code = dispatched.ok ? 0 : 13;
    result.messages = dispatched.messages;
    result.request = dispatched.request;
    result.snapshot = impl_->peerProfileService->snapshot();
    return result;
}

PcProductPeerProfileResult
PcProductSessionController::applyCompletedPeerProfiles()
{
    if (impl_->peerProfileService == nullptr) {
        PcProductPeerProfileResult result;
        result.code = 13;
        result.messages.push_back("peer profile service is not active");
        return result;
    }

    const runtime::qt::QtPeerProfileRuntimeApplyResult applied =
        impl_->peerProfileService->applyCompletedClientProfiles();
    PcProductPeerProfileResult result;
    result.ok = applied.ok;
    result.code = applied.ok ? 0 : 13;
    result.messages = applied.messages;
    result.readyChannels = applied.readyChannels;
    result.listeningChannels = applied.listeningChannels;
    appendUnique(impl_->listeningChannels, applied.listeningChannels);
    result.snapshot = impl_->peerProfileService->snapshot();
    return result;
}

PcProductDisplayDependenciesResult
PcProductSessionController::buildDisplayDependencies(
    const PcProductDisplayDependenciesOptions& options) const
{
    PcProductDisplayDependenciesResult result;
    result.dependencies.capture = options.capture;
    result.dependencies.captureOptions = options.captureOptions;
    result.dependencies.encoder = options.encoder.encoder;
    result.dependencies.encoderCodec = options.encoder.runtimeInfo;
    result.dependencies.decoder = options.decoder.decoder;
    result.dependencies.decoderCodec = options.decoder.runtimeInfo;
    result.dependencies.renderer = options.renderer;
    result.dependencies.inputCapture = options.inputCapture;
    result.dependencies.inputInjector = options.inputInjector;

    if (options.role == PcShellRole::Agent) {
        if (!result.dependencies.capture)
            result.messages.push_back(
                "agent display dependencies require a capture adapter");
        if (!options.encoder.ok || !result.dependencies.encoder)
            result.messages.push_back(
                "agent display dependencies require a created encoder");
    } else {
        if (!options.decoder.ok || !result.dependencies.decoder)
            result.messages.push_back(
                "client display dependencies require a created decoder");
        if (!result.dependencies.renderer)
            result.messages.push_back(
                "client display dependencies require a renderer");
    }

    if (!result.messages.empty()) {
        result.code = 19;
        return result;
    }

    result.ok = true;
    return result;
}

PcProductSessionOperationResult
PcProductSessionController::mountDisplayDependencies(
    const PcProductDisplayDependenciesOptions& options,
    const runtime::LinkChannelBindingReportOptions& linkOptions)
{
    const PcProductDisplayDependenciesResult built =
        buildDisplayDependencies(options);
    if (!built.ok) {
        PcProductSessionOperationResult result;
        result.code = 19;
        result.messages = built.messages;
        if (result.messages.empty())
            result.messages.push_back("display dependencies build failed");
        return result;
    }
    return mountProfileModules(built.dependencies, linkOptions);
}

void PcProductSessionController::stopPeerProfileService()
{
    if (impl_->peerProfileService)
        impl_->peerProfileService->stop();
    impl_->peerProfileService.reset();
}

PcProductDisplayRuntimeResult PcProductSessionController::startDisplayRuntime(
    const PcProductDisplayRuntimeStartOptions& options)
{
    if (!impl_->active || impl_->session == nullptr)
        return PcProductDisplayRuntimeResult{
            false,
            17,
            {"pc product session controller is not active"},
            {}};
    if (impl_->displayService != nullptr)
        return PcProductDisplayRuntimeResult{
            false,
            17,
            {"display runtime service is already active"},
            impl_->displayService->snapshot()};

    runtime::display::DisplayRuntimeServiceOptions runtimeOptions =
        options.runtime;
    if (options.useControllerSession)
        runtimeOptions.session = impl_->session;
    if (runtimeOptions.session == nullptr) {
        return PcProductDisplayRuntimeResult{
            false,
            17,
            {"display runtime service requires a session"},
            {}};
    }

    auto service =
        std::make_unique<runtime::display::DisplayRuntimeService>(
            runtimeOptions);
    const runtime::display::DisplayRuntimeServiceStartResult started =
        service->start();
    if (!started.ok) {
        PcProductDisplayRuntimeResult result;
        result.code = 17;
        result.messages = started.messages;
        if (result.messages.empty())
            result.messages.push_back("display runtime service start failed");
        return result;
    }

    impl_->displayService = std::move(service);
    if (options.startPumpTimer) {
        impl_->displayPumpTimer = std::make_unique<QTimer>();
        impl_->displayPumpTimer->setInterval(
            options.pumpIntervalMs > 0
                ? options.pumpIntervalMs
                : displayPumpIntervalMs(runtimeOptions));
        QObject::connect(impl_->displayPumpTimer.get(),
                         &QTimer::timeout,
                         [this]() {
                             if (impl_ && impl_->displayService)
                                 impl_->displayService->pumpOnce();
                         });
        impl_->displayPumpTimer->start();
    }

    PcProductDisplayRuntimeResult result;
    result.ok = true;
    result.snapshot = impl_->displayService->snapshot();
    return result;
}

void PcProductSessionController::stopDisplayRuntime()
{
    if (impl_->displayPumpTimer)
        impl_->displayPumpTimer->stop();
    impl_->displayPumpTimer.reset();

    if (impl_->displayService)
        impl_->displayService->stop();
    impl_->displayService.reset();
}

runtime::display::DisplayRuntimePumpResult
PcProductSessionController::pumpDisplayOnce()
{
    if (impl_->displayService == nullptr)
        return {};
    return impl_->displayService->pumpOnce();
}

PcProductClipboardRuntimeResult
PcProductSessionController::startClipboardRuntime(
    const PcProductClipboardRuntimeStartOptions& options)
{
    if (!impl_->active || impl_->session == nullptr) {
        return PcProductClipboardRuntimeResult{
            false,
            20,
            {"pc product session controller is not active"},
            {},
            {}};
    }
    if (impl_->clipboardService != nullptr) {
        return PcProductClipboardRuntimeResult{
            false,
            20,
            {"clipboard runtime service is already active"},
            impl_->clipboardService->snapshot(),
            clipboardRuntimePolicySnapshot()};
    }

    runtime::feature::ClipboardRuntimeServiceOptions runtimeOptions =
        options.runtime;
    if (options.useControllerSession)
        runtimeOptions.session = impl_->session;
    if (runtimeOptions.session == nullptr) {
        return PcProductClipboardRuntimeResult{
            false,
            20,
            {"clipboard runtime service requires a session"},
            {},
            {}};
    }
    if (runtimeOptions.endpoint == nullptr) {
        return PcProductClipboardRuntimeResult{
            false,
            20,
            {"clipboard runtime service requires an endpoint"},
            {},
            {}};
    }
    if (runtimeOptions.policy == nullptr) {
        runtimeOptions.policy =
            runtime::feature::makeClipboardRuntimePolicyFromProductPolicy(
                impl_->host.profile().clipboardPolicy);
    }

    impl_->clipboardPolicy = runtimeOptions.policy;
    auto service =
        std::make_unique<runtime::feature::ClipboardRuntimeService>(
            runtimeOptions);
    const runtime::feature::ClipboardRuntimeServiceStartResult started =
        service->start();
    if (!started.ok) {
        PcProductClipboardRuntimeResult result;
        result.code = 20;
        result.messages = started.messages;
        if (result.messages.empty())
            result.messages.push_back("clipboard runtime service start failed");
        return result;
    }

    impl_->clipboardService = std::move(service);
    if (options.startPumpTimer) {
        impl_->clipboardPumpTimer = std::make_unique<QTimer>();
        impl_->clipboardPumpTimer->setInterval(
            options.pumpIntervalMs > 0 ? options.pumpIntervalMs : 100);
        QObject::connect(impl_->clipboardPumpTimer.get(),
                         &QTimer::timeout,
                         [this]() {
                             if (!impl_ || !impl_->clipboardService)
                                 return;
                             impl_->clipboardService->pumpOnce();
                             impl_->clipboardService->expirePendingReads(
                                 runtime::qt::QtTimerBridge::monotonicNowUsec());
                         });
        impl_->clipboardPumpTimer->start();
    }

    PcProductClipboardRuntimeResult result;
    result.ok = true;
    result.snapshot = impl_->clipboardService->snapshot();
    result.policySnapshot = clipboardRuntimePolicySnapshot();
    return result;
}

void PcProductSessionController::stopClipboardRuntime()
{
    if (impl_->clipboardPumpTimer)
        impl_->clipboardPumpTimer->stop();
    impl_->clipboardPumpTimer.reset();

    if (impl_->clipboardService)
        impl_->clipboardService->stop();
    impl_->clipboardService.reset();
    impl_->clipboardPolicy.reset();
}

runtime::feature::ClipboardRuntimePumpResult
PcProductSessionController::pumpClipboardOnce()
{
    if (impl_->clipboardService == nullptr)
        return {};
    return impl_->clipboardService->pumpOnce();
}

runtime::feature::ClipboardRuntimeExpiryResult
PcProductSessionController::expireClipboardPendingReads(
    std::uint64_t nowUsec)
{
    if (impl_->clipboardService == nullptr)
        return {};
    return impl_->clipboardService->expirePendingReads(nowUsec);
}

PcProductDisplayWindowResult PcProductSessionController::showDisplayWindow(
    modules::display::IDisplayRenderer& renderer)
{
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    if (dynamic_cast<QApplication*>(QCoreApplication::instance()) == nullptr) {
        PcProductDisplayWindowResult result;
        result.supported = true;
        result.code = 18;
        result.messages.push_back("display window requires QApplication");
        return result;
    }

    auto* qtRenderer =
        dynamic_cast<adapters::qt::display::QtImageDisplayRenderer*>(&renderer);
    if (qtRenderer == nullptr) {
        PcProductDisplayWindowResult result;
        result.supported = true;
        result.code = 18;
        result.messages.push_back("display window requires QtImageDisplayRenderer");
        return result;
    }

    closeDisplayWindow();
    impl_->displayWindow =
        std::make_unique<adapters::qt::display::QtImageDisplayWindow>();
    impl_->displayWindow->attachRenderer(*qtRenderer);
    impl_->displayWindow->show();
    return updateDisplayWindowHealth();
#else
    (void)renderer;
    return unsupportedDisplayWindowResult();
#endif
}

PcProductDisplayWindowResult
PcProductSessionController::updateDisplayWindowHealth()
{
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    if (impl_->displayWindow == nullptr) {
        PcProductDisplayWindowResult result;
        result.supported = true;
        result.code = 18;
        result.messages.push_back("display window is not open");
        return result;
    }

    const std::string status =
        displayWindowStatusText(displayHealthPresentation());
    impl_->displayWindow->setStatusText(QString::fromStdString(status));

    PcProductDisplayWindowResult result;
    result.ok = true;
    result.supported = true;
    result.visible = impl_->displayWindow->isVisible();
    result.renderedFrames = impl_->displayWindow->renderedFrames();
    result.statusText = impl_->displayWindow->statusText().toStdString();
    return result;
#else
    return unsupportedDisplayWindowResult();
#endif
}

void PcProductSessionController::closeDisplayWindow()
{
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    if (impl_->displayWindow != nullptr) {
        impl_->displayWindow->detachRenderer();
        impl_->displayWindow->close();
    }
    impl_->displayWindow.reset();
#endif
}

bool PcProductSessionController::active() const
{
    return impl_->active;
}

protocol::SessionId PcProductSessionController::sessionId() const
{
    return impl_->sessionId;
}

session::Session* PcProductSessionController::session()
{
    return impl_->session;
}

const session::Session* PcProductSessionController::session() const
{
    return impl_->session;
}

runtime::RuntimeHost& PcProductSessionController::host()
{
    return impl_->host;
}

const runtime::RuntimeHost& PcProductSessionController::host() const
{
    return impl_->host;
}

runtime::qt::QtRuntimeTransportManager*
PcProductSessionController::transportManager()
{
    return impl_->transportManager.get();
}

runtime::qt::QtPeerProfileRuntimeService*
PcProductSessionController::peerProfileService()
{
    return impl_->peerProfileService.get();
}

runtime::qt::QtReconnectRuntimeService*
PcProductSessionController::reconnectService()
{
    return impl_->reconnectService.get();
}

runtime::qt::QtPeerProfileRuntimeServiceSnapshot
PcProductSessionController::peerProfileSnapshot() const
{
    if (impl_->peerProfileService == nullptr)
        return {};
    return impl_->peerProfileService->snapshot();
}

runtime::display::DisplayRuntimeService*
PcProductSessionController::displayRuntimeService()
{
    return impl_->displayService.get();
}

runtime::display::DisplayRuntimeServiceSnapshot
PcProductSessionController::displayRuntimeSnapshot() const
{
    if (impl_->displayService == nullptr)
        return {};
    return impl_->displayService->snapshot();
}

runtime::feature::ClipboardRuntimeService*
PcProductSessionController::clipboardRuntimeService()
{
    return impl_->clipboardService.get();
}

runtime::feature::ClipboardRuntimeServiceSnapshot
PcProductSessionController::clipboardRuntimeSnapshot() const
{
    if (impl_->clipboardService == nullptr)
        return {};
    return impl_->clipboardService->snapshot();
}

runtime::feature::ClipboardRuntimePolicySnapshot
PcProductSessionController::clipboardRuntimePolicySnapshot() const
{
    auto configurable =
        std::dynamic_pointer_cast<
            runtime::feature::ConfigurableClipboardRuntimePolicy>(
            impl_->clipboardPolicy);
    if (configurable == nullptr)
        return {};
    return configurable->snapshot();
}

bool PcProductSessionController::displayWindowVisible() const
{
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    return impl_->displayWindow != nullptr && impl_->displayWindow->isVisible();
#else
    return false;
#endif
}

runtime::SessionRuntimeDiagnosticsSnapshot
PcProductSessionController::diagnostics(
    const runtime::SessionRuntimeDiagnosticsOptions& options) const
{
    if (!impl_->active || impl_->sessionId == 0) {
        runtime::SessionRuntimeDiagnosticsSnapshot result;
        result.messages.push_back("pc product session controller is not active");
        return result;
    }

    runtime::SessionRuntimeDiagnosticsOptions merged = options;
    appendUnique(merged.linkReportOptions.listeningChannels,
                 impl_->listeningChannels);
    return runtime::buildSessionRuntimeDiagnostics(
        const_cast<runtime::RuntimeHost&>(impl_->host),
        impl_->sessionId,
        merged);
}

runtime::DisplayProductHealthPresentation
PcProductSessionController::displayHealthPresentation(
    const runtime::SessionRuntimeDiagnosticsOptions& options) const
{
    return runtime::buildDisplayProductHealthPresentation(diagnostics(options));
}

PcProductSessionSnapshot PcProductSessionController::snapshot(
    const runtime::SessionRuntimeDiagnosticsOptions& options) const
{
    PcProductSessionSnapshot result;
    result.active = impl_->active;
    result.reconnectActive =
        impl_->reconnectService != nullptr && impl_->reconnectService->active();
    result.peerProfileActive =
        impl_->peerProfileService != nullptr &&
        impl_->peerProfileService->active();
    result.displayRuntimeActive =
        impl_->displayService != nullptr &&
        impl_->displayService->snapshot().active;
    result.clipboardRuntimeActive =
        impl_->clipboardService != nullptr &&
        impl_->clipboardService->snapshot().active;
    result.sessionId = impl_->sessionId;
    result.sessionState = impl_->session != nullptr
                              ? impl_->session->state()
                              : session::SessionState::Created;
    result.connectorCount = impl_->transportManager != nullptr
                                ? impl_->transportManager->connectorCount()
                                : 0;
    result.listenerCount = impl_->transportManager != nullptr
                               ? impl_->transportManager->listenerCount()
                               : 0;
    result.listeningChannels = impl_->listeningChannels;
    result.diagnostics = diagnostics(options);
    result.peerProfile = peerProfileSnapshot();
    result.displayHealth =
        runtime::buildDisplayProductHealthPresentation(result.diagnostics);
    result.displayRuntime = displayRuntimeSnapshot();
    result.clipboardProductPolicy =
        runtime::feature::buildClipboardProductPolicyPresentation(
            impl_->host.profile().clipboardPolicy);
    result.clipboardRuntime = clipboardRuntimeSnapshot();
    result.clipboardRuntimePolicy = clipboardRuntimePolicySnapshot();
    result.clipboardHealth =
        runtime::feature::buildClipboardProductHealthPresentation(
            result.clipboardRuntime,
            result.clipboardRuntimePolicy);
    result.displayWindowVisible = displayWindowVisible();
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    if (impl_->displayWindow != nullptr)
        result.displayWindowStatusText =
            impl_->displayWindow->statusText().toStdString();
#endif
    return result;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
