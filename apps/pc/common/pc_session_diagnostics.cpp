#include "pc_session_diagnostics.h"

#include "pc_display_diagnostics.h"
#include "pc_shell_options.h"

#include <iostream>
#include <string>

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
#include <QString>

#include "fusiondesk/adapters/qt/display/qt_image_display_window.h"
#endif
#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/runtime/display/display_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_reconnect_runtime_service.h"
#include "fusiondesk/runtime/session/display_product_health_presenter.h"
#include "fusiondesk/runtime/session/session_runtime_diagnostics.h"

namespace fusiondesk {
namespace apps {
namespace pc {
namespace {

void appendUniqueChannel(std::vector<network::ChannelKey>& channels,
                         const network::ChannelKey& key)
{
    for (const network::ChannelKey& existing : channels) {
        if (existing.channelId == key.channelId &&
            existing.channelType == key.channelType)
            return;
    }
    channels.push_back(key);
}

runtime::LinkChannelBindingReportOptions buildLinkReportOptions(
    const std::vector<network::ChannelKey>& listeningChannels)
{
    runtime::LinkChannelBindingReportOptions options;
    for (const network::ChannelKey& key : listeningChannels)
        appendUniqueChannel(options.listeningChannels, key);
    return options;
}

std::string joinedModules(const std::vector<std::string>& modules)
{
    std::string result;
    for (const std::string& module : modules) {
        if (!result.empty())
            result += ",";
        result += module;
    }
    return result;
}

std::string moduleVersionString(const module::ModuleVersion& version)
{
    return std::to_string(version.major) + "." +
           std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

void writeReconnectChannel(const char* phase, const network::ChannelKey& channel)
{
    std::cout << "reconnect.diagnostics.channel"
              << " phase=" << phase
              << " id=" << channel.channelId
              << " type=" << static_cast<unsigned int>(channel.channelType)
              << std::endl;
}

void writeReconnectDiagnostics(
    const runtime::qt::QtReconnectRuntimeServiceSnapshot& snapshot,
    const char* phase)
{
    const runtime::connection::ReconnectDiagnosticsReport& report =
        snapshot.runtime.diagnostics;

    std::cout << "reconnect.diagnostics"
              << " phase=" << phase
              << " active=" << boolValue(report.active)
              << " timerRunning=" << boolValue(snapshot.timerRunning)
              << " attempted=" << boolValue(report.attempted)
              << " complete=" << boolValue(report.complete)
              << " ok=" << boolValue(report.ok)
              << " pending=" << report.pendingRequests
              << " expired=" << report.expiredRequests
              << " timeoutOk=" << boolValue(report.timeoutOk)
              << " clientSession=" << report.clientSessionId
              << " agentSession=" << report.agentSessionId
              << " keyframe=" << boolValue(report.requestDisplayKeyframe)
              << " reason=" << report.reason
              << std::endl;

    for (const network::ChannelKey& channel : report.degradedChannels)
        writeReconnectChannel(phase, channel);

    for (const runtime::connection::ReconnectDiagnosticStageReport& stage :
         report.stages) {
        std::cout << "reconnect.diagnostics.stage"
                  << " phase=" << phase
                  << " name=" << runtime::connection::reconnectDiagnosticStageName(stage.stage)
                  << " attempted=" << boolValue(stage.attempted)
                  << " ok=" << boolValue(stage.ok)
                  << " pending=" << boolValue(stage.pending)
                  << " channels=" << stage.channels.size()
                  << " messages=" << stage.messages.size()
                  << std::endl;
    }

    for (const std::string& message : report.messages)
        std::cout << "reconnect.diagnostics.message"
                  << " phase=" << phase
                  << " text=" << message
                  << std::endl;
}

void writeSessionDiagnosticsChannel(const char* phase,
                                    const runtime::LinkChannelBindingItem& channel)
{
    std::cout << "session.diagnostics.channel"
              << " phase=" << phase
              << " id=" << channel.key.channelId
              << " type=" << static_cast<unsigned int>(channel.key.channelType)
              << " name=" << channel.name
              << " required=" << boolValue(channel.moduleRequired)
              << " listening=" << boolValue(channel.listening)
              << " bound=" << boolValue(channel.bound)
              << " ready=" << boolValue(channel.ready)
              << " degraded=" << boolValue(channel.degraded)
              << " blocked=" << boolValue(channel.blocked)
              << " modules=" << joinedModules(channel.requiredByModules)
              << " message=" << channel.message
              << std::endl;
}

void writeSessionDiagnosticsRemoteModule(
    const char* phase,
    const module::ModuleManifest& manifest)
{
    std::cout << "session.diagnostics.remote_module"
              << " phase=" << phase
              << " module=" << manifest.moduleId
              << " version=" << moduleVersionString(manifest.version)
              << " roleFlags=" << manifest.roleFlags
              << " runModeFlags=" << manifest.runModeFlags
              << " channels=" << manifest.channels.size()
              << " peers=" << manifest.compatiblePeers.size()
              << std::endl;
}

void writeSessionDiagnosticsDisplayCapture(
    const char* phase,
    const runtime::DisplayCaptureRuntimeStatus& capture)
{
    std::cout << "session.diagnostics.display_capture"
              << " phase=" << phase
              << " module=" << capture.moduleId
              << " backend=" << capture.backendId
              << " includeCursor=" << boolValue(capture.includeCursor)
              << " capturedFrames=" << capture.capturedFrames
              << " sentFrames=" << capture.sentFrames
              << " capturedPixelBytes=" << capture.capturedPixelBytes
              << " encodedPayloadBytes=" << capture.encodedPayloadBytes
              << " sentPayloadBytes=" << capture.sentPayloadBytes
              << " lastSentPayloadBytes=" << capture.lastSentPayloadBytes
              << " captureErrors=" << capture.captureErrors
              << " code=" << static_cast<unsigned int>(capture.status.code)
              << " status=" << modules::display::displayCaptureStatusCodeName(
                     capture.status.code)
              << " action=" << runtime::display::displayCaptureRecoveryActionName(
                     capture.recoveryAction)
              << " native=" << capture.status.nativeCode
              << " recoverable=" << boolValue(capture.status.recoverable)
              << " message=" << capture.status.message
              << std::endl;
}

void writeSessionDiagnosticsDisplayCodecMessage(
    const char* phase,
    const runtime::DisplayCodecRuntimeStatus& status,
    std::size_t index,
    const std::string& message)
{
    std::cout << "session.diagnostics.display_codec.message"
              << " phase=" << phase
              << " module=" << status.moduleId
              << " direction=" << status.direction
              << " index=" << index
              << " text=" << message
              << std::endl;
}

void writeSessionDiagnosticsDisplayCodec(
    const char* phase,
    const runtime::DisplayCodecRuntimeStatus& status)
{
    const modules::display::DisplayCodecRuntimeInfo& codec = status.codec;
    std::cout << "session.diagnostics.display_codec"
              << " phase=" << phase
              << " module=" << status.moduleId
              << " direction=" << status.direction
              << " selected=" << boolValue(codec.selected)
              << " adapter=" << codec.adapterId
              << " codec=" << codec.codec
              << " backend=" << codec.backend
              << " fallback=" << boolValue(codec.fallback)
              << " hardware=" << boolValue(codec.hardwareAccelerated)
              << " zeroCopy=" << boolValue(codec.zeroCopy)
              << " lowLatency=" << boolValue(codec.lowLatency)
              << " deltaFrames=" << boolValue(codec.deltaFrames)
              << " selectionMode=" << codec.selectionMode
              << " frames=" << status.frames
              << " payloadBytes=" << status.payloadBytes
              << " errors=" << status.errors
              << " pendingFrames=" << status.pendingFrames
              << " delayedFrames=" << status.delayedFrames
              << " fallbackReason=" << codec.fallbackReason
              << " messages=" << codec.messages.size()
              << std::endl;

    for (std::size_t index = 0; index < codec.messages.size(); ++index)
        writeSessionDiagnosticsDisplayCodecMessage(phase,
                                                   status,
                                                   index,
                                                   codec.messages[index]);
}

void writeSessionDiagnosticsDisplayHealthMessage(
    const char* phase,
    std::size_t index,
    const std::string& message)
{
    std::cout << "session.diagnostics.display_health.message"
              << " phase=" << phase
              << " index=" << index
              << " text=" << message
              << std::endl;
}

void writeSessionDiagnosticsDisplayHealth(
    const runtime::DisplayProductDiagnosticsSnapshot& health,
    const char* phase)
{
    const runtime::DisplayProductHealthPresentation presentation =
        runtime::buildDisplayProductHealthPresentation(health);
    std::cout << "session.diagnostics.display_health"
              << " phase=" << phase
              << " usable=" << boolValue(health.usable)
              << " health="
              << runtime::displayProductHealthLevelName(health.health)
              << " status=" << presentation.statusCode
              << " action=" << presentation.primaryActionCode
              << " session=" << health.sessionId
              << " sessionOk=" << boolValue(health.sessionOk)
              << " linkReady=" << boolValue(health.linkReady)
              << " blocked=" << health.blockedChannels
              << " displayModules=" << health.displayModules
              << " runningDisplayModules="
              << health.runningDisplayModules
              << " capturePresent=" << boolValue(health.capturePresent)
              << " captureBackend=" << health.captureBackendId
              << " captureStatus="
              << modules::display::displayCaptureStatusCodeName(
                     health.captureStatus.code)
              << " captureRecoverable="
              << boolValue(health.captureStatus.recoverable)
              << " captureErrors=" << health.captureErrors
              << " captureRecoveryAction="
              << runtime::display::displayCaptureRecoveryActionName(
                     health.captureRecoveryAction)
              << " codecPresent=" << boolValue(health.codecPresent)
              << " codecSelected=" << boolValue(health.codecSelected)
              << " codecAdapter=" << health.codecAdapterId
              << " codec=" << health.codec
              << " codecBackend=" << health.codecBackend
              << " selectionMode=" << health.codecSelectionMode
              << " codecFallback=" << boolValue(health.codecFallback)
              << " codecDeltaFrames="
              << boolValue(health.codecDeltaFrames)
              << " codecLowLatency=" << boolValue(health.codecLowLatency)
              << " codecErrors=" << health.codecErrors
              << " codecPendingFrames=" << health.codecPendingFrames
              << " codecDelayedFrames=" << health.codecDelayedFrames
              << " codecPayloadBytes=" << health.codecPayloadBytes
              << " captureState=" << presentation.captureState
              << " codecState=" << presentation.codecState
              << " codecFallbackWarning="
              << boolValue(presentation.showCodecFallbackWarning)
              << " codecLatencyWarning="
              << boolValue(presentation.showCodecLatencyWarning)
              << " captureRecoveryWarning="
              << boolValue(presentation.showCaptureRecoveryWarning)
              << " fallbackReason=" << health.codecFallbackReason
              << " messages=" << health.messages.size()
              << std::endl;

    for (std::size_t index = 0; index < health.messages.size(); ++index)
        writeSessionDiagnosticsDisplayHealthMessage(phase,
                                                    index,
                                                    health.messages[index]);
}

void writeSessionDiagnostics(
    const runtime::SessionRuntimeDiagnosticsSnapshot& snapshot,
    const char* phase)
{
    std::cout << "session.diagnostics"
              << " phase=" << phase
              << " ok=" << boolValue(snapshot.ok)
              << " session=" << snapshot.sessionId
              << " state=" << static_cast<unsigned int>(snapshot.sessionState)
              << " linkReady=" << boolValue(snapshot.linkReady)
              << " blocked=" << snapshot.blockedChannels
              << " mounted=" << snapshot.mountedModules
              << " running=" << snapshot.runningModules
              << " remoteSession=" << snapshot.remoteModuleInventory.peerSessionId
              << " remoteModules=" << snapshot.remoteModules
              << " events=" << snapshot.diagnostics.size()
              << " messages=" << snapshot.messages.size()
              << std::endl;

    for (const runtime::LinkChannelBindingItem& channel :
         snapshot.linkChannels.channels) {
        if (channel.moduleRequired || channel.blocked || !channel.ready)
            writeSessionDiagnosticsChannel(phase, channel);
    }

    for (const module::ModuleManifest& manifest :
         snapshot.remoteModuleInventory.manifests)
        writeSessionDiagnosticsRemoteModule(phase, manifest);

    for (const runtime::DisplayCaptureRuntimeStatus& capture :
         snapshot.displayCaptureStatuses)
        writeSessionDiagnosticsDisplayCapture(phase, capture);

    for (const runtime::DisplayCodecRuntimeStatus& codec :
         snapshot.displayCodecStatuses)
        writeSessionDiagnosticsDisplayCodec(phase, codec);

    writeSessionDiagnosticsDisplayHealth(
        runtime::buildDisplayProductDiagnostics(snapshot),
        phase);

    for (const std::string& message : snapshot.messages)
        std::cout << "session.diagnostics.message"
                  << " phase=" << phase
                  << " text=" << message
                  << std::endl;
}

void writeDisplayRuntimeDiagnostics(
    const runtime::display::DisplayRuntimeServiceSnapshot& snapshot,
    const char* phase)
{
    std::cout << "display.runtime"
              << " phase=" << phase
              << " active=" << boolValue(snapshot.active)
              << " pumpAgentFrames=" << boolValue(snapshot.pumpAgentFrames)
              << " targetFps=" << snapshot.targetFps
              << " pumpCount=" << snapshot.pumpCount
              << " skippedByFrameRate=" << snapshot.skippedByFrameRate
              << " frameAttempts=" << snapshot.frameAttempts
              << " lastPumpUsec=" << snapshot.lastPumpUsec
              << " lastFrameAttemptUsec=" << snapshot.lastFrameAttemptUsec
              << " firstFrameSentUsec=" << snapshot.firstFrameSentUsec
              << " lastFrameSentUsec=" << snapshot.lastFrameSentUsec
              << " lastFrameAgeUsec=" << snapshot.lastFrameAgeUsec
              << " effectiveFpsX1000=" << snapshot.effectiveFpsX1000
              << " consecutiveFrameMisses="
              << snapshot.consecutiveFrameMisses
              << " framesSent=" << snapshot.framesSent
              << " capturedPixelBytes=" << snapshot.capturedPixelBytes
              << " encodedPayloadBytes=" << snapshot.encodedPayloadBytes
              << " sentPayloadBytes=" << snapshot.sentPayloadBytes
              << " lastCapturedPixelBytes="
              << snapshot.lastCapturedPixelBytes
              << " lastEncodedPayloadBytes="
              << snapshot.lastEncodedPayloadBytes
              << " lastSentPayloadBytes=" << snapshot.lastSentPayloadBytes
              << " effectiveBitrateKbps="
              << snapshot.effectiveBitrateKbps
              << " droppedFrames=" << snapshot.droppedFrames
              << " sendFailures=" << snapshot.sendFailures
              << " missingAgentModules=" << snapshot.missingAgentModules
              << " missingClientModules=" << snapshot.missingClientModules
              << " firstFrameTimeouts=" << snapshot.firstFrameTimeouts
              << " reconnectFrameTimeouts="
              << snapshot.reconnectFrameTimeouts
              << " keyframeRequestsSent=" << snapshot.keyframeRequestsSent
              << " keyframeRequestFailures="
              << snapshot.keyframeRequestFailures
              << " keyframeRequestTimeouts="
              << snapshot.keyframeRequestTimeouts
              << " captureRecoveryAttempts="
              << snapshot.captureRecoveryAttempts
              << " captureRecoverySuccesses="
              << snapshot.captureRecoverySuccesses
              << " captureRecoveryFailures="
              << snapshot.captureRecoveryFailures
              << " captureRecoveryCooldownBlocks="
              << snapshot.captureRecoveryCooldownBlocks
              << " captureRecoverySwitchPromotions="
              << snapshot.captureRecoverySwitchPromotions
              << " captureBackend=" << snapshot.captureBackendId
              << " captureErrors=" << snapshot.captureErrors
              << " captureStatus="
              << modules::display::displayCaptureStatusCodeName(
                     snapshot.lastCaptureStatus.code)
              << " recoveryAction="
              << runtime::display::displayCaptureRecoveryActionName(
                     snapshot.lastCaptureRecoveryAction)
              << std::endl;
}

} // namespace

runtime::LinkChannelBindingReportOptions makeLinkReportOptions(
    const std::vector<network::ChannelKey>& listeningChannels)
{
    return buildLinkReportOptions(listeningChannels);
}

void writeReconnectDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::qt::QtReconnectRuntimeService& service,
    const char* phase)
{
    if (hasArg(argc, argv, "--print-reconnect-diagnostics"))
        writeReconnectDiagnostics(service.snapshot(), phase);
}

void writeSessionDiagnosticsIfRequested(
    int argc,
    char** argv,
    runtime::RuntimeHost& host,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelKey>& listeningChannels,
    const char* phase)
{
    if (!hasArg(argc, argv, "--print-session-diagnostics"))
        return;

    runtime::SessionRuntimeDiagnosticsOptions options;
    options.linkReportOptions = buildLinkReportOptions(listeningChannels);
    writeSessionDiagnostics(
        runtime::buildSessionRuntimeDiagnostics(host, sessionId, options),
        phase);
}

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
void updateDisplayWindowHealth(
    adapters::qt::display::QtImageDisplayWindow* window,
    runtime::RuntimeHost& host,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelKey>& listeningChannels)
{
    if (window == nullptr)
        return;

    runtime::SessionRuntimeDiagnosticsOptions options;
    options.linkReportOptions = buildLinkReportOptions(listeningChannels);
    const runtime::DisplayProductHealthPresentation presentation =
        runtime::buildDisplayProductHealthPresentation(
            runtime::buildSessionRuntimeDiagnostics(host, sessionId, options));
    const std::string statusText =
        presentation.statusCode + " | " + presentation.primaryActionCode +
        " | " + presentation.healthName;
    window->setStatusText(QString::fromStdString(statusText));
}
#endif

void writeDisplayRuntimeDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::display::DisplayRuntimeService* service,
    const char* phase)
{
    if (!hasArg(argc, argv, "--print-display-runtime-diagnostics") ||
        service == nullptr)
        return;

    writeDisplayRuntimeDiagnostics(service->snapshot(), phase);
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
