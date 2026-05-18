#include "fusiondesk/runtime/session/session_runtime_diagnostics.h"

#include <utility>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/modules/display/display_modules.h"

namespace fusiondesk {
namespace runtime {

namespace {

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

std::size_t countBlockedChannels(const LinkChannelBindingReport& report)
{
    std::size_t count = 0;
    for (const LinkChannelBindingItem& item : report.channels) {
        if (item.blocked)
            ++count;
    }
    return count;
}

std::size_t countRunningModules(const session::SessionSnapshot& snapshot)
{
    std::size_t count = 0;
    for (const module::ModuleSnapshot& module : snapshot.moduleSnapshots) {
        if (module.state == module::ModuleState::Running)
            ++count;
    }
    return count;
}

std::vector<DisplayCaptureRuntimeStatus> collectDisplayCaptureStatuses(
    session::Session& session)
{
    std::vector<DisplayCaptureRuntimeStatus> result;
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return result;

    auto* agent = dynamic_cast<modules::display::DisplayAgentModule*>(
        host->module("display.screen.agent"));
    if (agent == nullptr)
        return result;

    DisplayCaptureRuntimeStatus status;
    status.moduleId = "display.screen.agent";
    const modules::display::DisplayAgentSnapshot snapshot = agent->snapshot();
    status.backendId = snapshot.captureBackendId;
    status.includeCursor = snapshot.includeCursor;
    status.capturedFrames = snapshot.capturedFrames;
    status.sentFrames = snapshot.sentFrames;
    status.capturedPixelBytes = snapshot.capturedPixelBytes;
    status.encodedPayloadBytes = snapshot.encodedPayloadBytes;
    status.sentPayloadBytes = snapshot.sentPayloadBytes;
    status.lastSentPayloadBytes = snapshot.lastSentPayloadBytes;
    status.captureErrors = snapshot.captureErrors;
    status.status = snapshot.lastCaptureStatus;
    status.recoveryAction =
        display::decideDisplayCaptureRecovery(status.status).action;
    result.push_back(std::move(status));
    return result;
}

std::vector<DisplayCodecRuntimeStatus> collectDisplayCodecStatuses(
    session::Session& session)
{
    std::vector<DisplayCodecRuntimeStatus> result;
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return result;

    auto* agent = dynamic_cast<modules::display::DisplayAgentModule*>(
        host->module("display.screen.agent"));
    if (agent != nullptr) {
        const modules::display::DisplayAgentSnapshot snapshot =
            agent->snapshot();
        DisplayCodecRuntimeStatus status;
        status.moduleId = "display.screen.agent";
        status.direction = "encode";
        status.codec = snapshot.encoderCodec;
        status.frames = snapshot.encodedFrames;
        status.payloadBytes = snapshot.encodedPayloadBytes;
        status.errors = snapshot.encodeFailures;
        result.push_back(std::move(status));
    }

    auto* client = dynamic_cast<modules::display::DisplayClientModule*>(
        host->module("display.screen.client"));
    if (client != nullptr) {
        const modules::display::DisplayClientSnapshot snapshot =
            client->snapshot();
        DisplayCodecRuntimeStatus status;
        status.moduleId = "display.screen.client";
        status.direction = "decode";
        status.codec = snapshot.decoderCodec;
        status.frames = snapshot.decodedFrames;
        status.payloadBytes = snapshot.decodedPixelBytes;
        status.errors = snapshot.decodeErrors;
        status.pendingFrames = snapshot.decoderPendingFrames;
        status.delayedFrames = snapshot.delayedDecodedFrames;
        result.push_back(std::move(status));
    }

    return result;
}

bool worseThan(DisplayProductHealthLevel lhs, DisplayProductHealthLevel rhs)
{
    return static_cast<int>(lhs) > static_cast<int>(rhs);
}

void raiseHealth(DisplayProductDiagnosticsSnapshot& result,
                 DisplayProductHealthLevel level,
                 std::string message = {})
{
    if (result.health == DisplayProductHealthLevel::Unknown ||
        worseThan(level, result.health)) {
        result.health = level;
    }
    if (!message.empty())
        result.messages.push_back(std::move(message));
}

} // namespace

const char* displayProductHealthLevelName(DisplayProductHealthLevel level)
{
    switch (level) {
    case DisplayProductHealthLevel::Unknown:
        return "unknown";
    case DisplayProductHealthLevel::Ok:
        return "ok";
    case DisplayProductHealthLevel::Warning:
        return "warning";
    case DisplayProductHealthLevel::Degraded:
        return "degraded";
    case DisplayProductHealthLevel::Blocked:
        return "blocked";
    }
    return "unknown";
}

SessionRuntimeDiagnosticsSnapshot buildSessionRuntimeDiagnostics(
    session::Session& session,
    const diagnostics::DiagnosticsSink* diagnostics,
    const SessionRuntimeDiagnosticsOptions& options)
{
    SessionRuntimeDiagnosticsSnapshot result;
    result.sessionId = session.id();
    result.sessionState = session.state();
    result.session = session.snapshot();
    result.linkChannels =
        buildLinkChannelBindingReport(session, options.linkReportOptions);
    result.linkReady = result.linkChannels.ok;
    result.blockedChannels = countBlockedChannels(result.linkChannels);
    result.mountedModules = result.session.moduleSnapshots.size();
    result.runningModules = countRunningModules(result.session);
    result.remoteModuleInventory = result.session.remoteModuleInventory;
    result.remoteModules = result.remoteModuleInventory.manifests.size();
    result.displayCaptureStatuses = collectDisplayCaptureStatuses(session);
    result.displayCodecStatuses = collectDisplayCodecStatuses(session);

    if (!result.linkChannels.messages.empty()) {
        result.messages.insert(result.messages.end(),
                               result.linkChannels.messages.begin(),
                               result.linkChannels.messages.end());
    }

    if (options.includeDiagnosticEvents && diagnostics != nullptr)
        result.diagnostics = diagnostics->eventsForSession(session.id());

    result.ok = result.sessionId != 0 && result.messages.empty();
    return result;
}

SessionRuntimeDiagnosticsSnapshot buildSessionRuntimeDiagnostics(
    RuntimeHost& host,
    protocol::SessionId sessionId,
    const SessionRuntimeDiagnosticsOptions& options)
{
    SessionRuntimeDiagnosticsSnapshot result;
    session::Session* session = host.sessions().find(sessionId);
    if (session == nullptr) {
        result.messages.push_back("session not found");
        return result;
    }

    return buildSessionRuntimeDiagnostics(*session,
                                          &host.diagnostics(),
                                          options);
}

DisplayProductDiagnosticsSnapshot buildDisplayProductDiagnostics(
    const SessionRuntimeDiagnosticsSnapshot& snapshot)
{
    DisplayProductDiagnosticsSnapshot result;
    result.sessionId = snapshot.sessionId;
    result.sessionOk = snapshot.ok;
    result.linkReady = snapshot.linkReady;
    result.blockedChannels = snapshot.blockedChannels;
    result.health = DisplayProductHealthLevel::Ok;
    result.messages = snapshot.messages;

    if (snapshot.sessionId == 0) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Blocked,
                    "session is missing");
    }

    for (const module::ModuleSnapshot& module :
         snapshot.session.moduleSnapshots) {
        if (!startsWith(module.moduleId, "display.screen"))
            continue;
        ++result.displayModules;
        if (module.state == module::ModuleState::Running)
            ++result.runningDisplayModules;
    }

    if (result.displayModules == 0) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Blocked,
                    "display module is not mounted");
    } else if (result.runningDisplayModules == 0) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Blocked,
                    "display module is not running");
    }

    if (!snapshot.linkReady) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Blocked,
                    "display required channel is not ready");
    }

    if (!snapshot.displayCaptureStatuses.empty()) {
        const DisplayCaptureRuntimeStatus& capture =
            snapshot.displayCaptureStatuses.front();
        result.capturePresent = true;
        result.captureBackendId = capture.backendId;
        result.captureStatus = capture.status;
        result.captureRecoveryAction = capture.recoveryAction;
        result.captureErrors = capture.captureErrors;
        if (capture.status.code !=
            modules::display::DisplayCaptureStatusCode::Ok) {
            raiseHealth(result,
                        capture.status.recoverable
                            ? DisplayProductHealthLevel::Degraded
                            : DisplayProductHealthLevel::Blocked,
                        capture.status.message.empty()
                            ? "display capture is not healthy"
                            : capture.status.message);
        }
        if (capture.captureErrors > 0) {
            raiseHealth(result,
                        DisplayProductHealthLevel::Degraded,
                        "display capture reported errors");
        }
    }

    const DisplayCodecRuntimeStatus* primaryCodec = nullptr;
    for (const DisplayCodecRuntimeStatus& codec :
         snapshot.displayCodecStatuses) {
        result.codecErrors += codec.errors;
        result.codecPendingFrames += codec.pendingFrames;
        result.codecDelayedFrames += codec.delayedFrames;
        result.codecPayloadBytes += codec.payloadBytes;
        if (primaryCodec == nullptr || codec.codec.selected)
            primaryCodec = &codec;
    }

    if (primaryCodec != nullptr) {
        result.codecPresent = true;
        result.codecSelected = primaryCodec->codec.selected;
        result.codecAdapterId = primaryCodec->codec.adapterId;
        result.codec = primaryCodec->codec.codec;
        result.codecBackend = primaryCodec->codec.backend;
        result.codecSelectionMode = primaryCodec->codec.selectionMode;
        result.codecFallback = primaryCodec->codec.fallback;
        result.codecDeltaFrames = primaryCodec->codec.deltaFrames;
        result.codecLowLatency = primaryCodec->codec.lowLatency;
        result.codecFallbackReason = primaryCodec->codec.fallbackReason;
        result.messages.insert(result.messages.end(),
                               primaryCodec->codec.messages.begin(),
                               primaryCodec->codec.messages.end());
    }

    if (!result.codecPresent) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Warning,
                    "display codec status is not available");
    } else if (!result.codecSelected) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Blocked,
                    "display codec is not selected");
    } else if (result.codecErrors > 0) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Degraded,
                    "display codec reported errors");
    } else if (result.codecFallback && !result.codecFallbackReason.empty()) {
        raiseHealth(result,
                    DisplayProductHealthLevel::Warning,
                    result.codecFallbackReason);
    }

    if (result.health == DisplayProductHealthLevel::Unknown)
        result.health = DisplayProductHealthLevel::Ok;
    result.usable = result.health == DisplayProductHealthLevel::Ok ||
                    result.health == DisplayProductHealthLevel::Warning ||
                    result.health == DisplayProductHealthLevel::Degraded;
    return result;
}

DisplayProductDiagnosticsSnapshot buildDisplayProductDiagnostics(
    session::Session& session,
    const diagnostics::DiagnosticsSink* diagnostics,
    const SessionRuntimeDiagnosticsOptions& options)
{
    return buildDisplayProductDiagnostics(
        buildSessionRuntimeDiagnostics(session, diagnostics, options));
}

DisplayProductDiagnosticsSnapshot buildDisplayProductDiagnostics(
    RuntimeHost& host,
    protocol::SessionId sessionId,
    const SessionRuntimeDiagnosticsOptions& options)
{
    return buildDisplayProductDiagnostics(
        buildSessionRuntimeDiagnostics(host, sessionId, options));
}

} // namespace runtime
} // namespace fusiondesk
