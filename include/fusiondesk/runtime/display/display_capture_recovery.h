#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_RECOVERY_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_RECOVERY_H

#include <cstdint>
#include <string>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace runtime {
namespace display {

enum class DisplayCaptureRecoveryAction : std::uint16_t
{
    None = 0,
    RetryNextFrame = 1,
    ReopenCapture = 2,
    RecreateCapture = 3,
    SwitchBackend = 4,
    WaitForPermission = 5,
    StopCapture = 6
};

struct DisplayCaptureRecoveryDecision
{
    DisplayCaptureRecoveryAction action = DisplayCaptureRecoveryAction::None;
    bool requestKeyframe = false;
    bool keepSessionRunning = true;
};

struct DisplayCaptureRecoveryPolicy
{
    std::uint32_t sameBackendRecoveryLimit = 2;
    std::uint64_t recoveryWindowUsec = 5000000;
    std::uint64_t failedRecoveryCooldownUsec = 250000;
    bool promoteRepeatedSameBackendRecoveryToSwitch = true;
};

struct DisplayCaptureRecoveryState
{
    std::string backendId;
    std::uint32_t consecutiveFailures = 0;
    std::uint32_t sameBackendRecoveryAttempts = 0;
    std::uint64_t lastRecoveryAttemptUsec = 0;
    std::uint64_t cooldownUntilUsec = 0;
};

struct DisplayCaptureRecoveryPlan
{
    DisplayCaptureRecoveryDecision decision;
    DisplayCaptureRecoveryAction baseAction = DisplayCaptureRecoveryAction::None;
    DisplayCaptureRecoveryState nextState;
    bool blockedByCooldown = false;
    bool promotedToSwitchBackend = false;
    std::uint64_t cooldownRemainingUsec = 0;
};

const char* displayCaptureRecoveryActionName(
    DisplayCaptureRecoveryAction action);

DisplayCaptureRecoveryDecision decideDisplayCaptureRecovery(
    const modules::display::DisplayCaptureStatus& status);

bool isExecutableDisplayCaptureRecoveryAction(DisplayCaptureRecoveryAction action);

DisplayCaptureRecoveryPlan selectDisplayCaptureRecoveryPlan(
    const DisplayCaptureRecoveryPolicy& policy,
    const DisplayCaptureRecoveryState& state,
    const modules::display::DisplayCaptureStatus& status,
    const std::string& backendId,
    std::uint64_t monotonicNowUsec);

DisplayCaptureRecoveryState recordDisplayCaptureRecoveryAttempt(
    const DisplayCaptureRecoveryPolicy& policy,
    const DisplayCaptureRecoveryPlan& plan,
    bool success,
    const std::string& recoveredBackendId,
    std::uint64_t monotonicNowUsec);

DisplayCaptureRecoveryState recordDisplayCaptureFrameSuccess(
    const DisplayCaptureRecoveryPolicy& policy,
    const DisplayCaptureRecoveryState& state,
    const std::string& backendId,
    std::uint64_t monotonicNowUsec);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_RECOVERY_H
