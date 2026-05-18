#ifndef FUSIONDESK_POLICY_POLICY_ENGINE_H
#define FUSIONDESK_POLICY_POLICY_ENGINE_H

#include "fusiondesk/core/module/module_manifest.h"
#include "fusiondesk/core/policy/policy_types.h"

namespace fusiondesk {
namespace policy {

class IPolicyEngine
{
public:
    virtual ~IPolicyEngine() = default;

    virtual protocol::FeatureSet authorizeFeatures(const PolicyContext& context,
                                                   protocol::FeatureSet requested) const = 0;
    virtual PolicyDecision authorizeModule(const PolicyContext& context,
                                           const module::ModuleManifest& manifest) const = 0;
};

class StaticPolicyEngine : public IPolicyEngine
{
public:
    explicit StaticPolicyEngine(protocol::FeatureSet licensedFeatures);

    void setPolicyFeatures(protocol::FeatureSet policyFeatures);
    protocol::FeatureSet authorizeFeatures(const PolicyContext& context,
                                           protocol::FeatureSet requested) const override;
    PolicyDecision authorizeModule(const PolicyContext& context,
                                   const module::ModuleManifest& manifest) const override;

private:
    protocol::FeatureSet licensedFeatures_;
    protocol::FeatureSet policyFeatures_;
};

} // namespace policy
} // namespace fusiondesk

#endif // FUSIONDESK_POLICY_POLICY_ENGINE_H
