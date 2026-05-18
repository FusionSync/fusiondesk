#ifndef FUSIONDESK_RUNTIME_SESSION_SESSION_RUNTIME_DIAGNOSTICS_H
#define FUSIONDESK_RUNTIME_SESSION_SESSION_RUNTIME_DIAGNOSTICS_H

#include <cstddef>
#include <string>
#include <vector>

#include "fusiondesk/core/diagnostics/diagnostics_sink.h"
#include "fusiondesk/modules/display/display_types.h"
#include "fusiondesk/runtime/display/display_capture_recovery.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/session/link_channel_binding_report.h"

namespace fusiondesk {
namespace runtime {

struct SessionRuntimeDiagnosticsOptions
{
    LinkChannelBindingReportOptions linkReportOptions;
    bool includeDiagnosticEvents = true;
};

struct DisplayCaptureRuntimeStatus
{
    std::string moduleId;
    std::string backendId;
    bool includeCursor = true;
    int capturedFrames = 0;
    int sentFrames = 0;
    std::uint64_t capturedPixelBytes = 0;
    std::uint64_t encodedPayloadBytes = 0;
    std::uint64_t sentPayloadBytes = 0;
    std::uint64_t lastSentPayloadBytes = 0;
    int captureErrors = 0;
    modules::display::DisplayCaptureStatus status;
    display::DisplayCaptureRecoveryAction recoveryAction =
        display::DisplayCaptureRecoveryAction::None;
};

struct DisplayCodecRuntimeStatus
{
    std::string moduleId;
    std::string direction;
    modules::display::DisplayCodecRuntimeInfo codec;
    int frames = 0;
    std::uint64_t payloadBytes = 0;
    int errors = 0;
    int pendingFrames = 0;
    int delayedFrames = 0;
};

enum class DisplayProductHealthLevel
{
    Unknown,
    Ok,
    Warning,
    Degraded,
    Blocked
};

struct DisplayProductDiagnosticsSnapshot
{
    bool usable = false;
    DisplayProductHealthLevel health = DisplayProductHealthLevel::Unknown;
    protocol::SessionId sessionId = 0;
    bool sessionOk = false;
    bool linkReady = false;
    std::size_t blockedChannels = 0;
    std::size_t displayModules = 0;
    std::size_t runningDisplayModules = 0;
    bool capturePresent = false;
    std::string captureBackendId;
    modules::display::DisplayCaptureStatus captureStatus;
    display::DisplayCaptureRecoveryAction captureRecoveryAction =
        display::DisplayCaptureRecoveryAction::None;
    int captureErrors = 0;
    bool codecPresent = false;
    bool codecSelected = false;
    std::string codecAdapterId;
    std::string codec;
    std::string codecBackend;
    std::string codecSelectionMode;
    bool codecFallback = false;
    bool codecDeltaFrames = false;
    bool codecLowLatency = false;
    std::string codecFallbackReason;
    int codecErrors = 0;
    int codecPendingFrames = 0;
    int codecDelayedFrames = 0;
    std::uint64_t codecPayloadBytes = 0;
    std::vector<std::string> messages;
};

struct SessionRuntimeDiagnosticsSnapshot
{
    bool ok = false;
    protocol::SessionId sessionId = 0;
    session::SessionState sessionState = session::SessionState::Created;
    bool linkReady = false;
    std::size_t blockedChannels = 0;
    std::size_t mountedModules = 0;
    std::size_t runningModules = 0;
    std::size_t remoteModules = 0;
    session::SessionSnapshot session;
    session::RemoteModuleInventorySnapshot remoteModuleInventory;
    LinkChannelBindingReport linkChannels;
    std::vector<DisplayCaptureRuntimeStatus> displayCaptureStatuses;
    std::vector<DisplayCodecRuntimeStatus> displayCodecStatuses;
    std::vector<diagnostics::DiagnosticEvent> diagnostics;
    std::vector<std::string> messages;
};

const char* displayProductHealthLevelName(DisplayProductHealthLevel level);

SessionRuntimeDiagnosticsSnapshot buildSessionRuntimeDiagnostics(
    session::Session& session,
    const diagnostics::DiagnosticsSink* diagnostics,
    const SessionRuntimeDiagnosticsOptions& options = {});

SessionRuntimeDiagnosticsSnapshot buildSessionRuntimeDiagnostics(
    RuntimeHost& host,
    protocol::SessionId sessionId,
    const SessionRuntimeDiagnosticsOptions& options = {});

DisplayProductDiagnosticsSnapshot buildDisplayProductDiagnostics(
    const SessionRuntimeDiagnosticsSnapshot& snapshot);

DisplayProductDiagnosticsSnapshot buildDisplayProductDiagnostics(
    session::Session& session,
    const diagnostics::DiagnosticsSink* diagnostics,
    const SessionRuntimeDiagnosticsOptions& options = {});

DisplayProductDiagnosticsSnapshot buildDisplayProductDiagnostics(
    RuntimeHost& host,
    protocol::SessionId sessionId,
    const SessionRuntimeDiagnosticsOptions& options = {});

} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_SESSION_SESSION_RUNTIME_DIAGNOSTICS_H
