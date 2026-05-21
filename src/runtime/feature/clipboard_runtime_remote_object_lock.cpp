#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"

#include <chrono>
#include <thread>
#include <utility>

#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

void pauseClipboardObjectPoll()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

modules::clipboard::FdclObjectLock fdclObjectLockFromTransfer(
    const modules::clipboard::TransferObjectLockRequest& request)
{
    modules::clipboard::FdclObjectLock result;
    result.bundleId = request.bundleId;
    result.offerId = request.offerId;
    result.ownerEpoch = request.ownerEpoch;
    result.sourceId = request.sourceId;
    result.objectId = request.objectId;
    result.fileIndex = request.fileIndex;
    result.lockId = request.lockId;
    result.leaseUsec = request.leaseUsec;
    return result;
}

modules::clipboard::FdclCancel cancelFromTransferObjectLock(
    const modules::clipboard::TransferObjectLockRequest& request,
    protocol::MessageId messageId,
    modules::clipboard::FdclCancelReason reason,
    std::string message)
{
    modules::clipboard::FdclCancel cancel;
    cancel.correlationId = messageId;
    cancel.bundleId = request.bundleId;
    cancel.offerId = request.offerId;
    cancel.ownerEpoch = request.ownerEpoch;
    cancel.sourceId = request.sourceId;
    cancel.reason = reason;
    cancel.message = std::move(message);
    return cancel;
}

modules::clipboard::TransferObjectLockResult failedObjectLock(
    protocol::ResponseStatus status,
    std::string message)
{
    modules::clipboard::TransferObjectLockResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

} // namespace

modules::clipboard::TransferObjectLockResult
ClipboardRuntimeRemoteReader::lockRemoteObject(
    const modules::clipboard::TransferObjectLockRequest& request,
    std::uint32_t timeoutMs)
{
    modules::clipboard::ClipboardModuleBase* module = clipboardModule();
    if (module == nullptr) {
        return failedObjectLock(
            protocol::ResponseStatus::NotFound,
            "clipboard remote object lock requires a running module");
    }
    if (options_.pump == nullptr) {
        return failedObjectLock(
            protocol::ResponseStatus::InvalidArgument,
            "clipboard remote object lock requires a pump");
    }

    ClipboardRuntimePolicyContext context =
        makePolicyContext(ClipboardRuntimeOperation::RemoteObjectLock,
                          request);
    const ClipboardRuntimePolicyDecision decision =
        options_.policy == nullptr
            ? ClipboardRuntimePolicyDecision::allow(false)
            : options_.policy->authorize(context);
    if (decision.auditRequired && options_.policy != nullptr) {
        ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = decision.allowed;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        options_.policy->audit(event);
    }
    if (!decision.allowed) {
        return failedObjectLock(
            decision.responseStatus,
            decision.reason.empty()
                ? "clipboard remote object lock denied by policy"
                : decision.reason);
    }

    const modules::clipboard::ClipboardModuleSnapshot before =
        module->snapshot();
    const std::uint32_t effectiveTimeoutMs =
        timeoutMs == 0
            ? (options_.defaultTimeoutMs == 0 ? 1000 : options_.defaultTimeoutMs)
            : timeoutMs;
    const std::uint64_t startUsec =
        options_.pump == nullptr ? monotonicNowUsec()
                                 : options_.pump->monotonicNowUsec();
    const std::uint64_t deadlineUsec =
        startUsec + static_cast<std::uint64_t>(effectiveTimeoutMs) * 1000U;

    const modules::clipboard::ClipboardRemoteReadDispatchResult dispatched =
        module->requestRemoteObjectLockTracked(
            fdclObjectLockFromTransfer(request),
            effectiveTimeoutMs,
            startUsec);
    if (!dispatched.dispatched) {
        return failedObjectLock(
            dispatched.status,
            dispatched.message.empty()
                ? "clipboard remote object lock dispatch failed"
                : dispatched.message);
    }

    std::uint64_t nowUsec = startUsec;
    while (nowUsec <= deadlineUsec) {
        options_.pump->pumpOnce();
        nowUsec = options_.pump->monotonicNowUsec();
        module->expirePendingReads(nowUsec);

        const modules::clipboard::ClipboardModuleSnapshot current =
            module->snapshot();
        if (current.lastObjectLockResponseTo == dispatched.messageId &&
            current.lastObjectLockResult.has_value()) {
            if (current.lastObjectLockResult->status ==
                protocol::ResponseStatus::Timeout) {
                module->sendCancel(
                    cancelFromTransferObjectLock(
                        request,
                        dispatched.messageId,
                        modules::clipboard::FdclCancelReason::Timeout,
                        "clipboard remote object lock timed out"),
                    effectiveTimeoutMs,
                    nowUsec);
            }
            return *current.lastObjectLockResult;
        }
        if (current.objectLockResponsesReceived >
                before.objectLockResponsesReceived ||
            current.timeoutFailures > before.timeoutFailures ||
            current.staleOfferFailures > before.staleOfferFailures ||
            current.policyDenials > before.policyDenials ||
            current.decodeFailures > before.decodeFailures ||
            current.pendingReads <= before.pendingReads) {
            if (current.pendingReads <= before.pendingReads)
                break;
        }
        if (nowUsec < deadlineUsec)
            pauseClipboardObjectPoll();
    }

    module->expirePendingReads(deadlineUsec + 1);
    const modules::clipboard::ClipboardModuleSnapshot expired =
        module->snapshot();
    if (expired.lastObjectLockResponseTo == dispatched.messageId &&
        expired.lastObjectLockResult.has_value() &&
        expired.timeoutFailures > before.timeoutFailures) {
        module->sendCancel(
            cancelFromTransferObjectLock(
                request,
                dispatched.messageId,
                modules::clipboard::FdclCancelReason::Timeout,
                "clipboard remote object lock timed out"),
            effectiveTimeoutMs,
            deadlineUsec + 1);
        return *expired.lastObjectLockResult;
    }

    module->sendCancel(
        cancelFromTransferObjectLock(request,
                                     dispatched.messageId,
                                     modules::clipboard::FdclCancelReason::Timeout,
                                     "clipboard remote object lock timed out"),
        effectiveTimeoutMs,
        deadlineUsec + 1);
    return failedObjectLock(protocol::ResponseStatus::Timeout,
                            "clipboard remote object lock timed out");
}

modules::clipboard::TransferObjectLockResult
ClipboardRuntimeRemoteReader::unlockRemoteObject(
    const modules::clipboard::TransferObjectLockRequest& request,
    std::uint32_t timeoutMs)
{
    modules::clipboard::ClipboardModuleBase* module = clipboardModule();
    if (module == nullptr) {
        return failedObjectLock(
            protocol::ResponseStatus::NotFound,
            "clipboard remote object unlock requires a running module");
    }
    if (options_.pump == nullptr) {
        return failedObjectLock(
            protocol::ResponseStatus::InvalidArgument,
            "clipboard remote object unlock requires a pump");
    }

    ClipboardRuntimePolicyContext context =
        makePolicyContext(ClipboardRuntimeOperation::RemoteObjectUnlock,
                          request);
    const ClipboardRuntimePolicyDecision decision =
        options_.policy == nullptr
            ? ClipboardRuntimePolicyDecision::allow(false)
            : options_.policy->authorize(context);
    if (decision.auditRequired && options_.policy != nullptr) {
        ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = decision.allowed;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        options_.policy->audit(event);
    }
    if (!decision.allowed) {
        return failedObjectLock(
            decision.responseStatus,
            decision.reason.empty()
                ? "clipboard remote object unlock denied by policy"
                : decision.reason);
    }

    const modules::clipboard::ClipboardModuleSnapshot before =
        module->snapshot();
    const std::uint32_t effectiveTimeoutMs =
        timeoutMs == 0
            ? (options_.defaultTimeoutMs == 0 ? 1000 : options_.defaultTimeoutMs)
            : timeoutMs;
    const std::uint64_t startUsec =
        options_.pump == nullptr ? monotonicNowUsec()
                                 : options_.pump->monotonicNowUsec();
    const std::uint64_t deadlineUsec =
        startUsec + static_cast<std::uint64_t>(effectiveTimeoutMs) * 1000U;

    const modules::clipboard::ClipboardRemoteReadDispatchResult dispatched =
        module->requestRemoteObjectUnlockTracked(
            fdclObjectLockFromTransfer(request),
            effectiveTimeoutMs,
            startUsec);
    if (!dispatched.dispatched) {
        return failedObjectLock(
            dispatched.status,
            dispatched.message.empty()
                ? "clipboard remote object unlock dispatch failed"
                : dispatched.message);
    }

    std::uint64_t nowUsec = startUsec;
    while (nowUsec <= deadlineUsec) {
        options_.pump->pumpOnce();
        nowUsec = options_.pump->monotonicNowUsec();
        module->expirePendingReads(nowUsec);

        const modules::clipboard::ClipboardModuleSnapshot current =
            module->snapshot();
        if (current.lastObjectUnlockResponseTo == dispatched.messageId &&
            current.lastObjectUnlockResult.has_value()) {
            if (current.lastObjectUnlockResult->status ==
                protocol::ResponseStatus::Timeout) {
                module->sendCancel(
                    cancelFromTransferObjectLock(
                        request,
                        dispatched.messageId,
                        modules::clipboard::FdclCancelReason::Timeout,
                        "clipboard remote object unlock timed out"),
                    effectiveTimeoutMs,
                    nowUsec);
            }
            return *current.lastObjectUnlockResult;
        }
        if (current.objectUnlockResponsesReceived >
                before.objectUnlockResponsesReceived ||
            current.timeoutFailures > before.timeoutFailures ||
            current.staleOfferFailures > before.staleOfferFailures ||
            current.policyDenials > before.policyDenials ||
            current.decodeFailures > before.decodeFailures ||
            current.pendingReads <= before.pendingReads) {
            if (current.pendingReads <= before.pendingReads)
                break;
        }
        if (nowUsec < deadlineUsec)
            pauseClipboardObjectPoll();
    }

    module->expirePendingReads(deadlineUsec + 1);
    const modules::clipboard::ClipboardModuleSnapshot expired =
        module->snapshot();
    if (expired.lastObjectUnlockResponseTo == dispatched.messageId &&
        expired.lastObjectUnlockResult.has_value() &&
        expired.timeoutFailures > before.timeoutFailures) {
        module->sendCancel(
            cancelFromTransferObjectLock(
                request,
                dispatched.messageId,
                modules::clipboard::FdclCancelReason::Timeout,
                "clipboard remote object unlock timed out"),
            effectiveTimeoutMs,
            deadlineUsec + 1);
        return *expired.lastObjectUnlockResult;
    }

    module->sendCancel(
        cancelFromTransferObjectLock(
            request,
            dispatched.messageId,
            modules::clipboard::FdclCancelReason::Timeout,
            "clipboard remote object unlock timed out"),
        effectiveTimeoutMs,
        deadlineUsec + 1);
    return failedObjectLock(protocol::ResponseStatus::Timeout,
                            "clipboard remote object unlock timed out");
}

ClipboardRuntimePolicyContext ClipboardRuntimeRemoteReader::makePolicyContext(
    ClipboardRuntimeOperation operation,
    const modules::clipboard::TransferObjectLockRequest& request) const
{
    ClipboardRuntimePolicyContext context;
    context.operation = operation;
    context.moduleId = moduleId();
    context.bundleId = request.bundleId;
    context.offerId = request.offerId;
    context.ownerEpoch = request.ownerEpoch;
    context.objectId = request.objectId;
    context.fileIndex = request.fileIndex;
    context.requestedBytes = request.leaseUsec;
    context.formatCount = 1;
    context.canonicalFormat = modules::clipboard::FdclFileListFormat;

    if (options_.session != nullptr) {
        const session::SessionContext& sessionContext =
            options_.session->context();
        context.sessionId = sessionContext.sessionId;
        context.traceId = sessionContext.traceId;
        context.role = sessionContext.role;
        context.policyVersion = sessionContext.policyVersion;
    }

    return context;
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
