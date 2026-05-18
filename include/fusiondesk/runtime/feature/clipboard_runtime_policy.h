#ifndef FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_RUNTIME_POLICY_H
#define FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_RUNTIME_POLICY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/core/session/session_context.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

enum class ClipboardRuntimeOperation
{
    LocalSnapshotAnnounce,
    RemoteFormatRead,
    RemoteFileRangeRead,
    RemoteObjectLock,
    RemoteObjectUnlock,
    PendingReadExpiry
};

struct ClipboardRuntimePolicyContext
{
    protocol::SessionId sessionId = 0;
    protocol::TraceId traceId = 0;
    session::SessionRole role = session::SessionRole::Standalone;
    std::string moduleId;
    std::string policyVersion;
    ClipboardRuntimeOperation operation = ClipboardRuntimeOperation::LocalSnapshotAnnounce;
    std::uint64_t bundleId = 0;
    std::uint64_t offerId = 0;
    std::uint64_t ownerEpoch = 0;
    std::uint64_t sequence = 0;
    std::uint64_t objectId = 0;
    std::uint64_t requestedBytes = 0;
    std::uint32_t fileIndex = 0;
    std::size_t formatCount = 0;
    std::string canonicalFormat;
};

struct ClipboardRuntimePolicyDecision
{
    bool allowed = true;
    bool auditRequired = false;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Ok;
    std::string reason;

    static ClipboardRuntimePolicyDecision allow(bool audit = false,
                                                std::string reason = "allowed");
    static ClipboardRuntimePolicyDecision deny(protocol::ResponseStatus status,
                                               std::string reason);
};

struct ClipboardRuntimeAuditEvent
{
    ClipboardRuntimePolicyContext context;
    bool allowed = true;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Ok;
    std::string reason;
};

class IClipboardRuntimePolicy
{
public:
    virtual ~IClipboardRuntimePolicy() = default;

    virtual ClipboardRuntimePolicyDecision authorize(
        const ClipboardRuntimePolicyContext& context) = 0;
    virtual void audit(const ClipboardRuntimeAuditEvent& event) = 0;
};

class AllowAllClipboardRuntimePolicy : public IClipboardRuntimePolicy
{
public:
    ClipboardRuntimePolicyDecision authorize(
        const ClipboardRuntimePolicyContext& context) override;
    void audit(const ClipboardRuntimeAuditEvent& event) override;

    const std::vector<ClipboardRuntimeAuditEvent>& auditEvents() const;

private:
    std::vector<ClipboardRuntimeAuditEvent> auditEvents_;
};

const char* clipboardRuntimeOperationName(ClipboardRuntimeOperation operation);

struct ClipboardRuntimePolicyRules
{
    bool auditAllowed = false;
    std::size_t maxRecentAuditEvents = 16;
    bool allowLocalSnapshotAnnounce = true;
    bool allowRemoteFormatRead = true;
    bool allowRemoteFileRangeRead = true;
    bool allowRemoteObjectLock = true;
    bool allowRemoteObjectUnlock = true;
    bool allowPendingReadExpiry = true;
    std::string denialReason = "clipboard_runtime_policy_denied";
};

struct ClipboardRuntimePolicySnapshot
{
    std::uint64_t authorizeCalls = 0;
    std::uint64_t allowed = 0;
    std::uint64_t denied = 0;
    std::uint64_t auditEvents = 0;
    std::uint64_t auditedAllowed = 0;
    std::uint64_t auditedDenied = 0;
    ClipboardRuntimeOperation lastOperation =
        ClipboardRuntimeOperation::LocalSnapshotAnnounce;
    bool lastAllowed = true;
    protocol::ResponseStatus lastStatus = protocol::ResponseStatus::Ok;
    std::uint64_t lastBundleId = 0;
    std::uint64_t lastOfferId = 0;
    std::uint64_t lastOwnerEpoch = 0;
    std::uint64_t lastObjectId = 0;
    std::uint64_t lastRequestedBytes = 0;
    std::uint32_t lastFileIndex = 0;
    std::string lastCanonicalFormat;
    std::string lastReason;
    std::vector<ClipboardRuntimeAuditEvent> recentAuditEvents;
};

class ConfigurableClipboardRuntimePolicy final
    : public IClipboardRuntimePolicy
{
public:
    explicit ConfigurableClipboardRuntimePolicy(
        ClipboardRuntimePolicyRules rules = {});

    ClipboardRuntimePolicyDecision authorize(
        const ClipboardRuntimePolicyContext& context) override;
    void audit(const ClipboardRuntimeAuditEvent& event) override;

    const ClipboardRuntimePolicyRules& rules() const;
    ClipboardRuntimePolicySnapshot snapshot() const;

private:
    bool operationAllowed(ClipboardRuntimeOperation operation) const;

private:
    ClipboardRuntimePolicyRules rules_;
    ClipboardRuntimePolicySnapshot snapshot_;
};

} // namespace feature
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_RUNTIME_POLICY_H
