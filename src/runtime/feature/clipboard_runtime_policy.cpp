#include "fusiondesk/runtime/feature/clipboard_runtime_policy.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace feature {

ClipboardRuntimePolicyDecision ClipboardRuntimePolicyDecision::allow(
    bool audit,
    std::string reason)
{
    ClipboardRuntimePolicyDecision decision;
    decision.allowed = true;
    decision.auditRequired = audit;
    decision.responseStatus = protocol::ResponseStatus::Ok;
    decision.reason = std::move(reason);
    return decision;
}

ClipboardRuntimePolicyDecision ClipboardRuntimePolicyDecision::deny(
    protocol::ResponseStatus status,
    std::string reason)
{
    ClipboardRuntimePolicyDecision decision;
    decision.allowed = false;
    decision.auditRequired = true;
    decision.responseStatus = status;
    decision.reason = std::move(reason);
    return decision;
}

ClipboardRuntimePolicyDecision AllowAllClipboardRuntimePolicy::authorize(
    const ClipboardRuntimePolicyContext& context)
{
    (void)context;
    return ClipboardRuntimePolicyDecision::allow(false);
}

void AllowAllClipboardRuntimePolicy::audit(
    const ClipboardRuntimeAuditEvent& event)
{
    auditEvents_.push_back(event);
}

const std::vector<ClipboardRuntimeAuditEvent>&
AllowAllClipboardRuntimePolicy::auditEvents() const
{
    return auditEvents_;
}

const char* clipboardRuntimeOperationName(ClipboardRuntimeOperation operation)
{
    switch (operation) {
    case ClipboardRuntimeOperation::LocalSnapshotAnnounce:
        return "LocalSnapshotAnnounce";
    case ClipboardRuntimeOperation::RemoteFormatRead:
        return "RemoteFormatRead";
    case ClipboardRuntimeOperation::RemoteFileRangeRead:
        return "RemoteFileRangeRead";
    case ClipboardRuntimeOperation::RemoteObjectLock:
        return "RemoteObjectLock";
    case ClipboardRuntimeOperation::RemoteObjectUnlock:
        return "RemoteObjectUnlock";
    case ClipboardRuntimeOperation::PendingReadExpiry:
        return "PendingReadExpiry";
    }
    return "Unknown";
}

ConfigurableClipboardRuntimePolicy::ConfigurableClipboardRuntimePolicy(
    ClipboardRuntimePolicyRules rules)
    : rules_(std::move(rules))
{
}

ClipboardRuntimePolicyDecision
ConfigurableClipboardRuntimePolicy::authorize(
    const ClipboardRuntimePolicyContext& context)
{
    ++snapshot_.authorizeCalls;
    snapshot_.lastOperation = context.operation;
    snapshot_.lastBundleId = context.bundleId;
    snapshot_.lastOfferId = context.offerId;
    snapshot_.lastOwnerEpoch = context.ownerEpoch;
    snapshot_.lastObjectId = context.objectId;
    snapshot_.lastRequestedBytes = context.requestedBytes;
    snapshot_.lastFileIndex = context.fileIndex;
    snapshot_.lastCanonicalFormat = context.canonicalFormat;

    if (!operationAllowed(context.operation)) {
        ++snapshot_.denied;
        snapshot_.lastAllowed = false;
        snapshot_.lastStatus = protocol::ResponseStatus::DeniedByPolicy;
        snapshot_.lastReason = rules_.denialReason;
        return ClipboardRuntimePolicyDecision::deny(
            protocol::ResponseStatus::DeniedByPolicy,
            rules_.denialReason);
    }

    ++snapshot_.allowed;
    snapshot_.lastAllowed = true;
    snapshot_.lastStatus = protocol::ResponseStatus::Ok;
    snapshot_.lastReason = "allowed";
    const bool auditAllowedOperation =
        rules_.auditAllowed &&
        context.operation != ClipboardRuntimeOperation::PendingReadExpiry;
    return ClipboardRuntimePolicyDecision::allow(auditAllowedOperation,
                                                 "allowed");
}

void ConfigurableClipboardRuntimePolicy::audit(
    const ClipboardRuntimeAuditEvent& event)
{
    ++snapshot_.auditEvents;
    if (event.allowed)
        ++snapshot_.auditedAllowed;
    else
        ++snapshot_.auditedDenied;
    snapshot_.lastOperation = event.context.operation;
    snapshot_.lastAllowed = event.allowed;
    snapshot_.lastStatus = event.responseStatus;
    snapshot_.lastReason = event.reason;
    snapshot_.lastBundleId = event.context.bundleId;
    snapshot_.lastOfferId = event.context.offerId;
    snapshot_.lastOwnerEpoch = event.context.ownerEpoch;
    snapshot_.lastObjectId = event.context.objectId;
    snapshot_.lastRequestedBytes = event.context.requestedBytes;
    snapshot_.lastFileIndex = event.context.fileIndex;
    snapshot_.lastCanonicalFormat = event.context.canonicalFormat;

    if (rules_.maxRecentAuditEvents == 0) {
        snapshot_.recentAuditEvents.clear();
        return;
    }

    snapshot_.recentAuditEvents.push_back(event);
    while (snapshot_.recentAuditEvents.size() > rules_.maxRecentAuditEvents)
        snapshot_.recentAuditEvents.erase(snapshot_.recentAuditEvents.begin());
}

const ClipboardRuntimePolicyRules&
ConfigurableClipboardRuntimePolicy::rules() const
{
    return rules_;
}

ClipboardRuntimePolicySnapshot
ConfigurableClipboardRuntimePolicy::snapshot() const
{
    return snapshot_;
}

bool ConfigurableClipboardRuntimePolicy::operationAllowed(
    ClipboardRuntimeOperation operation) const
{
    switch (operation) {
    case ClipboardRuntimeOperation::LocalSnapshotAnnounce:
        return rules_.allowLocalSnapshotAnnounce;
    case ClipboardRuntimeOperation::RemoteFormatRead:
        return rules_.allowRemoteFormatRead;
    case ClipboardRuntimeOperation::RemoteFileRangeRead:
        return rules_.allowRemoteFileRangeRead;
    case ClipboardRuntimeOperation::RemoteObjectLock:
        return rules_.allowRemoteObjectLock;
    case ClipboardRuntimeOperation::RemoteObjectUnlock:
        return rules_.allowRemoteObjectUnlock;
    case ClipboardRuntimeOperation::PendingReadExpiry:
        return rules_.allowPendingReadExpiry;
    }
    return false;
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
