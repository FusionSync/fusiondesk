#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_RUNTIME_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_types.h"
#include "fusiondesk/runtime/display/display_capture_backend_failover.h"
#include "fusiondesk/runtime/display/display_capture_recovery.h"

namespace fusiondesk {
namespace session {
class Session;
} // namespace session

namespace runtime {
namespace display {

struct DisplayRuntimeServiceOptions
{
    session::Session* session = nullptr;
    std::uint32_t targetFps = 15;
    bool pumpAgentFrames = true;
    bool monitorClientFirstFrame = true;
    bool monitorClientFreshFrameAfterReconnect = true;
    std::uint64_t firstFrameTimeoutUsec = 3000000;
    const IDisplayCaptureBackendFactory* captureBackendFactory = nullptr;
    DisplayCaptureBackendSelectionRequest captureBackendSelectionRequest;
    bool allowCaptureBackendSwitch = true;
    bool overrideRequestedCaptureBackendOnSwitch = false;
    DisplayCaptureRecoveryPolicy captureRecoveryPolicy;
};

struct DisplayRuntimeServiceStartResult
{
    bool ok = false;
    std::vector<std::string> messages;
};

struct DisplayRuntimePumpResult
{
    bool active = false;
    bool skippedByFrameRate = false;
    bool frameAttempted = false;
    bool frameSent = false;
    int droppedFrames = 0;
    int missingAgentModules = 0;
    int missingClientModules = 0;
    int sendFailures = 0;
    int encodeFailures = 0;
    std::uint64_t sentPayloadBytes = 0;
    bool firstFrameTimedOut = false;
    bool reconnectFrameTimedOut = false;
    bool keyframeRequestSent = false;
    int keyframeRequestFailures = 0;
    int keyframeRequestTimeouts = 0;
    DisplayCaptureRecoveryAction captureRecoveryAction =
        DisplayCaptureRecoveryAction::None;
    bool captureRecoveryBlockedByCooldown = false;
    bool captureRecoveryPromotedToSwitchBackend = false;
    std::uint64_t captureRecoveryCooldownRemainingUsec = 0;
    bool captureRecoveryAttempted = false;
    bool captureRecoverySucceeded = false;
    int captureRecoveryFramesSent = 0;
};

struct DisplayRuntimeServiceSnapshot
{
    bool active = false;
    bool pumpAgentFrames = false;
    std::uint32_t targetFps = 0;
    std::uint64_t pumpCount = 0;
    std::uint64_t skippedByFrameRate = 0;
    std::uint64_t frameAttempts = 0;
    std::uint64_t lastPumpUsec = 0;
    std::uint64_t lastFrameAttemptUsec = 0;
    std::uint64_t firstFrameSentUsec = 0;
    std::uint64_t lastFrameSentUsec = 0;
    std::uint64_t lastFrameAgeUsec = 0;
    std::uint32_t effectiveFpsX1000 = 0;
    std::uint32_t consecutiveFrameMisses = 0;
    int framesSent = 0;
    std::uint64_t capturedPixelBytes = 0;
    std::uint64_t encodedPayloadBytes = 0;
    std::uint64_t sentPayloadBytes = 0;
    std::uint64_t lastCapturedPixelBytes = 0;
    std::uint64_t lastEncodedPayloadBytes = 0;
    std::uint64_t lastSentPayloadBytes = 0;
    std::uint32_t effectiveBitrateKbps = 0;
    int droppedFrames = 0;
    int sendFailures = 0;
    int captureErrors = 0;
    int missingAgentModules = 0;
    int missingClientModules = 0;
    int firstFrameTimeouts = 0;
    int reconnectFrameTimeouts = 0;
    int keyframeRequestsSent = 0;
    int keyframeRequestFailures = 0;
    int keyframeRequestTimeouts = 0;
    int captureRecoveryAttempts = 0;
    int captureRecoverySuccesses = 0;
    int captureRecoveryFailures = 0;
    int captureRecoveryCooldownBlocks = 0;
    int captureRecoverySwitchPromotions = 0;
    std::uint32_t captureRecoveryConsecutiveFailures = 0;
    std::uint32_t captureRecoverySameBackendAttempts = 0;
    std::uint64_t captureRecoveryCooldownRemainingUsec = 0;
    std::string captureBackendId;
    modules::display::DisplayCaptureStatus lastCaptureStatus;
    DisplayCaptureRecoveryAction lastCaptureRecoveryAction =
        DisplayCaptureRecoveryAction::None;
};

class DisplayRuntimeService
{
public:
    explicit DisplayRuntimeService(DisplayRuntimeServiceOptions options);

    DisplayRuntimeServiceStartResult start();
    void stop();

    DisplayRuntimePumpResult pumpOnce();
    DisplayRuntimePumpResult pumpOnce(std::uint64_t monotonicNowUsec);
    DisplayRuntimeServiceSnapshot snapshot() const;

private:
    std::uint64_t frameIntervalUsec() const;

private:
    DisplayRuntimeServiceOptions options_;
    DisplayRuntimeServiceSnapshot snapshot_;
    DisplayCaptureRecoveryState captureRecoveryState_;
    std::uint64_t lastFramePumpUsec_ = 0;
    std::uint64_t clientFirstFrameWaitStartUsec_ = 0;
    std::uint64_t lastFirstFrameRequestUsec_ = 0;
    std::uint64_t clientReconnectFrameWaitStartUsec_ = 0;
    std::uint64_t lastReconnectFrameRequestUsec_ = 0;
    int lastClientReconnectResumes_ = 0;
    int clientReconnectFrameBaseline_ = 0;
};

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_RUNTIME_SERVICE_H
