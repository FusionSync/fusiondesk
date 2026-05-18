#include <cassert>
#include <string>

#include "fusiondesk/runtime/display/display_capture_recovery.h"

using fusiondesk::modules::display::DisplayCaptureStatus;
using fusiondesk::modules::display::DisplayCaptureStatusCode;
using fusiondesk::runtime::display::DisplayCaptureRecoveryAction;
using fusiondesk::runtime::display::DisplayCaptureRecoveryPlan;
using fusiondesk::runtime::display::DisplayCaptureRecoveryPolicy;
using fusiondesk::runtime::display::DisplayCaptureRecoveryState;
using fusiondesk::runtime::display::decideDisplayCaptureRecovery;
using fusiondesk::runtime::display::displayCaptureRecoveryActionName;
using fusiondesk::runtime::display::recordDisplayCaptureFrameSuccess;
using fusiondesk::runtime::display::recordDisplayCaptureRecoveryAttempt;
using fusiondesk::runtime::display::selectDisplayCaptureRecoveryPlan;

namespace {

DisplayCaptureStatus status(DisplayCaptureStatusCode code, bool recoverable)
{
    DisplayCaptureStatus value;
    value.code = code;
    value.recoverable = recoverable;
    return value;
}

void okNeedsNoRecovery()
{
    const auto decision =
        decideDisplayCaptureRecovery(status(DisplayCaptureStatusCode::Ok, true));
    assert(decision.action == DisplayCaptureRecoveryAction::None);
    assert(!decision.requestKeyframe);
    assert(decision.keepSessionRunning);
}

void timeoutRetriesNextFrame()
{
    const auto decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::FrameTimeout, true));
    assert(decision.action == DisplayCaptureRecoveryAction::RetryNextFrame);
    assert(!decision.requestKeyframe);
    assert(decision.keepSessionRunning);
}

void hotplugAndGeometryReopenCapture()
{
    auto decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::SourceHotplug, true));
    assert(decision.action == DisplayCaptureRecoveryAction::ReopenCapture);
    assert(decision.requestKeyframe);

    decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::GeometryOrFormatChanged, true));
    assert(decision.action == DisplayCaptureRecoveryAction::ReopenCapture);
    assert(decision.requestKeyframe);
}

void deviceLossRecreatesCapture()
{
    const auto decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::DeviceLost, true));
    assert(decision.action == DisplayCaptureRecoveryAction::RecreateCapture);
    assert(decision.requestKeyframe);
}

void permissionFailuresWaitWithoutKillingSession()
{
    const auto decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::AccessDenied, false));
    assert(decision.action == DisplayCaptureRecoveryAction::WaitForPermission);
    assert(!decision.requestKeyframe);
    assert(decision.keepSessionRunning);
}

void unrecoverableSystemFailuresSwitchBackend()
{
    auto decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::SystemCallFailed, false));
    assert(decision.action == DisplayCaptureRecoveryAction::SwitchBackend);
    assert(!decision.requestKeyframe);

    decision = decideDisplayCaptureRecovery(
        status(DisplayCaptureStatusCode::Unsupported, false));
    assert(decision.action == DisplayCaptureRecoveryAction::SwitchBackend);
}

void actionNamesAreStable()
{
    assert(std::string(displayCaptureRecoveryActionName(
               DisplayCaptureRecoveryAction::ReopenCapture)) ==
           "reopen_capture");
    assert(std::string(displayCaptureRecoveryActionName(
               DisplayCaptureRecoveryAction::SwitchBackend)) ==
           "switch_backend");
}

void repeatedRecoverableFailuresPromoteToSwitchBackend()
{
    DisplayCaptureRecoveryPolicy policy;
    policy.sameBackendRecoveryLimit = 1;
    policy.recoveryWindowUsec = 1000000;

    DisplayCaptureRecoveryState state;
    DisplayCaptureRecoveryPlan plan = selectDisplayCaptureRecoveryPlan(
        policy,
        state,
        status(DisplayCaptureStatusCode::SourceHotplug, true),
        "windows.dxgi.desktop_duplication",
        1000);
    assert(plan.decision.action == DisplayCaptureRecoveryAction::ReopenCapture);
    assert(!plan.promotedToSwitchBackend);

    state = recordDisplayCaptureRecoveryAttempt(policy,
                                                plan,
                                                true,
                                                "windows.dxgi.desktop_duplication",
                                                1000);
    assert(state.sameBackendRecoveryAttempts == 1);

    plan = selectDisplayCaptureRecoveryPlan(
        policy,
        state,
        status(DisplayCaptureStatusCode::SourceHotplug, true),
        "windows.dxgi.desktop_duplication",
        2000);
    assert(plan.baseAction == DisplayCaptureRecoveryAction::ReopenCapture);
    assert(plan.decision.action == DisplayCaptureRecoveryAction::SwitchBackend);
    assert(plan.promotedToSwitchBackend);
}

void failedRecoveryStartsCooldown()
{
    DisplayCaptureRecoveryPolicy policy;
    policy.failedRecoveryCooldownUsec = 500;

    DisplayCaptureRecoveryState state;
    DisplayCaptureRecoveryPlan plan = selectDisplayCaptureRecoveryPlan(
        policy,
        state,
        status(DisplayCaptureStatusCode::SourceHotplug, true),
        "windows.dxgi.desktop_duplication",
        1000);
    assert(plan.decision.action == DisplayCaptureRecoveryAction::ReopenCapture);

    state = recordDisplayCaptureRecoveryAttempt(policy,
                                                plan,
                                                false,
                                                "windows.dxgi.desktop_duplication",
                                                1000);
    assert(state.cooldownUntilUsec == 1500);

    plan = selectDisplayCaptureRecoveryPlan(
        policy,
        state,
        status(DisplayCaptureStatusCode::SourceHotplug, true),
        "windows.dxgi.desktop_duplication",
        1200);
    assert(plan.blockedByCooldown);
    assert(plan.cooldownRemainingUsec == 300);
    assert(plan.decision.action == DisplayCaptureRecoveryAction::RetryNextFrame);

    state = plan.nextState;
    plan = selectDisplayCaptureRecoveryPlan(
        policy,
        state,
        status(DisplayCaptureStatusCode::SourceHotplug, true),
        "windows.dxgi.desktop_duplication",
        1500);
    assert(!plan.blockedByCooldown);
    assert(plan.decision.action == DisplayCaptureRecoveryAction::ReopenCapture);
}

void frameSuccessClearsCooldownAndFailureCount()
{
    DisplayCaptureRecoveryPolicy policy;
    policy.failedRecoveryCooldownUsec = 500;

    DisplayCaptureRecoveryState state;
    DisplayCaptureRecoveryPlan plan = selectDisplayCaptureRecoveryPlan(
        policy,
        state,
        status(DisplayCaptureStatusCode::SourceHotplug, true),
        "windows.dxgi.desktop_duplication",
        1000);
    state = recordDisplayCaptureRecoveryAttempt(policy,
                                                plan,
                                                false,
                                                "windows.dxgi.desktop_duplication",
                                                1000);

    state = recordDisplayCaptureFrameSuccess(policy,
                                             state,
                                             "windows.dxgi.desktop_duplication",
                                             1100);
    assert(state.consecutiveFailures == 0);
    assert(state.cooldownUntilUsec == 0);
}

} // namespace

int main()
{
    okNeedsNoRecovery();
    timeoutRetriesNextFrame();
    hotplugAndGeometryReopenCapture();
    deviceLossRecreatesCapture();
    permissionFailuresWaitWithoutKillingSession();
    unrecoverableSystemFailuresSwitchBackend();
    actionNamesAreStable();
    repeatedRecoverableFailuresPromoteToSwitchBackend();
    failedRecoveryStartsCooldown();
    frameSuccessClearsCooldownAndFailureCount();
    return 0;
}
