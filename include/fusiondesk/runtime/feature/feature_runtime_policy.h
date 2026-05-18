#ifndef FUSIONDESK_RUNTIME_FEATURE_FEATURE_RUNTIME_POLICY_H
#define FUSIONDESK_RUNTIME_FEATURE_FEATURE_RUNTIME_POLICY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/core/session/session_context.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

enum class FeatureRuntimeOperation
{
    InputMouseEvent,
    InputKeyboardEvent
};

struct FeatureRuntimePolicyContext
{
    protocol::SessionId sessionId = 0;
    protocol::TraceId traceId = 0;
    session::SessionRole role = session::SessionRole::Standalone;
    std::string moduleId;
    std::string policyVersion;
    FeatureRuntimeOperation operation = FeatureRuntimeOperation::InputMouseEvent;
    std::uint64_t sequence = 0;
};

struct FeatureRuntimePolicyDecision
{
    bool allowed = true;
    bool auditRequired = false;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Ok;
    std::string reason;

    static FeatureRuntimePolicyDecision allow(bool audit = false,
                                              std::string reason = "allowed");
    static FeatureRuntimePolicyDecision deny(protocol::ResponseStatus status,
                                             std::string reason);
};

struct FeatureRuntimeAuditEvent
{
    FeatureRuntimePolicyContext context;
    bool allowed = true;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Ok;
    std::string reason;
};

class IFeatureRuntimePolicy
{
public:
    virtual ~IFeatureRuntimePolicy() = default;

    virtual FeatureRuntimePolicyDecision authorize(
        const FeatureRuntimePolicyContext& context) = 0;
    virtual void audit(const FeatureRuntimeAuditEvent& event) = 0;
};

class AllowAllFeatureRuntimePolicy : public IFeatureRuntimePolicy
{
public:
    FeatureRuntimePolicyDecision authorize(
        const FeatureRuntimePolicyContext& context) override;
    void audit(const FeatureRuntimeAuditEvent& event) override;

    const std::vector<FeatureRuntimeAuditEvent>& auditEvents() const;

private:
    std::vector<FeatureRuntimeAuditEvent> auditEvents_;
};

} // namespace feature
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_FEATURE_FEATURE_RUNTIME_POLICY_H
