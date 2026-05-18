#include "fusiondesk/runtime/feature/clipboard_product_presenter.h"

#include <string>

namespace fusiondesk {
namespace runtime {
namespace feature {
namespace {

const char* operationToken(ClipboardRuntimeOperation operation)
{
    switch (operation) {
    case ClipboardRuntimeOperation::LocalSnapshotAnnounce:
        return "local_snapshot_announce";
    case ClipboardRuntimeOperation::RemoteFormatRead:
        return "remote_format_read";
    case ClipboardRuntimeOperation::RemoteFileRangeRead:
        return "remote_file_range_read";
    case ClipboardRuntimeOperation::RemoteObjectLock:
        return "remote_object_lock";
    case ClipboardRuntimeOperation::RemoteObjectUnlock:
        return "remote_object_unlock";
    case ClipboardRuntimeOperation::PendingReadExpiry:
        return "pending_read_expiry";
    }
    return "unknown";
}

std::string runtimeStateFor(
    const ClipboardRuntimeServiceSnapshot& runtime)
{
    if (!runtime.active)
        return "runtime.stopped";
    if (!runtime.endpointAttached)
        return "runtime.endpoint_missing";
    if (runtime.missingModules > 0)
        return "runtime.module_missing";
    if (runtime.sendFailures > 0)
        return "runtime.send_failed";
    if (runtime.policyDenials > 0)
        return "runtime.policy_limited";
    if (runtime.expiredReads > 0)
        return "runtime.pending_read_expired";
    if (runtime.announcementsSent > 0)
        return "runtime.announced";
    return runtime.pumpCount > 0 ? "runtime.idle" : "runtime.ready";
}

std::string policyStateFor(
    const ClipboardRuntimePolicySnapshot& policy)
{
    if (policy.authorizeCalls == 0 && policy.auditEvents == 0)
        return "policy.none";

    std::string state = policy.denied > 0 ? "policy.denied." :
                                            "policy.allowed.";
    state += operationToken(policy.lastOperation);
    return state;
}

std::string statusCodeFor(
    const ClipboardRuntimeServiceSnapshot& runtime,
    const ClipboardRuntimePolicySnapshot& policy)
{
    if (!runtime.active)
        return "clipboard.runtime_stopped";
    if (!runtime.endpointAttached || runtime.missingEndpoints > 0)
        return "clipboard.endpoint_missing";
    if (runtime.missingModules > 0)
        return "clipboard.module_missing";
    if (runtime.sendFailures > 0)
        return "clipboard.send_degraded";
    if (runtime.policyDenials > 0 || policy.denied > 0)
        return "clipboard.policy_limited";
    if (runtime.expiredReads > 0)
        return "clipboard.transfer_warning";
    if (runtime.announcementsSent > 0)
        return "clipboard.offer_active";
    if (runtime.pumpCount > 0)
        return "clipboard.idle";
    return "clipboard.ready";
}

std::string actionCodeFor(const std::string& statusCode)
{
    if (statusCode == "clipboard.runtime_stopped")
        return "clipboard.start_runtime";
    if (statusCode == "clipboard.endpoint_missing")
        return "clipboard.attach_endpoint";
    if (statusCode == "clipboard.module_missing")
        return "module.mount_or_start_clipboard";
    if (statusCode == "clipboard.send_degraded")
        return "clipboard.inspect_transport";
    if (statusCode == "clipboard.policy_limited")
        return "policy.review_clipboard_rules";
    if (statusCode == "clipboard.transfer_warning")
        return "clipboard.inspect_pending_reads";
    return "none";
}

ClipboardProductHealthLevel healthFor(
    const ClipboardRuntimeServiceSnapshot& runtime,
    const ClipboardRuntimePolicySnapshot& policy)
{
    if (!runtime.active || !runtime.endpointAttached ||
        runtime.missingEndpoints > 0 || runtime.missingModules > 0) {
        return ClipboardProductHealthLevel::Blocked;
    }
    if (runtime.sendFailures > 0)
        return ClipboardProductHealthLevel::Degraded;
    if (runtime.policyDenials > 0 || policy.denied > 0 ||
        runtime.expiredReads > 0) {
        return ClipboardProductHealthLevel::Warning;
    }
    return ClipboardProductHealthLevel::Ok;
}

void addDetailMessages(
    ClipboardProductHealthPresentation& presentation,
    const ClipboardRuntimeServiceSnapshot& runtime,
    const ClipboardRuntimePolicySnapshot& policy)
{
    if (runtime.lastOfferId != 0) {
        presentation.detailMessages.push_back(
            "lastOffer=" + std::to_string(runtime.lastOfferId));
    }
    if (runtime.policyDenials > 0 || policy.denied > 0) {
        presentation.detailMessages.push_back(
            "policyDenied=" +
            std::to_string(runtime.policyDenials + policy.denied));
    }
    if (!policy.lastReason.empty()) {
        presentation.detailMessages.push_back(
            "lastPolicyReason=" + policy.lastReason);
    }
    if (runtime.sendFailures > 0) {
        presentation.detailMessages.push_back(
            "sendFailures=" + std::to_string(runtime.sendFailures));
    }
    if (runtime.expiredReads > 0) {
        presentation.detailMessages.push_back(
            "expiredReads=" + std::to_string(runtime.expiredReads));
    }
}

bool directionBlocked(const modules::clipboard::ClipboardPolicy& policy)
{
    return !policy.allowAnnounce &&
           !policy.allowReceive &&
           !policy.allowSendContent &&
           !policy.allowWriteLocal;
}

bool directionOpen(const modules::clipboard::ClipboardPolicy& policy)
{
    return policy.allowAnnounce &&
           policy.allowReceive &&
           policy.allowSendContent &&
           policy.allowWriteLocal;
}

bool contentBlocked(const modules::clipboard::ClipboardPolicy& policy)
{
    return !policy.allowPlainText &&
           !policy.allowHtml &&
           !policy.allowRtf &&
           !policy.allowImage &&
           !policy.allowFileList &&
           !policy.allowCustomFormats;
}

bool standardContentOpen(const modules::clipboard::ClipboardPolicy& policy)
{
    return policy.allowPlainText &&
           policy.allowHtml &&
           policy.allowRtf &&
           policy.allowImage &&
           policy.allowFileList;
}

bool runtimeBlocked(const ClipboardRuntimePolicyRules& rules)
{
    return !rules.allowLocalSnapshotAnnounce &&
           !rules.allowRemoteFormatRead &&
           !rules.allowRemoteFileRangeRead &&
           !rules.allowRemoteObjectLock &&
           !rules.allowRemoteObjectUnlock;
}

std::string directionStateFor(
    const modules::clipboard::ClipboardPolicy& policy)
{
    if (directionBlocked(policy))
        return "direction.blocked";
    if (directionOpen(policy))
        return "direction.bidirectional";
    return "direction.restricted";
}

std::string contentStateFor(
    const modules::clipboard::ClipboardPolicy& policy)
{
    if (contentBlocked(policy))
        return "content.blocked";

    const bool textOnly =
        (policy.allowPlainText || policy.allowHtml || policy.allowRtf) &&
        !policy.allowImage &&
        !policy.allowFileList &&
        !policy.allowCustomFormats;
    if (textOnly)
        return "content.text_only";

    if (standardContentOpen(policy))
        return "content.standard";
    return "content.restricted";
}

std::string fileStateFor(
    const modules::clipboard::ClipboardPolicy& modulePolicy,
    const ClipboardRuntimePolicyRules& runtimeRules)
{
    if (!modulePolicy.allowFileList)
        return "file.disabled";
    if (!modulePolicy.allowFileContents ||
        !runtimeRules.allowRemoteFileRangeRead) {
        return "file.metadata_only";
    }
    if (!runtimeRules.allowRemoteObjectLock ||
        !runtimeRules.allowRemoteObjectUnlock) {
        return "file.stream_limited";
    }
    return "file.contents_enabled";
}

std::string runtimePolicyStateFor(
    const ClipboardRuntimePolicyRules& rules)
{
    if (runtimeBlocked(rules))
        return "runtime.blocked";
    if (!rules.allowLocalSnapshotAnnounce ||
        !rules.allowRemoteFormatRead) {
        return "runtime.clipboard_limited";
    }
    if (!rules.allowRemoteFileRangeRead ||
        !rules.allowRemoteObjectLock ||
        !rules.allowRemoteObjectUnlock ||
        !rules.allowPendingReadExpiry) {
        return "runtime.transfer_limited";
    }
    return "runtime.open";
}

bool policyOpen(const modules::clipboard::ClipboardPolicy& modulePolicy,
                const ClipboardRuntimePolicyRules& runtimeRules)
{
    return directionOpen(modulePolicy) &&
           standardContentOpen(modulePolicy) &&
           modulePolicy.allowFileContents &&
           modulePolicy.allowDrag &&
           runtimeRules.allowLocalSnapshotAnnounce &&
           runtimeRules.allowRemoteFormatRead &&
           runtimeRules.allowRemoteFileRangeRead &&
           runtimeRules.allowRemoteObjectLock &&
           runtimeRules.allowRemoteObjectUnlock &&
           runtimeRules.allowPendingReadExpiry;
}

std::string modeCodeFor(
    const modules::clipboard::ClipboardPolicy& modulePolicy,
    const ClipboardRuntimePolicyRules& runtimeRules)
{
    if (directionBlocked(modulePolicy) ||
        contentBlocked(modulePolicy) ||
        runtimeBlocked(runtimeRules)) {
        return "clipboard.policy.blocked";
    }
    if (policyOpen(modulePolicy, runtimeRules))
        return "clipboard.policy.open";
    return "clipboard.policy.restricted";
}

std::string policyActionCodeFor(
    const ClipboardProductPolicyPresentation& presentation)
{
    if (presentation.modeCode == "clipboard.policy.blocked")
        return "policy.enable_clipboard";
    if (presentation.directionState != "direction.bidirectional")
        return "policy.review_direction";
    if (presentation.contentState == "content.blocked" ||
        presentation.contentState == "content.restricted") {
        return "policy.review_formats";
    }
    if (presentation.fileState != "file.contents_enabled")
        return "policy.review_file_transfer";
    if (presentation.dragState == "drag.disabled")
        return "policy.review_drag";
    if (presentation.runtimeState != "runtime.open")
        return "policy.review_runtime_rules";
    return "none";
}

void addPolicyDetailMessages(
    ClipboardProductPolicyPresentation& presentation,
    const modules::clipboard::ClipboardPolicy& modulePolicy,
    const ClipboardRuntimePolicyRules& runtimeRules)
{
    if (!modulePolicy.allowPlainText)
        presentation.detailMessages.push_back("plainText=false");
    if (!modulePolicy.allowImage)
        presentation.detailMessages.push_back("image=false");
    if (!modulePolicy.allowFileContents)
        presentation.detailMessages.push_back("fileContents=false");
    if (!runtimeRules.allowRemoteFormatRead)
        presentation.detailMessages.push_back("remoteRead=false");
    if (!runtimeRules.allowRemoteFileRangeRead)
        presentation.detailMessages.push_back("remoteFileRange=false");
    if (!runtimeRules.denialReason.empty()) {
        presentation.detailMessages.push_back(
            "denialReason=" + runtimeRules.denialReason);
    }
}

} // namespace

const char* clipboardProductHealthLevelName(
    ClipboardProductHealthLevel level)
{
    switch (level) {
    case ClipboardProductHealthLevel::Unknown:
        return "unknown";
    case ClipboardProductHealthLevel::Ok:
        return "ok";
    case ClipboardProductHealthLevel::Warning:
        return "warning";
    case ClipboardProductHealthLevel::Degraded:
        return "degraded";
    case ClipboardProductHealthLevel::Blocked:
        return "blocked";
    }
    return "unknown";
}

ClipboardProductHealthPresentation
buildClipboardProductHealthPresentation(
    const ClipboardRuntimeServiceSnapshot& runtime,
    const ClipboardRuntimePolicySnapshot& policy)
{
    ClipboardProductHealthPresentation presentation;
    presentation.health = healthFor(runtime, policy);
    presentation.healthName =
        clipboardProductHealthLevelName(presentation.health);
    presentation.usable =
        presentation.health == ClipboardProductHealthLevel::Ok ||
        presentation.health == ClipboardProductHealthLevel::Warning;
    presentation.statusCode = statusCodeFor(runtime, policy);
    presentation.primaryActionCode =
        actionCodeFor(presentation.statusCode);
    presentation.runtimeState = runtimeStateFor(runtime);
    presentation.policyState = policyStateFor(policy);
    presentation.showPolicyDenialWarning =
        runtime.policyDenials > 0 || policy.denied > 0;
    presentation.showAuditIndicator =
        runtime.auditEvents > 0 || policy.auditEvents > 0;
    presentation.showTransferWarning =
        runtime.sendFailures > 0 || runtime.expiredReads > 0;
    addDetailMessages(presentation, runtime, policy);
    return presentation;
}

ClipboardProductPolicyPresentation
buildClipboardProductPolicyPresentation(
    const ProductClipboardPolicy& policy)
{
    ClipboardProductPolicyPresentation presentation;
    const modules::clipboard::ClipboardPolicy modulePolicy =
        clipboardModulePolicyFromProductPolicy(policy);
    const ClipboardRuntimePolicyRules runtimeRules =
        clipboardRuntimePolicyRulesFromProductPolicy(policy);

    presentation.directionState = directionStateFor(modulePolicy);
    presentation.contentState = contentStateFor(modulePolicy);
    presentation.fileState = fileStateFor(modulePolicy, runtimeRules);
    presentation.dragState =
        modulePolicy.allowDrag ? "drag.enabled" : "drag.disabled";
    presentation.customFormatState =
        modulePolicy.allowCustomFormats ? "custom.enabled" :
                                          "custom.disabled";
    presentation.runtimeState = runtimePolicyStateFor(runtimeRules);
    presentation.auditState =
        runtimeRules.auditAllowed ? "audit.enabled" : "audit.disabled";
    presentation.modeCode = modeCodeFor(modulePolicy, runtimeRules);
    presentation.usable =
        presentation.modeCode != "clipboard.policy.blocked";
    presentation.allowPlainText = modulePolicy.allowPlainText;
    presentation.allowRichText =
        modulePolicy.allowHtml || modulePolicy.allowRtf;
    presentation.allowImage = modulePolicy.allowImage;
    presentation.allowFiles = modulePolicy.allowFileList;
    presentation.allowFileContents =
        modulePolicy.allowFileContents &&
        runtimeRules.allowRemoteFileRangeRead;
    presentation.allowDrag = modulePolicy.allowDrag;
    presentation.allowCustomFormats = modulePolicy.allowCustomFormats;
    presentation.auditEnabled = runtimeRules.auditAllowed;
    presentation.showRestrictionWarning =
        presentation.modeCode != "clipboard.policy.open";
    presentation.showFileTransferWarning =
        presentation.fileState != "file.contents_enabled";
    presentation.showAuditIndicator = runtimeRules.auditAllowed;
    presentation.maxInlineBytes = modulePolicy.maxInlineBytes;
    presentation.maxFileRangeBytes = modulePolicy.maxFileRangeBytes;
    presentation.maxFileCount = modulePolicy.maxFileCount;
    presentation.maxSingleFileBytes = modulePolicy.maxSingleFileBytes;
    presentation.primaryActionCode = policyActionCodeFor(presentation);
    addPolicyDetailMessages(presentation, modulePolicy, runtimeRules);
    return presentation;
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
