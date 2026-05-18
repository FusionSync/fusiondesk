#include "fusiondesk/runtime/display/display_runtime_service.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/display/display_modules.h"

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

modules::display::DisplayAgentModule* agentModule(session::Session* session)
{
    if (session == nullptr || session->moduleHost() == nullptr)
        return nullptr;

    return dynamic_cast<modules::display::DisplayAgentModule*>(
        session->moduleHost()->module("display.screen.agent"));
}

modules::display::DisplayClientModule* clientModule(session::Session* session)
{
    if (session == nullptr || session->moduleHost() == nullptr)
        return nullptr;

    return dynamic_cast<modules::display::DisplayClientModule*>(
        session->moduleHost()->module("display.screen.client"));
}

bool shouldExecuteCaptureRecovery(DisplayCaptureRecoveryAction action)
{
    return action == DisplayCaptureRecoveryAction::ReopenCapture ||
           action == DisplayCaptureRecoveryAction::RecreateCapture;
}

bool shouldExecuteCaptureBackendSwitch(
    DisplayCaptureRecoveryAction action,
    const DisplayRuntimeServiceOptions& options)
{
    return action == DisplayCaptureRecoveryAction::SwitchBackend &&
           options.allowCaptureBackendSwitch &&
           options.captureBackendFactory != nullptr;
}

void updateCaptureRecoverySnapshot(DisplayRuntimeServiceSnapshot& snapshot,
                                   const DisplayCaptureRecoveryState& state,
                                   std::uint64_t monotonicNow)
{
    snapshot.captureRecoveryConsecutiveFailures =
        state.consecutiveFailures;
    snapshot.captureRecoverySameBackendAttempts =
        state.sameBackendRecoveryAttempts;
    snapshot.captureRecoveryCooldownRemainingUsec =
        state.cooldownUntilUsec > monotonicNow ?
        state.cooldownUntilUsec - monotonicNow :
        0;
}

void recordClientKeyframeRequestResult(DisplayRuntimePumpResult& result,
                                       DisplayRuntimeServiceSnapshot& snapshot,
                                       bool sent)
{
    result.keyframeRequestSent = sent;
    if (sent) {
        ++snapshot.keyframeRequestsSent;
    } else {
        ++result.keyframeRequestFailures;
        ++snapshot.keyframeRequestFailures;
    }
}

void updateFrameAge(DisplayRuntimeServiceSnapshot& snapshot,
                    std::uint64_t monotonicNow)
{
    if (snapshot.lastFrameSentUsec == 0) {
        snapshot.lastFrameAgeUsec = 0;
        return;
    }
    snapshot.lastFrameAgeUsec =
        monotonicNow > snapshot.lastFrameSentUsec
            ? monotonicNow - snapshot.lastFrameSentUsec
            : 0;
}

void recordFrameDelivered(DisplayRuntimeServiceSnapshot& snapshot,
                          std::uint64_t monotonicNow)
{
    if (snapshot.firstFrameSentUsec == 0)
        snapshot.firstFrameSentUsec = monotonicNow;
    snapshot.lastFrameSentUsec = monotonicNow;
    snapshot.lastFrameAgeUsec = 0;
    snapshot.consecutiveFrameMisses = 0;

    if (snapshot.framesSent > 1 &&
        snapshot.lastFrameSentUsec > snapshot.firstFrameSentUsec) {
        const std::uint64_t elapsed =
            snapshot.lastFrameSentUsec - snapshot.firstFrameSentUsec;
        const std::uint64_t fpsX1000 =
            static_cast<std::uint64_t>(snapshot.framesSent - 1) *
            1000000000ULL / elapsed;
        snapshot.effectiveFpsX1000 = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                fpsX1000,
                std::numeric_limits<std::uint32_t>::max()));
        const std::uint64_t bitrateKbps =
            snapshot.sentPayloadBytes * 8ULL * 1000ULL / elapsed;
        snapshot.effectiveBitrateKbps = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                bitrateKbps,
                std::numeric_limits<std::uint32_t>::max()));
    }
}

void recordFrameMiss(DisplayRuntimeServiceSnapshot& snapshot,
                     std::uint64_t monotonicNow)
{
    if (snapshot.consecutiveFrameMisses <
        std::numeric_limits<std::uint32_t>::max())
        ++snapshot.consecutiveFrameMisses;
    updateFrameAge(snapshot, monotonicNow);
}

std::uint64_t positiveDelta(std::uint64_t after, std::uint64_t before)
{
    return after > before ? after - before : 0;
}

void recordAgentThroughputDelta(
    DisplayRuntimeServiceSnapshot& snapshot,
    const modules::display::DisplayAgentSnapshot& before,
    const modules::display::DisplayAgentSnapshot& after)
{
    snapshot.capturedPixelBytes += positiveDelta(after.capturedPixelBytes,
                                                 before.capturedPixelBytes);
    snapshot.encodedPayloadBytes += positiveDelta(after.encodedPayloadBytes,
                                                  before.encodedPayloadBytes);
    snapshot.sentPayloadBytes += positiveDelta(after.sentPayloadBytes,
                                               before.sentPayloadBytes);
    snapshot.lastCapturedPixelBytes = after.lastCapturedPixelBytes;
    snapshot.lastEncodedPayloadBytes = after.lastEncodedPayloadBytes;
    snapshot.lastSentPayloadBytes = after.lastSentPayloadBytes;
}

} // namespace

DisplayRuntimeService::DisplayRuntimeService(DisplayRuntimeServiceOptions options)
    : options_(std::move(options))
{
    if (options_.targetFps == 0)
        options_.targetFps = 15;
    if (options_.targetFps > 120)
        options_.targetFps = 120;
    if (options_.firstFrameTimeoutUsec == 0)
        options_.firstFrameTimeoutUsec = 3000000;

    snapshot_.targetFps = options_.targetFps;
    snapshot_.pumpAgentFrames = options_.pumpAgentFrames;
}

DisplayRuntimeServiceStartResult DisplayRuntimeService::start()
{
    DisplayRuntimeServiceStartResult result;
    if (snapshot_.active) {
        result.ok = true;
        return result;
    }

    if (options_.session == nullptr) {
        result.messages.push_back("display runtime service requires a session");
        return result;
    }
    if (options_.pumpAgentFrames && agentModule(options_.session) == nullptr) {
        result.messages.push_back("display runtime service requires display.screen.agent");
        return result;
    }

    snapshot_.active = true;
    result.ok = true;
    return result;
}

void DisplayRuntimeService::stop()
{
    snapshot_.active = false;
}

DisplayRuntimePumpResult DisplayRuntimeService::pumpOnce()
{
    return pumpOnce(monotonicNowUsec());
}

DisplayRuntimePumpResult DisplayRuntimeService::pumpOnce(std::uint64_t monotonicNow)
{
    DisplayRuntimePumpResult result;
    result.active = snapshot_.active;
    if (!snapshot_.active)
        return result;

    ++snapshot_.pumpCount;
    snapshot_.lastPumpUsec = monotonicNow;
    updateFrameAge(snapshot_, monotonicNow);

    const std::uint64_t interval = frameIntervalUsec();
    if (lastFramePumpUsec_ != 0 &&
        monotonicNow < lastFramePumpUsec_ + interval) {
        result.skippedByFrameRate = true;
        ++snapshot_.skippedByFrameRate;
        return result;
    }

    lastFramePumpUsec_ = monotonicNow;
    if (!options_.pumpAgentFrames) {
        if (!options_.monitorClientFirstFrame &&
            !options_.monitorClientFreshFrameAfterReconnect)
            return result;

        modules::display::DisplayClientModule* module =
            clientModule(options_.session);
        if (module == nullptr) {
            ++result.missingClientModules;
            ++snapshot_.missingClientModules;
            return result;
        }

        const std::size_t expired =
            module->expireKeyframeRequests(monotonicNow);
        result.keyframeRequestTimeouts = static_cast<int>(expired);
        snapshot_.keyframeRequestTimeouts += result.keyframeRequestTimeouts;

        const modules::display::DisplayClientSnapshot clientSnapshot =
            module->snapshot();

        if (clientSnapshot.renderedFrames > 0 &&
            options_.monitorClientFirstFrame) {
            clientFirstFrameWaitStartUsec_ = 0;
            lastFirstFrameRequestUsec_ = 0;
        }

        if (options_.monitorClientFreshFrameAfterReconnect &&
            clientSnapshot.renderedFrames > 0 &&
            clientSnapshot.reconnectResumes > lastClientReconnectResumes_) {
            lastClientReconnectResumes_ = clientSnapshot.reconnectResumes;
            clientReconnectFrameBaseline_ =
                clientSnapshot.reconnectRenderedFramesBaseline;
            clientReconnectFrameWaitStartUsec_ = monotonicNow;
            lastReconnectFrameRequestUsec_ = 0;
        }

        if (options_.monitorClientFreshFrameAfterReconnect &&
            clientReconnectFrameWaitStartUsec_ != 0) {
            if (clientSnapshot.renderedFrames > clientReconnectFrameBaseline_) {
                clientReconnectFrameWaitStartUsec_ = 0;
                lastReconnectFrameRequestUsec_ = 0;
            } else if (monotonicNow >=
                       clientReconnectFrameWaitStartUsec_ +
                           options_.firstFrameTimeoutUsec &&
                       clientSnapshot.pendingKeyframeRequests == 0 &&
                       (lastReconnectFrameRequestUsec_ == 0 ||
                        monotonicNow >=
                            lastReconnectFrameRequestUsec_ +
                                options_.firstFrameTimeoutUsec)) {
                result.reconnectFrameTimedOut = true;
                ++snapshot_.reconnectFrameTimeouts;
                lastReconnectFrameRequestUsec_ = monotonicNow;
                recordClientKeyframeRequestResult(
                    result,
                    snapshot_,
                    module->requestKeyFrame(
                        modules::display::DisplayKeyframeReason::Reconnect));
            }
        }

        if (!options_.monitorClientFirstFrame ||
            clientSnapshot.renderedFrames > 0)
            return result;

        if (clientFirstFrameWaitStartUsec_ == 0)
            clientFirstFrameWaitStartUsec_ = monotonicNow;

        if (monotonicNow >=
                clientFirstFrameWaitStartUsec_ + options_.firstFrameTimeoutUsec &&
            clientSnapshot.pendingKeyframeRequests == 0 &&
            (lastFirstFrameRequestUsec_ == 0 ||
             monotonicNow >=
                 lastFirstFrameRequestUsec_ + options_.firstFrameTimeoutUsec)) {
            result.firstFrameTimedOut = true;
            ++snapshot_.firstFrameTimeouts;
            lastFirstFrameRequestUsec_ = monotonicNow;
            recordClientKeyframeRequestResult(
                result,
                snapshot_,
                module->requestKeyFrame(
                    modules::display::DisplayKeyframeReason::FirstFrameTimeout));
        }

        return result;
    }

    modules::display::DisplayAgentModule* module = agentModule(options_.session);
    if (module == nullptr) {
        ++result.missingAgentModules;
        ++snapshot_.missingAgentModules;
        return result;
    }

    result.frameAttempted = true;
    ++snapshot_.frameAttempts;
    snapshot_.lastFrameAttemptUsec = monotonicNow;
    const modules::display::DisplayAgentSnapshot before = module->snapshot();
    result.frameSent = module->sendDeltaFrame();
    const modules::display::DisplayAgentSnapshot after = module->snapshot();
    snapshot_.captureBackendId = after.captureBackendId;
    snapshot_.lastCaptureStatus = after.lastCaptureStatus;
    snapshot_.captureErrors = after.captureErrors;
    recordAgentThroughputDelta(snapshot_, before, after);
    if (result.frameSent) {
        result.sentPayloadBytes =
            after.sentPayloadBytes > before.sentPayloadBytes
                ? after.sentPayloadBytes - before.sentPayloadBytes
                : 0;
        ++snapshot_.framesSent;
        recordFrameDelivered(snapshot_, monotonicNow);
        captureRecoveryState_ =
            recordDisplayCaptureFrameSuccess(options_.captureRecoveryPolicy,
                                             captureRecoveryState_,
                                             after.captureBackendId,
                                             monotonicNow);
        result.captureRecoveryAction = DisplayCaptureRecoveryAction::None;
        snapshot_.lastCaptureRecoveryAction =
            DisplayCaptureRecoveryAction::None;
        updateCaptureRecoverySnapshot(snapshot_,
                                      captureRecoveryState_,
                                      monotonicNow);
    } else if (after.droppedFrames > before.droppedFrames) {
        result.droppedFrames = after.droppedFrames - before.droppedFrames;
        snapshot_.droppedFrames += result.droppedFrames;
        recordFrameMiss(snapshot_, monotonicNow);
    } else {
        if (after.encodeFailures > before.encodeFailures) {
            result.encodeFailures = after.encodeFailures - before.encodeFailures;
        } else {
            ++result.sendFailures;
            ++snapshot_.sendFailures;
        }

        const DisplayCaptureRecoveryPlan recovery =
            selectDisplayCaptureRecoveryPlan(options_.captureRecoveryPolicy,
                                             captureRecoveryState_,
                                             after.lastCaptureStatus,
                                             after.captureBackendId,
                                             monotonicNow);
        captureRecoveryState_ = recovery.nextState;
        result.captureRecoveryAction = recovery.decision.action;
        result.captureRecoveryBlockedByCooldown = recovery.blockedByCooldown;
        result.captureRecoveryPromotedToSwitchBackend =
            recovery.promotedToSwitchBackend;
        result.captureRecoveryCooldownRemainingUsec =
            recovery.cooldownRemainingUsec;
        snapshot_.lastCaptureRecoveryAction = recovery.decision.action;
        if (recovery.blockedByCooldown)
            ++snapshot_.captureRecoveryCooldownBlocks;
        if (recovery.promotedToSwitchBackend)
            ++snapshot_.captureRecoverySwitchPromotions;

        if (shouldExecuteCaptureRecovery(recovery.decision.action)) {
            result.captureRecoveryAttempted = true;
            ++snapshot_.captureRecoveryAttempts;
            result.captureRecoverySucceeded =
                module->reopenCapture(recovery.decision.requestKeyframe);
            const modules::display::DisplayAgentSnapshot recovered =
                module->snapshot();
            snapshot_.captureBackendId = recovered.captureBackendId;
            snapshot_.lastCaptureStatus = recovered.lastCaptureStatus;
            snapshot_.captureErrors = recovered.captureErrors;
            recordAgentThroughputDelta(snapshot_, after, recovered);
            captureRecoveryState_ =
                recordDisplayCaptureRecoveryAttempt(
                    options_.captureRecoveryPolicy,
                    recovery,
                    result.captureRecoverySucceeded,
                    recovered.captureBackendId,
                    monotonicNow);
            if (result.captureRecoverySucceeded)
                ++snapshot_.captureRecoverySuccesses;
            else
                ++snapshot_.captureRecoveryFailures;
            if (recovered.sentFrames > after.sentFrames) {
                result.captureRecoveryFramesSent =
                    recovered.sentFrames - after.sentFrames;
                result.sentPayloadBytes =
                    recovered.sentPayloadBytes > after.sentPayloadBytes
                        ? recovered.sentPayloadBytes - after.sentPayloadBytes
                        : 0;
                snapshot_.framesSent += result.captureRecoveryFramesSent;
                recordFrameDelivered(snapshot_, monotonicNow);
            }
        } else if (shouldExecuteCaptureBackendSwitch(recovery.decision.action,
                                                    options_)) {
            result.captureRecoveryAttempted = true;
            ++snapshot_.captureRecoveryAttempts;

            DisplayCaptureBackendFailoverRequest failoverRequest;
            failoverRequest.selectionRequest =
                options_.captureBackendSelectionRequest;
            failoverRequest.failedAdapterId = after.captureBackendId;
            failoverRequest.honorRequestedAdapter =
                !options_.overrideRequestedCaptureBackendOnSwitch;

            const DisplayCaptureBackendCreateResult created =
                createFailoverDisplayCapture(*options_.captureBackendFactory,
                                             failoverRequest);
            result.captureRecoverySucceeded =
                created.ok && created.capture != nullptr &&
                module->replaceCapture(created.capture, true);

            const modules::display::DisplayAgentSnapshot recovered =
                module->snapshot();
            snapshot_.captureBackendId = recovered.captureBackendId;
            snapshot_.lastCaptureStatus = recovered.lastCaptureStatus;
            snapshot_.captureErrors = recovered.captureErrors;
            recordAgentThroughputDelta(snapshot_, after, recovered);
            captureRecoveryState_ =
                recordDisplayCaptureRecoveryAttempt(
                    options_.captureRecoveryPolicy,
                    recovery,
                    result.captureRecoverySucceeded,
                    recovered.captureBackendId,
                    monotonicNow);
            if (result.captureRecoverySucceeded)
                ++snapshot_.captureRecoverySuccesses;
            else
                ++snapshot_.captureRecoveryFailures;
            if (recovered.sentFrames > after.sentFrames) {
                result.captureRecoveryFramesSent =
                    recovered.sentFrames - after.sentFrames;
                result.sentPayloadBytes =
                    recovered.sentPayloadBytes > after.sentPayloadBytes
                        ? recovered.sentPayloadBytes - after.sentPayloadBytes
                        : 0;
                snapshot_.framesSent += result.captureRecoveryFramesSent;
                recordFrameDelivered(snapshot_, monotonicNow);
            }
        }
        if (result.captureRecoveryFramesSent == 0)
            recordFrameMiss(snapshot_, monotonicNow);
        updateCaptureRecoverySnapshot(snapshot_,
                                      captureRecoveryState_,
                                      monotonicNow);
    }
    return result;
}

DisplayRuntimeServiceSnapshot DisplayRuntimeService::snapshot() const
{
    return snapshot_;
}

std::uint64_t DisplayRuntimeService::frameIntervalUsec() const
{
    const std::uint32_t fps = options_.targetFps == 0 ? 15 : options_.targetFps;
    return 1000000ULL / fps;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
