#include "fusiondesk/core/policy/policy_engine.h"

namespace fusiondesk {
namespace policy {

StaticPolicyEngine::StaticPolicyEngine(protocol::FeatureSet licensedFeatures)
    : licensedFeatures_(licensedFeatures),
      policyFeatures_(licensedFeatures)
{
}

void StaticPolicyEngine::setPolicyFeatures(protocol::FeatureSet policyFeatures)
{
    policyFeatures_ = policyFeatures;
}

protocol::FeatureSet StaticPolicyEngine::authorizeFeatures(const PolicyContext& context,
                                                           protocol::FeatureSet requested) const
{
    (void)context;

    protocol::FeatureSet result;
    result.bits = requested.bits & licensedFeatures_.bits & policyFeatures_.bits;
    return result;
}

PolicyDecision StaticPolicyEngine::authorizeModule(const PolicyContext& context,
                                                   const module::ModuleManifest& manifest) const
{
    protocol::FeatureSet requested;
    requested.enable(manifest.feature);

    const protocol::FeatureSet effective = authorizeFeatures(context, requested);
    if (!licensedFeatures_.has(manifest.feature)) {
        return {false, DenyReason::FeatureNotLicensed, "feature is not licensed", effective};
    }

    if (!policyFeatures_.has(manifest.feature)) {
        return {false, DenyReason::FeatureDisabledByPolicy, "feature is disabled by policy", effective};
    }

    if (!effective.has(manifest.feature)) {
        return {false, DenyReason::FeatureDisabledByPolicy, "feature is not effective in this session", effective};
    }

    return {true, DenyReason::None, "allowed", effective};
}

} // namespace policy
} // namespace fusiondesk
