#ifndef FUSIONDESK_APPS_PC_COMMON_PC_PRODUCT_SESSION_CONTROLLER_H
#define FUSIONDESK_APPS_PC_COMMON_PC_PRODUCT_SESSION_CONTROLLER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pc_app_shell.h"
#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/runtime/connection/peer_profile_runtime_service.h"
#include "fusiondesk/runtime/display/display_codec_negotiation.h"
#include "fusiondesk/runtime/display/display_codec_selection.h"
#include "fusiondesk/runtime/display/display_runtime_service.h"
#include "fusiondesk/runtime/feature/clipboard_product_presenter.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/session/display_product_health_presenter.h"
#include "fusiondesk/runtime/session/session_mainline.h"
#include "fusiondesk/runtime/session/session_runtime_diagnostics.h"

namespace fusiondesk {

namespace modules {
namespace display {
class IDisplayCapture;
class IDisplayRenderer;
class IVideoDecoder;
class IVideoEncoder;
} // namespace display
namespace input {
class IInputCapture;
class IInputInjector;
} // namespace input
} // namespace modules

namespace runtime {
namespace display {
class IDisplayCodecBackendFactory;
} // namespace display
namespace qt {
class QtReconnectRuntimeService;
class QtRuntimeTransportManager;
} // namespace qt
} // namespace runtime

namespace apps {
namespace pc {

struct PcProductSessionStartOptions
{
    PcShellRole role = PcShellRole::Client;
    runtime::RuntimeOptions runtimeOptions;
    session::SessionCreateOptions sessionOptions;
    bool useDefaultSessionOptions = true;
    bool startReconnectRuntime = true;
    protocol::MessageId firstReconnectMessageId = 15000;
};

struct PcProductSessionOperationResult
{
    bool ok = false;
    int code = 0;
    std::vector<std::string> messages;
    std::vector<network::ChannelKey> readyChannels;
    std::vector<network::ChannelKey> listeningChannels;
    runtime::SessionMainlineReport mainline;
};

struct PcProductDisplayRuntimeStartOptions
{
    runtime::display::DisplayRuntimeServiceOptions runtime;
    bool useControllerSession = true;
    bool startPumpTimer = true;
    int pumpIntervalMs = 0;
};

struct PcProductDisplayRuntimeResult
{
    bool ok = false;
    int code = 0;
    std::vector<std::string> messages;
    runtime::display::DisplayRuntimeServiceSnapshot snapshot;
};

struct PcProductClipboardRuntimeStartOptions
{
    runtime::feature::ClipboardRuntimeServiceOptions runtime;
    bool useControllerSession = true;
    bool startPumpTimer = true;
    int pumpIntervalMs = 100;
};

struct PcProductClipboardRuntimeResult
{
    bool ok = false;
    int code = 0;
    std::vector<std::string> messages;
    runtime::feature::ClipboardRuntimeServiceSnapshot snapshot;
    runtime::feature::ClipboardRuntimePolicySnapshot policySnapshot;
};

struct PcProductDisplayWindowResult
{
    bool ok = false;
    int code = 0;
    bool supported = false;
    bool visible = false;
    int renderedFrames = 0;
    std::string statusText;
    std::vector<std::string> messages;
};

struct PcProductPeerProfileStartOptions
{
    runtime::connection::PeerProfileRuntimeServiceStartOptions runtime;
    bool autoApplyAgentListenProfile = true;
    bool startTimer = true;
    std::uint32_t timerIntervalMs = 10;
    protocol::MessageId firstMessageId = 21000;
};

struct PcProductPeerProfileResult
{
    bool ok = false;
    int code = 0;
    std::vector<std::string> messages;
    std::vector<network::ChannelKey> readyChannels;
    std::vector<network::ChannelKey> listeningChannels;
    protocol::PacketEnvelope request;
    runtime::qt::QtPeerProfileRuntimeServiceSnapshot snapshot;
};

struct PcProductDisplayCodecPeerProfileResult
{
    bool ok = false;
    int code = 0;
    bool attempted = false;
    bool extensionAdded = false;
    runtime::display::DisplayCodecNegotiationResult negotiation;
    runtime::display::DisplayCodecSelectionRequest encoderRequest;
    runtime::display::DisplayCodecSelectionRequest decoderRequest;
    std::vector<std::string> messages;
};

struct PcProductDisplayCodecInventoryOptions
{
    runtime::ProductDisplayCodecPolicy policy;
    bool useControllerProductPolicy = true;
    runtime::display::DisplayPlatformFamily platform =
        runtime::display::DisplayPlatformFamily::WindowsDesktop;
    runtime::display::DisplayTargetArchitecture architecture =
        runtime::display::DisplayTargetArchitecture::Unknown;
    runtime::display::DisplayTargetSocProfile socProfile =
        runtime::display::DisplayTargetSocProfile::Unknown;
    std::string requestedAdapterId;
    std::vector<runtime::display::DisplayCodecId> codecPreferenceOverride;
    bool requireLowLatency = true;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct PcProductDisplayCodecInventoryResult
{
    bool ok = false;
    int code = 0;
    runtime::display::DisplayCodecSelectionRequest request;
    std::vector<std::string> messages;
};

struct PcProductDisplayCodecCreateOptions
{
    runtime::ProductDisplayCodecPolicy policy;
    bool useControllerProductPolicy = true;
};

struct PcProductDisplayCodecCreateResult
{
    bool ok = false;
    int code = 0;
    runtime::display::DisplayCodecSelectionRequest request;
    runtime::display::DisplayCodecSelectionResult selection;
    std::shared_ptr<modules::display::IVideoEncoder> encoder;
    std::shared_ptr<modules::display::IVideoDecoder> decoder;
    modules::display::DisplayCodecRuntimeInfo runtimeInfo;
    std::vector<std::string> messages;
};

struct PcProductDisplayDependenciesOptions
{
    PcShellRole role = PcShellRole::Client;
    std::shared_ptr<modules::display::IDisplayCapture> capture;
    modules::display::DisplayCaptureOpenOptions captureOptions;
    PcProductDisplayCodecCreateResult encoder;
    PcProductDisplayCodecCreateResult decoder;
    std::shared_ptr<modules::display::IDisplayRenderer> renderer;
    std::shared_ptr<modules::input::IInputCapture> inputCapture;
    std::shared_ptr<modules::input::IInputInjector> inputInjector;
};

struct PcProductDisplayDependenciesResult
{
    bool ok = false;
    int code = 0;
    runtime::DisplayMvpDependencies dependencies;
    std::vector<std::string> messages;
};

struct PcProductSessionSnapshot
{
    bool active = false;
    bool reconnectActive = false;
    bool peerProfileActive = false;
    bool displayRuntimeActive = false;
    bool clipboardRuntimeActive = false;
    protocol::SessionId sessionId = 0;
    session::SessionState sessionState = session::SessionState::Created;
    std::size_t connectorCount = 0;
    std::size_t listenerCount = 0;
    std::vector<network::ChannelKey> listeningChannels;
    runtime::SessionRuntimeDiagnosticsSnapshot diagnostics;
    runtime::qt::QtPeerProfileRuntimeServiceSnapshot peerProfile;
    runtime::DisplayProductHealthPresentation displayHealth;
    runtime::display::DisplayRuntimeServiceSnapshot displayRuntime;
    runtime::feature::ClipboardProductPolicyPresentation clipboardProductPolicy;
    runtime::feature::ClipboardProductHealthPresentation clipboardHealth;
    runtime::feature::ClipboardRuntimeServiceSnapshot clipboardRuntime;
    runtime::feature::ClipboardRuntimePolicySnapshot clipboardRuntimePolicy;
    bool displayWindowVisible = false;
    std::string displayWindowStatusText;
};

class PcProductSessionController
{
public:
    PcProductSessionController();
    ~PcProductSessionController();

    PcProductSessionController(const PcProductSessionController&) = delete;
    PcProductSessionController& operator=(const PcProductSessionController&) = delete;

    PcProductSessionOperationResult start(
        const PcProductSessionStartOptions& options = {});
    void stop(const std::string& reason = "pc product session controller stop");

    PcProductSessionOperationResult applyTransportProfile(
        const std::string& path);
    PcProductSessionOperationResult applyListenProfile(
        const std::string& path);
    PcProductSessionOperationResult mountProfileModules(
        const runtime::DisplayMvpDependencies& dependencies,
        const runtime::LinkChannelBindingReportOptions& linkOptions = {});
    PcProductSessionOperationResult startProfileModules(
        const module::ModuleStartOptions& startOptions = {},
        const runtime::LinkChannelBindingReportOptions& linkOptions = {});
    PcProductPeerProfileResult startPeerProfileService(
        const PcProductPeerProfileStartOptions& options = {});
    PcProductPeerProfileResult requestPeerProfile(
        const runtime::connection::PeerProfileExchangeRequest& request,
        const runtime::connection::PeerProfileRuntimeExchangeOptions& options = {});
    PcProductPeerProfileResult applyCompletedPeerProfiles();
    PcProductDisplayCodecPeerProfileResult appendDisplayCodecPeerProfileDecoderRequest(
        runtime::connection::PeerProfileExchangeRequest& request,
        runtime::display::DisplayCodecSelectionRequest decoderRequest) const;
    PcProductDisplayCodecPeerProfileResult handleDisplayCodecPeerProfileRequest(
        const runtime::connection::PeerProfileExchangeRequest& request,
        runtime::connection::PeerProfileExchangeResult& exchange,
        runtime::display::DisplayCodecSelectionRequest encoderRequest) const;
    PcProductDisplayCodecPeerProfileResult readDisplayCodecPeerProfileCompletion(
        const runtime::qt::QtPeerProfileRuntimeServiceSnapshot& snapshot) const;
    PcProductDisplayCodecInventoryResult buildDisplayCodecInventoryRequest(
        runtime::display::DisplayCodecDirection direction,
        const runtime::display::IDisplayCodecBackendFactory& factory,
        const PcProductDisplayCodecInventoryOptions& options = {}) const;
    PcProductDisplayCodecInventoryResult pinDisplayCodecInventoryRequest(
        PcShellRole role,
        runtime::display::DisplayCodecSelectionRequest request,
        const runtime::display::DisplayCodecNegotiationResult& negotiation) const;
    PcProductDisplayCodecCreateResult createDisplayEncoder(
        const runtime::display::IDisplayCodecBackendFactory& factory,
        runtime::display::DisplayCodecSelectionRequest request,
        const PcProductDisplayCodecCreateOptions& options = {}) const;
    PcProductDisplayCodecCreateResult createDisplayDecoder(
        const runtime::display::IDisplayCodecBackendFactory& factory,
        runtime::display::DisplayCodecSelectionRequest request,
        const PcProductDisplayCodecCreateOptions& options = {}) const;
    PcProductDisplayDependenciesResult buildDisplayDependencies(
        const PcProductDisplayDependenciesOptions& options) const;
    PcProductSessionOperationResult mountDisplayDependencies(
        const PcProductDisplayDependenciesOptions& options,
        const runtime::LinkChannelBindingReportOptions& linkOptions = {});
    void stopPeerProfileService();
    PcProductDisplayRuntimeResult startDisplayRuntime(
        const PcProductDisplayRuntimeStartOptions& options = {});
    void stopDisplayRuntime();
    runtime::display::DisplayRuntimePumpResult pumpDisplayOnce();
    PcProductClipboardRuntimeResult startClipboardRuntime(
        const PcProductClipboardRuntimeStartOptions& options = {});
    void stopClipboardRuntime();
    runtime::feature::ClipboardRuntimePumpResult pumpClipboardOnce();
    runtime::feature::ClipboardRuntimeExpiryResult expireClipboardPendingReads(
        std::uint64_t nowUsec);
    PcProductDisplayWindowResult showDisplayWindow(
        modules::display::IDisplayRenderer& renderer);
    PcProductDisplayWindowResult updateDisplayWindowHealth();
    void closeDisplayWindow();

    bool active() const;
    protocol::SessionId sessionId() const;
    session::Session* session();
    const session::Session* session() const;
    runtime::RuntimeHost& host();
    const runtime::RuntimeHost& host() const;
    runtime::qt::QtRuntimeTransportManager* transportManager();
    runtime::qt::QtPeerProfileRuntimeService* peerProfileService();
    runtime::qt::QtReconnectRuntimeService* reconnectService();
    runtime::qt::QtPeerProfileRuntimeServiceSnapshot peerProfileSnapshot() const;
    runtime::display::DisplayRuntimeService* displayRuntimeService();
    runtime::display::DisplayRuntimeServiceSnapshot displayRuntimeSnapshot() const;
    runtime::feature::ClipboardRuntimeService* clipboardRuntimeService();
    runtime::feature::ClipboardRuntimeServiceSnapshot clipboardRuntimeSnapshot() const;
    runtime::feature::ClipboardRuntimePolicySnapshot
    clipboardRuntimePolicySnapshot() const;
    bool displayWindowVisible() const;

    runtime::SessionRuntimeDiagnosticsSnapshot diagnostics(
        const runtime::SessionRuntimeDiagnosticsOptions& options = {}) const;
    runtime::DisplayProductHealthPresentation displayHealthPresentation(
        const runtime::SessionRuntimeDiagnosticsOptions& options = {}) const;
    PcProductSessionSnapshot snapshot(
        const runtime::SessionRuntimeDiagnosticsOptions& options = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_PRODUCT_SESSION_CONTROLLER_H
