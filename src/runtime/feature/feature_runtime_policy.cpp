#include "fusiondesk/runtime/feature/feature_runtime_policy.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace feature {

FeatureRuntimePolicyDecision FeatureRuntimePolicyDecision::allow(bool audit,
                                                                 std::string reason)
{
    FeatureRuntimePolicyDecision decision;
    decision.allowed = true;
    decision.auditRequired = audit;
    decision.responseStatus = protocol::ResponseStatus::Ok;
    decision.reason = std::move(reason);
    return decision;
}

FeatureRuntimePolicyDecision FeatureRuntimePolicyDecision::deny(
    protocol::ResponseStatus status,
    std::string reason)
{
    FeatureRuntimePolicyDecision decision;
    decision.allowed = false;
    decision.auditRequired = true;
    decision.responseStatus = status;
    decision.reason = std::move(reason);
    return decision;
}

FeatureRuntimePolicyDecision AllowAllFeatureRuntimePolicy::authorize(
    const FeatureRuntimePolicyContext& context)
{
    (void)context;
    return FeatureRuntimePolicyDecision::allow(false);
}

void AllowAllFeatureRuntimePolicy::audit(const FeatureRuntimeAuditEvent& event)
{
    auditEvents_.push_back(event);
}

const std::vector<FeatureRuntimeAuditEvent>& AllowAllFeatureRuntimePolicy::auditEvents() const
{
    return auditEvents_;
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
