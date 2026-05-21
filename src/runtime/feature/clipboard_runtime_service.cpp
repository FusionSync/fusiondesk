#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"

#include <chrono>
#include <thread>
#include <utility>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

namespace {

std::size_t formatCount(
    const modules::clipboard::TransferSourceBundle& bundle)
{
    std::size_t count = 0;
    for (const std::shared_ptr<modules::clipboard::TransferSource>& source :
         bundle.sources) {
        if (source != nullptr)
            count += source->formats().size();
    }
    return count;
}

bool sameSnapshot(const ClipboardRuntimeServiceSnapshot& left,
                  const modules::clipboard::ClipboardSnapshot& right)
{
    return left.lastBundleId == right.bundle.bundleId &&
           left.lastOfferId == right.bundle.offerId &&
           left.lastOwnerEpoch == right.bundle.ownerEpoch &&
           left.lastSequence == right.bundle.sequence;
}

void pauseClipboardReadPoll()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

modules::clipboard::FdclReadFormatRequest fdclReadRequestFromTransfer(
    const modules::clipboard::TransferReadRequest& request)
{
    modules::clipboard::FdclReadFormatRequest result;
    result.bundleId = request.bundleId;
    result.offerId = request.offerId;
    result.ownerEpoch = request.ownerEpoch;
    result.sourceId = request.sourceId;
    result.itemIndex = request.itemIndex;
    result.formatId = request.formatId;
    result.localFormatToken = request.localFormatToken;
    result.acceptedMaxBytes = request.acceptedMaxBytes;
    result.streamAccepted = request.streamAccepted;
    result.requestedEncoding = request.requestedEncoding;
    result.canonicalFormat = request.canonicalFormat;
    return result;
}

modules::clipboard::FdclCancel cancelFromTransferRead(
    const modules::clipboard::TransferReadRequest& request,
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
    cancel.formatId = request.formatId;
    cancel.reason = reason;
    cancel.message = std::move(message);
    return cancel;
}

modules::clipboard::FdclCancel cancelFromTransferFileRange(
    const modules::clipboard::TransferFileRangeRequest& request,
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

modules::clipboard::TransferReadResult failedRead(
    protocol::ResponseStatus status,
    std::string message)
{
    modules::clipboard::TransferReadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

modules::clipboard::TransferFileRangeResult failedFileRange(
    protocol::ResponseStatus status,
    std::string message)
{
    modules::clipboard::TransferFileRangeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

} // namespace

ClipboardRuntimeRemoteReader::ClipboardRuntimeRemoteReader(
    ClipboardRuntimeRemoteReaderOptions options)
    : options_(std::move(options))
{
    if (options_.policy == nullptr)
        options_.policy = std::make_shared<AllowAllClipboardRuntimePolicy>();
}

modules::clipboard::TransferReadResult
ClipboardRuntimeRemoteReader::readRemoteFormat(
    const modules::clipboard::TransferReadRequest& request,
    std::uint32_t timeoutMs)
{
    modules::clipboard::ClipboardModuleBase* module = clipboardModule();
    if (module == nullptr)
        return failedRead(protocol::ResponseStatus::NotFound,
                          "clipboard remote reader requires a running module");
    if (options_.pump == nullptr)
        return failedRead(protocol::ResponseStatus::InvalidArgument,
                          "clipboard remote reader requires a pump");

    ClipboardRuntimePolicyContext context = makePolicyContext(request);
    const ClipboardRuntimePolicyDecision decision =
        options_.policy == nullptr ?
        ClipboardRuntimePolicyDecision::allow(false) :
        options_.policy->authorize(context);
    if (decision.allowed && decision.auditRequired && options_.policy != nullptr) {
        ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = true;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        options_.policy->audit(event);
    }
    if (!decision.allowed) {
        if (options_.policy != nullptr) {
            ClipboardRuntimeAuditEvent event;
            event.context = context;
            event.allowed = false;
            event.responseStatus = decision.responseStatus;
            event.reason = decision.reason;
            options_.policy->audit(event);
        }
        return failedRead(decision.responseStatus,
                          decision.reason.empty()
                              ? "clipboard remote read denied by policy"
                              : decision.reason);
    }

    const modules::clipboard::ClipboardModuleSnapshot before =
        module->snapshot();
    const std::uint32_t effectiveTimeoutMs =
        timeoutMs == 0 ?
        (options_.defaultTimeoutMs == 0 ? 1000 : options_.defaultTimeoutMs) :
        timeoutMs;
    const std::uint64_t startUsec = options_.pump->monotonicNowUsec();
    const std::uint64_t deadlineUsec =
        startUsec + static_cast<std::uint64_t>(effectiveTimeoutMs) * 1000U;

    const modules::clipboard::ClipboardRemoteReadDispatchResult dispatched =
        module->requestRemoteFormatTracked(fdclReadRequestFromTransfer(request),
                                           effectiveTimeoutMs,
                                           startUsec);
    if (!dispatched.dispatched) {
        return failedRead(dispatched.status,
                          dispatched.message.empty()
                              ? "clipboard remote read request dispatch failed"
                              : dispatched.message);
    }

    std::uint64_t nowUsec = startUsec;
    while (nowUsec <= deadlineUsec) {
        options_.pump->pumpOnce();
        nowUsec = options_.pump->monotonicNowUsec();
        module->expirePendingReads(nowUsec);

        const modules::clipboard::ClipboardModuleSnapshot current =
            module->snapshot();
        if (current.lastReadResponseTo == dispatched.messageId &&
            current.lastReadResult.has_value()) {
            if (current.lastReadResult->status ==
                protocol::ResponseStatus::Timeout) {
                module->sendCancel(
                    cancelFromTransferRead(
                        request,
                        dispatched.messageId,
                        modules::clipboard::FdclCancelReason::Timeout,
                        "clipboard remote read timed out"),
                    effectiveTimeoutMs,
                    nowUsec);
            }
            return *current.lastReadResult;
        }
        if (current.inlineResponsesReceived > before.inlineResponsesReceived ||
            current.timeoutFailures > before.timeoutFailures ||
            current.tooLargeFailures > before.tooLargeFailures ||
            current.staleOfferFailures > before.staleOfferFailures ||
            current.policyDenials > before.policyDenials ||
            current.decodeFailures > before.decodeFailures ||
            current.pendingReads <= before.pendingReads) {
            if (current.pendingReads <= before.pendingReads)
                break;
        }
        if (nowUsec < deadlineUsec)
            pauseClipboardReadPoll();
    }

    module->expirePendingReads(deadlineUsec + 1);
    const modules::clipboard::ClipboardModuleSnapshot expired =
        module->snapshot();
    if (expired.lastReadResponseTo == dispatched.messageId &&
        expired.lastReadResult.has_value() &&
        expired.timeoutFailures > before.timeoutFailures) {
        module->sendCancel(
            cancelFromTransferRead(request,
                                   dispatched.messageId,
                                   modules::clipboard::FdclCancelReason::Timeout,
                                   "clipboard remote read timed out"),
            effectiveTimeoutMs,
            deadlineUsec + 1);
        return *expired.lastReadResult;
    }

    module->sendCancel(
        cancelFromTransferRead(request,
                               dispatched.messageId,
                               modules::clipboard::FdclCancelReason::Timeout,
                               "clipboard remote read timed out"),
        effectiveTimeoutMs,
        deadlineUsec + 1);
    return failedRead(protocol::ResponseStatus::Timeout,
                      "clipboard remote read timed out");
}

modules::clipboard::TransferFileRangeResult
ClipboardRuntimeRemoteReader::readRemoteFileRange(
    const modules::clipboard::TransferFileRangeRequest& request,
    std::uint32_t timeoutMs)
{
    modules::clipboard::ClipboardModuleBase* module = clipboardModule();
    if (module == nullptr) {
        return failedFileRange(protocol::ResponseStatus::NotFound,
                               "clipboard remote file reader requires a running module");
    }
    if (options_.pump == nullptr) {
        return failedFileRange(protocol::ResponseStatus::InvalidArgument,
                               "clipboard remote file reader requires a pump");
    }

    ClipboardRuntimePolicyContext context = makePolicyContext(request);
    const ClipboardRuntimePolicyDecision decision =
        options_.policy == nullptr ?
        ClipboardRuntimePolicyDecision::allow(false) :
        options_.policy->authorize(context);
    if (decision.allowed && decision.auditRequired && options_.policy != nullptr) {
        ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = true;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        options_.policy->audit(event);
    }
    if (!decision.allowed) {
        if (options_.policy != nullptr) {
            ClipboardRuntimeAuditEvent event;
            event.context = context;
            event.allowed = false;
            event.responseStatus = decision.responseStatus;
            event.reason = decision.reason;
            options_.policy->audit(event);
        }
        return failedFileRange(decision.responseStatus,
                               decision.reason.empty()
                                   ? "clipboard remote file range denied by policy"
                                   : decision.reason);
    }

    modules::clipboard::FdclFileRangeRequest fdclRequest;
    fdclRequest.bundleId = request.bundleId;
    fdclRequest.offerId = request.offerId;
    fdclRequest.ownerEpoch = request.ownerEpoch;
    fdclRequest.sourceId = request.sourceId;
    fdclRequest.objectId = request.objectId;
    fdclRequest.fileIndex = request.fileIndex;
    fdclRequest.offset = request.offset;
    fdclRequest.requestedBytes = request.requestedBytes;

    const modules::clipboard::ClipboardModuleSnapshot before =
        module->snapshot();
    const std::uint32_t effectiveTimeoutMs =
        timeoutMs == 0 ?
        (options_.defaultTimeoutMs == 0 ? 1000 : options_.defaultTimeoutMs) :
        timeoutMs;
    const std::uint64_t startUsec = options_.pump->monotonicNowUsec();
    const std::uint64_t deadlineUsec =
        startUsec + static_cast<std::uint64_t>(effectiveTimeoutMs) * 1000U;

    const modules::clipboard::ClipboardRemoteReadDispatchResult dispatched =
        module->requestRemoteFileRangeTracked(fdclRequest,
                                              effectiveTimeoutMs,
                                              startUsec);
    if (!dispatched.dispatched) {
        return failedFileRange(dispatched.status,
                               dispatched.message.empty()
                                   ? "clipboard remote file range dispatch failed"
                                   : dispatched.message);
    }

    std::uint64_t nowUsec = startUsec;
    while (nowUsec <= deadlineUsec) {
        options_.pump->pumpOnce();
        nowUsec = options_.pump->monotonicNowUsec();
        module->expirePendingReads(nowUsec);

        const modules::clipboard::ClipboardModuleSnapshot current =
            module->snapshot();
        if (current.lastFileRangeResponseTo == dispatched.messageId &&
            current.lastFileRangeResult.has_value()) {
            if (current.lastFileRangeResult->status ==
                protocol::ResponseStatus::Timeout) {
                module->sendCancel(
                    cancelFromTransferFileRange(
                        request,
                        dispatched.messageId,
                        modules::clipboard::FdclCancelReason::Timeout,
                        "clipboard remote file range timed out"),
                    effectiveTimeoutMs,
                    nowUsec);
            }
            return *current.lastFileRangeResult;
        }
        if (current.fileRangeResponsesReceived > before.fileRangeResponsesReceived ||
            current.timeoutFailures > before.timeoutFailures ||
            current.tooLargeFailures > before.tooLargeFailures ||
            current.staleOfferFailures > before.staleOfferFailures ||
            current.policyDenials > before.policyDenials ||
            current.decodeFailures > before.decodeFailures ||
            current.pendingReads <= before.pendingReads) {
            if (current.pendingReads <= before.pendingReads)
                break;
        }
        if (nowUsec < deadlineUsec)
            pauseClipboardReadPoll();
    }

    module->expirePendingReads(deadlineUsec + 1);
    const modules::clipboard::ClipboardModuleSnapshot expired =
        module->snapshot();
    if (expired.lastFileRangeResponseTo == dispatched.messageId &&
        expired.lastFileRangeResult.has_value() &&
        expired.timeoutFailures > before.timeoutFailures) {
        module->sendCancel(
            cancelFromTransferFileRange(
                request,
                dispatched.messageId,
                modules::clipboard::FdclCancelReason::Timeout,
                "clipboard remote file range timed out"),
            effectiveTimeoutMs,
            deadlineUsec + 1);
        return *expired.lastFileRangeResult;
    }

    module->sendCancel(
        cancelFromTransferFileRange(request,
                                    dispatched.messageId,
                                    modules::clipboard::FdclCancelReason::Timeout,
                                    "clipboard remote file range timed out"),
        effectiveTimeoutMs,
        deadlineUsec + 1);
    return failedFileRange(protocol::ResponseStatus::Timeout,
                           "clipboard remote file range timed out");
}

modules::clipboard::ClipboardModuleBase*
ClipboardRuntimeRemoteReader::clipboardModule() const
{
    if (options_.session == nullptr || options_.session->moduleHost() == nullptr)
        return nullptr;

    return dynamic_cast<modules::clipboard::ClipboardModuleBase*>(
        options_.session->moduleHost()->module(moduleId()));
}

std::string ClipboardRuntimeRemoteReader::moduleId() const
{
    if (options_.session == nullptr)
        return {};

    if (options_.session->role() == session::SessionRole::Client)
        return "clipboard.redirect.client";
    if (options_.session->role() == session::SessionRole::Agent)
        return "clipboard.redirect.agent";
    return {};
}

ClipboardRuntimePolicyContext ClipboardRuntimeRemoteReader::makePolicyContext(
    const modules::clipboard::TransferReadRequest& request) const
{
    ClipboardRuntimePolicyContext context;
    context.operation = ClipboardRuntimeOperation::RemoteFormatRead;
    context.moduleId = moduleId();
    context.bundleId = request.bundleId;
    context.offerId = request.offerId;
    context.ownerEpoch = request.ownerEpoch;
    context.formatCount = request.canonicalFormat.empty() ? 0U : 1U;
    context.requestedBytes = request.acceptedMaxBytes;
    context.canonicalFormat = request.canonicalFormat;

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

ClipboardRuntimePolicyContext ClipboardRuntimeRemoteReader::makePolicyContext(
    const modules::clipboard::TransferFileRangeRequest& request) const
{
    ClipboardRuntimePolicyContext context;
    context.operation = ClipboardRuntimeOperation::RemoteFileRangeRead;
    context.moduleId = moduleId();
    context.bundleId = request.bundleId;
    context.offerId = request.offerId;
    context.ownerEpoch = request.ownerEpoch;
    context.objectId = request.objectId;
    context.requestedBytes = request.requestedBytes;
    context.fileIndex = request.fileIndex;
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

ClipboardRuntimeService::ClipboardRuntimeService(
    ClipboardRuntimeServiceOptions options)
    : options_(std::move(options))
{
    if (options_.policy == nullptr)
        options_.policy = std::make_shared<AllowAllClipboardRuntimePolicy>();

    snapshot_.endpointAttached = options_.endpoint != nullptr;
}

ClipboardRuntimeService::~ClipboardRuntimeService()
{
    stop();
}

ClipboardRuntimeServiceStartResult ClipboardRuntimeService::start()
{
    ClipboardRuntimeServiceStartResult result;
    if (snapshot_.active) {
        result.ok = true;
        return result;
    }

    if (options_.session == nullptr) {
        result.messages.push_back("clipboard runtime service requires a session");
        return result;
    }
    if (options_.session->moduleHost() == nullptr) {
        result.messages.push_back("clipboard runtime service requires a module host");
        return result;
    }

    snapshot_.active = true;
    result.ok = true;
    return result;
}

void ClipboardRuntimeService::stop()
{
    snapshot_.active = false;
}

ClipboardRuntimePumpResult ClipboardRuntimeService::pumpOnce()
{
    ClipboardRuntimePumpResult result;
    result.active = snapshot_.active;
    if (!snapshot_.active)
        return result;

    ++snapshot_.pumpCount;

    if (options_.endpoint == nullptr) {
        ++snapshot_.missingEndpoints;
        ++result.missingEndpoints;
        return result;
    }

    modules::clipboard::ClipboardModuleBase* module = clipboardModule();
    if (module == nullptr) {
        ++snapshot_.missingModules;
        ++result.missingModules;
        return result;
    }

    std::shared_ptr<modules::clipboard::IClipboardChangeMonitor>
        changeMonitor =
            std::dynamic_pointer_cast<
                modules::clipboard::IClipboardChangeMonitor>(
                options_.endpoint);
    if (changeMonitor != nullptr &&
        !changeMonitor->hasPendingClipboardChange()) {
        ++snapshot_.idlePolls;
        ++result.idlePolls;
        return result;
    }

    const modules::clipboard::ClipboardSnapshot clipboardSnapshot =
        options_.endpoint->snapshot();
    if (changeMonitor != nullptr)
        changeMonitor->markClipboardChangeConsumed();

    if (clipboardSnapshot.bundle.offerId == 0 ||
        clipboardSnapshot.bundle.sources.empty()) {
        return result;
    }

    if (sameSnapshot(snapshot_, clipboardSnapshot)) {
        ++snapshot_.duplicateSnapshots;
        ++result.duplicateSnapshots;
        return result;
    }

    ClipboardRuntimePolicyContext context = makePolicyContext(
        ClipboardRuntimeOperation::LocalSnapshotAnnounce,
        &clipboardSnapshot.bundle);
    if (!authorizeAndAudit(context, &result))
        return result;

    if (!module->announceLocalBundle(clipboardSnapshot.bundle)) {
        ++snapshot_.sendFailures;
        ++result.sendFailures;
        return result;
    }

    ++snapshot_.announcementsSent;
    ++result.announcementsSent;
    snapshot_.lastBundleId = clipboardSnapshot.bundle.bundleId;
    snapshot_.lastOfferId = clipboardSnapshot.bundle.offerId;
    snapshot_.lastOwnerEpoch = clipboardSnapshot.bundle.ownerEpoch;
    snapshot_.lastSequence = clipboardSnapshot.bundle.sequence;
    return result;
}

ClipboardRuntimeExpiryResult ClipboardRuntimeService::expirePendingReads(
    std::uint64_t nowUsec)
{
    ClipboardRuntimeExpiryResult result;
    result.active = snapshot_.active;
    if (!snapshot_.active)
        return result;

    modules::clipboard::ClipboardModuleBase* module = clipboardModule();
    if (module == nullptr) {
        ++snapshot_.missingModules;
        ++result.missingModules;
        return result;
    }

    ClipboardRuntimePolicyContext context =
        makePolicyContext(ClipboardRuntimeOperation::PendingReadExpiry);
    const ClipboardRuntimePolicyDecision decision =
        options_.policy == nullptr ?
        ClipboardRuntimePolicyDecision::allow(false) :
        options_.policy->authorize(context);
    if (!decision.allowed) {
        ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = false;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        if (options_.policy != nullptr)
            options_.policy->audit(event);
        ++snapshot_.auditEvents;
        ++snapshot_.policyDenials;
        ++result.policyDenials;
        return result;
    }

    result.expiredReads = module->expirePendingReads(nowUsec);
    snapshot_.expiredReads += result.expiredReads;
    return result;
}

ClipboardRuntimeServiceSnapshot ClipboardRuntimeService::snapshot() const
{
    return snapshot_;
}

modules::clipboard::ClipboardModuleBase*
ClipboardRuntimeService::clipboardModule() const
{
    if (options_.session == nullptr || options_.session->moduleHost() == nullptr)
        return nullptr;

    return dynamic_cast<modules::clipboard::ClipboardModuleBase*>(
        options_.session->moduleHost()->module(moduleId()));
}

std::string ClipboardRuntimeService::moduleId() const
{
    if (options_.session == nullptr)
        return {};

    if (options_.session->role() == session::SessionRole::Client)
        return "clipboard.redirect.client";
    if (options_.session->role() == session::SessionRole::Agent)
        return "clipboard.redirect.agent";
    return {};
}

ClipboardRuntimePolicyContext ClipboardRuntimeService::makePolicyContext(
    ClipboardRuntimeOperation operation,
    const modules::clipboard::TransferSourceBundle* bundle) const
{
    ClipboardRuntimePolicyContext context;
    context.operation = operation;
    context.moduleId = moduleId();

    if (options_.session != nullptr) {
        const session::SessionContext& sessionContext =
            options_.session->context();
        context.sessionId = sessionContext.sessionId;
        context.traceId = sessionContext.traceId;
        context.role = sessionContext.role;
        context.policyVersion = sessionContext.policyVersion;
    }

    if (bundle != nullptr) {
        context.bundleId = bundle->bundleId;
        context.offerId = bundle->offerId;
        context.ownerEpoch = bundle->ownerEpoch;
        context.sequence = bundle->sequence;
        context.formatCount = formatCount(*bundle);
    }

    return context;
}

bool ClipboardRuntimeService::authorizeAndAudit(
    const ClipboardRuntimePolicyContext& context,
    const ClipboardRuntimePolicyDecision& decision,
    ClipboardRuntimePumpResult* pumpResult)
{
    const bool shouldAudit = !decision.allowed || decision.auditRequired;
    if (shouldAudit && options_.policy != nullptr) {
        ClipboardRuntimeAuditEvent event;
        event.context = context;
        event.allowed = decision.allowed;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        options_.policy->audit(event);
        ++snapshot_.auditEvents;
    }

    if (!decision.allowed) {
        ++snapshot_.policyDenials;
        if (pumpResult != nullptr)
            ++pumpResult->policyDenials;
        return false;
    }

    return true;
}

bool ClipboardRuntimeService::authorizeAndAudit(
    const ClipboardRuntimePolicyContext& context,
    ClipboardRuntimePumpResult* pumpResult)
{
    const ClipboardRuntimePolicyDecision decision =
        options_.policy == nullptr ?
        ClipboardRuntimePolicyDecision::allow(false) :
        options_.policy->authorize(context);
    return authorizeAndAudit(context, decision, pumpResult);
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
