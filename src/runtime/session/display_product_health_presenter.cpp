#include "fusiondesk/runtime/session/display_product_health_presenter.h"

#include "fusiondesk/modules/display/display_types.h"
#include "fusiondesk/runtime/display/display_capture_recovery.h"

namespace fusiondesk {
namespace runtime {
namespace {

const char* captureStatusToken(
    modules::display::DisplayCaptureStatusCode code)
{
    switch (code) {
    case modules::display::DisplayCaptureStatusCode::Ok:
        return "ok";
    case modules::display::DisplayCaptureStatusCode::Unknown:
        return "unknown";
    case modules::display::DisplayCaptureStatusCode::NotOpen:
        return "not_open";
    case modules::display::DisplayCaptureStatusCode::AccessDenied:
        return "access_denied";
    case modules::display::DisplayCaptureStatusCode::PermissionRevoked:
        return "permission_revoked";
    case modules::display::DisplayCaptureStatusCode::ProtectedContent:
        return "protected_content";
    case modules::display::DisplayCaptureStatusCode::SourceNotFound:
        return "source_not_found";
    case modules::display::DisplayCaptureStatusCode::SourceHotplug:
        return "source_hotplug";
    case modules::display::DisplayCaptureStatusCode::GeometryOrFormatChanged:
        return "geometry_or_format_changed";
    case modules::display::DisplayCaptureStatusCode::DeviceLost:
        return "device_lost";
    case modules::display::DisplayCaptureStatusCode::SessionModeUnsupported:
        return "session_mode_unsupported";
    case modules::display::DisplayCaptureStatusCode::FrameTimeout:
        return "frame_timeout";
    case modules::display::DisplayCaptureStatusCode::InvalidFrame:
        return "invalid_frame";
    case modules::display::DisplayCaptureStatusCode::SystemCallFailed:
        return "system_call_failed";
    case modules::display::DisplayCaptureStatusCode::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

std::string captureState(const DisplayProductDiagnosticsSnapshot& snapshot)
{
    if (!snapshot.capturePresent)
        return "capture.none";

    std::string state = "capture.";
    state += captureStatusToken(snapshot.captureStatus.code);
    if (!snapshot.captureBackendId.empty()) {
        state += ".";
        state += snapshot.captureBackendId;
    }
    return state;
}

std::string codecState(const DisplayProductDiagnosticsSnapshot& snapshot)
{
    if (!snapshot.codecPresent)
        return "codec.none";

    std::string state = snapshot.codecSelected ? "codec.selected" :
                                                 "codec.not_selected";
    if (!snapshot.codecAdapterId.empty()) {
        state += ".";
        state += snapshot.codecAdapterId;
    }
    if (!snapshot.codecSelectionMode.empty()) {
        state += ".";
        state += snapshot.codecSelectionMode;
    }
    return state;
}

std::string statusCodeFor(const DisplayProductDiagnosticsSnapshot& snapshot)
{
    if (!snapshot.sessionOk || snapshot.sessionId == 0)
        return "display.session_blocked";
    if (!snapshot.linkReady || snapshot.blockedChannels > 0)
        return "display.channel_blocked";
    if (snapshot.displayModules == 0)
        return "display.module_missing";
    if (snapshot.runningDisplayModules == 0)
        return "display.module_stopped";
    if (snapshot.capturePresent &&
        snapshot.captureStatus.code !=
            modules::display::DisplayCaptureStatusCode::Ok) {
        return snapshot.captureStatus.recoverable ?
                   "display.capture_degraded" :
                   "display.capture_blocked";
    }
    if (snapshot.captureErrors > 0)
        return "display.capture_degraded";
    if (snapshot.codecPresent && !snapshot.codecSelected)
        return "display.codec_blocked";
    if (snapshot.codecErrors > 0)
        return "display.codec_degraded";
    if (snapshot.codecFallback && !snapshot.codecFallbackReason.empty())
        return "display.codec_fallback";
    if (snapshot.health == DisplayProductHealthLevel::Degraded)
        return "display.degraded";
    if (snapshot.health == DisplayProductHealthLevel::Warning)
        return "display.warning";
    if (snapshot.health == DisplayProductHealthLevel::Blocked)
        return "display.blocked";
    if (snapshot.usable)
        return "display.ready";
    return "display.unknown";
}

std::string actionCodeFor(const DisplayProductDiagnosticsSnapshot& snapshot,
                          const std::string& statusCode)
{
    if (statusCode == "display.session_blocked")
        return "session.create_or_restore";
    if (statusCode == "display.channel_blocked")
        return "network.bind_required_channels";
    if (statusCode == "display.module_missing")
        return "module.mount_display";
    if (statusCode == "display.module_stopped")
        return "module.start_display";
    if (statusCode == "display.capture_blocked" ||
        statusCode == "display.capture_degraded")
        return (snapshot.captureStatus.recoverable ||
                snapshot.captureErrors > 0) ?
                   "capture.recover_or_failover" :
                   "capture.recreate_with_supported_backend";
    if (statusCode == "display.codec_blocked")
        return "codec.select_supported_adapter";
    if (statusCode == "display.codec_degraded")
        return "codec.restart_or_fallback";
    if (statusCode == "display.codec_fallback")
        return "codec.review_product_policy";
    if (statusCode == "display.warning" ||
        statusCode == "display.degraded")
        return "display.inspect_diagnostics";
    return "none";
}

} // namespace

DisplayProductHealthPresentation buildDisplayProductHealthPresentation(
    const DisplayProductDiagnosticsSnapshot& snapshot)
{
    DisplayProductHealthPresentation presentation;
    presentation.usable = snapshot.usable;
    presentation.health = snapshot.health;
    presentation.healthName = displayProductHealthLevelName(snapshot.health);
    presentation.statusCode = statusCodeFor(snapshot);
    presentation.primaryActionCode =
        actionCodeFor(snapshot, presentation.statusCode);
    presentation.captureState = captureState(snapshot);
    presentation.codecState = codecState(snapshot);
    presentation.showCodecFallbackWarning =
        snapshot.codecFallback && !snapshot.codecFallbackReason.empty();
    presentation.showCodecLatencyWarning =
        snapshot.codecPendingFrames > 0 || snapshot.codecDelayedFrames > 0;
    presentation.showCaptureRecoveryWarning =
        snapshot.captureRecoveryAction !=
        display::DisplayCaptureRecoveryAction::None;
    presentation.detailMessages = snapshot.messages;
    return presentation;
}

DisplayProductHealthPresentation buildDisplayProductHealthPresentation(
    const SessionRuntimeDiagnosticsSnapshot& snapshot)
{
    return buildDisplayProductHealthPresentation(
        buildDisplayProductDiagnostics(snapshot));
}

} // namespace runtime
} // namespace fusiondesk
