#include "fusiondesk/runtime/display/display_capture_recovery.h"

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

bool sameBackend(const std::string& left, const std::string& right)
{
    return !left.empty() && left == right;
}

bool recoveryWindowExpired(const DisplayCaptureRecoveryPolicy& policy,
                           const DisplayCaptureRecoveryState& state,
                           std::uint64_t monotonicNowUsec)
{
    if (state.lastRecoveryAttemptUsec == 0)
        return true;
    if (policy.recoveryWindowUsec == 0)
        return false;
    return monotonicNowUsec >
           state.lastRecoveryAttemptUsec + policy.recoveryWindowUsec;
}

} // namespace

const char* displayCaptureRecoveryActionName(
    DisplayCaptureRecoveryAction action)
{
    switch (action) {
    case DisplayCaptureRecoveryAction::None:
        return "none";
    case DisplayCaptureRecoveryAction::RetryNextFrame:
        return "retry_next_frame";
    case DisplayCaptureRecoveryAction::ReopenCapture:
        return "reopen_capture";
    case DisplayCaptureRecoveryAction::RecreateCapture:
        return "recreate_capture";
    case DisplayCaptureRecoveryAction::SwitchBackend:
        return "switch_backend";
    case DisplayCaptureRecoveryAction::WaitForPermission:
        return "wait_for_permission";
    case DisplayCaptureRecoveryAction::StopCapture:
        return "stop_capture";
    }
    return "none";
}

DisplayCaptureRecoveryDecision decideDisplayCaptureRecovery(
    const modules::display::DisplayCaptureStatus& status)
{
    using modules::display::DisplayCaptureStatusCode;

    DisplayCaptureRecoveryDecision decision;
    switch (status.code) {
    case DisplayCaptureStatusCode::Ok:
    case DisplayCaptureStatusCode::Unknown:
        return decision;
    case DisplayCaptureStatusCode::FrameTimeout:
        decision.action = DisplayCaptureRecoveryAction::RetryNextFrame;
        return decision;
    case DisplayCaptureStatusCode::NotOpen:
    case DisplayCaptureStatusCode::SourceNotFound:
    case DisplayCaptureStatusCode::SourceHotplug:
    case DisplayCaptureStatusCode::GeometryOrFormatChanged:
        decision.action = status.recoverable ?
            DisplayCaptureRecoveryAction::ReopenCapture :
            DisplayCaptureRecoveryAction::SwitchBackend;
        decision.requestKeyframe = status.recoverable;
        return decision;
    case DisplayCaptureStatusCode::DeviceLost:
        decision.action = status.recoverable ?
            DisplayCaptureRecoveryAction::RecreateCapture :
            DisplayCaptureRecoveryAction::SwitchBackend;
        decision.requestKeyframe = status.recoverable;
        return decision;
    case DisplayCaptureStatusCode::AccessDenied:
    case DisplayCaptureStatusCode::PermissionRevoked:
    case DisplayCaptureStatusCode::ProtectedContent:
        decision.action = DisplayCaptureRecoveryAction::WaitForPermission;
        decision.keepSessionRunning = true;
        return decision;
    case DisplayCaptureStatusCode::SessionModeUnsupported:
        decision.action = status.recoverable ?
            DisplayCaptureRecoveryAction::ReopenCapture :
            DisplayCaptureRecoveryAction::SwitchBackend;
        decision.requestKeyframe = status.recoverable;
        return decision;
    case DisplayCaptureStatusCode::InvalidFrame:
    case DisplayCaptureStatusCode::SystemCallFailed:
        decision.action = status.recoverable ?
            DisplayCaptureRecoveryAction::ReopenCapture :
            DisplayCaptureRecoveryAction::SwitchBackend;
        decision.requestKeyframe = status.recoverable;
        return decision;
    case DisplayCaptureStatusCode::Unsupported:
        decision.action = DisplayCaptureRecoveryAction::SwitchBackend;
        decision.keepSessionRunning = true;
        return decision;
    }

    return decision;
}

bool isExecutableDisplayCaptureRecoveryAction(DisplayCaptureRecoveryAction action)
{
    return action == DisplayCaptureRecoveryAction::ReopenCapture ||
           action == DisplayCaptureRecoveryAction::RecreateCapture ||
           action == DisplayCaptureRecoveryAction::SwitchBackend;
}

DisplayCaptureRecoveryPlan selectDisplayCaptureRecoveryPlan(
    const DisplayCaptureRecoveryPolicy& policy,
    const DisplayCaptureRecoveryState& state,
    const modules::display::DisplayCaptureStatus& status,
    const std::string& backendId,
    std::uint64_t monotonicNowUsec)
{
    DisplayCaptureRecoveryPlan plan;
    plan.decision = decideDisplayCaptureRecovery(status);
    plan.baseAction = plan.decision.action;
    plan.nextState = state;
    plan.nextState.backendId = backendId;

    if (status.code == modules::display::DisplayCaptureStatusCode::Ok) {
        plan.nextState =
            recordDisplayCaptureFrameSuccess(policy,
                                             state,
                                             backendId,
                                             monotonicNowUsec);
        return plan;
    }

    if (!sameBackend(state.backendId, backendId)) {
        plan.nextState.consecutiveFailures = 1;
        plan.nextState.sameBackendRecoveryAttempts = 0;
        plan.nextState.lastRecoveryAttemptUsec = 0;
        plan.nextState.cooldownUntilUsec = 0;
    } else {
        ++plan.nextState.consecutiveFailures;
        if (recoveryWindowExpired(policy, state, monotonicNowUsec))
            plan.nextState.sameBackendRecoveryAttempts = 0;
    }

    if (isExecutableDisplayCaptureRecoveryAction(plan.decision.action) &&
        state.cooldownUntilUsec > monotonicNowUsec) {
        plan.blockedByCooldown = true;
        plan.cooldownRemainingUsec =
            state.cooldownUntilUsec - monotonicNowUsec;
        plan.nextState.cooldownUntilUsec = state.cooldownUntilUsec;
        plan.decision.action = DisplayCaptureRecoveryAction::RetryNextFrame;
        plan.decision.requestKeyframe = false;
        return plan;
    }

    const bool canPromote =
        policy.promoteRepeatedSameBackendRecoveryToSwitch &&
        policy.sameBackendRecoveryLimit > 0 &&
        (plan.baseAction == DisplayCaptureRecoveryAction::ReopenCapture ||
         plan.baseAction == DisplayCaptureRecoveryAction::RecreateCapture) &&
        plan.nextState.sameBackendRecoveryAttempts >=
            policy.sameBackendRecoveryLimit;
    if (canPromote) {
        plan.promotedToSwitchBackend = true;
        plan.decision.action = DisplayCaptureRecoveryAction::SwitchBackend;
        plan.decision.requestKeyframe = false;
    }

    return plan;
}

DisplayCaptureRecoveryState recordDisplayCaptureRecoveryAttempt(
    const DisplayCaptureRecoveryPolicy& policy,
    const DisplayCaptureRecoveryPlan& plan,
    bool success,
    const std::string& recoveredBackendId,
    std::uint64_t monotonicNowUsec)
{
    DisplayCaptureRecoveryState next = plan.nextState;
    next.lastRecoveryAttemptUsec = monotonicNowUsec;
    next.backendId = recoveredBackendId.empty() ? next.backendId : recoveredBackendId;

    if (plan.decision.action == DisplayCaptureRecoveryAction::SwitchBackend ||
        !sameBackend(plan.nextState.backendId, next.backendId)) {
        next.sameBackendRecoveryAttempts = 0;
    } else if (plan.decision.action == DisplayCaptureRecoveryAction::ReopenCapture ||
               plan.decision.action == DisplayCaptureRecoveryAction::RecreateCapture) {
        ++next.sameBackendRecoveryAttempts;
    }

    if (success) {
        next.cooldownUntilUsec = 0;
    } else {
        next.cooldownUntilUsec =
            monotonicNowUsec + policy.failedRecoveryCooldownUsec;
    }

    return next;
}

DisplayCaptureRecoveryState recordDisplayCaptureFrameSuccess(
    const DisplayCaptureRecoveryPolicy& policy,
    const DisplayCaptureRecoveryState& state,
    const std::string& backendId,
    std::uint64_t monotonicNowUsec)
{
    DisplayCaptureRecoveryState next = state;
    if (!sameBackend(state.backendId, backendId) ||
        recoveryWindowExpired(policy, state, monotonicNowUsec)) {
        next.sameBackendRecoveryAttempts = 0;
        next.lastRecoveryAttemptUsec = 0;
    }
    next.backendId = backendId;
    next.consecutiveFailures = 0;
    next.cooldownUntilUsec = 0;
    return next;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
