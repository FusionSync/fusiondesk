#include "fusiondesk/runtime/feature/clipboard_product_policy.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

modules::clipboard::ClipboardPolicy clipboardModulePolicyFromProductPolicy(
    const ProductClipboardPolicy& policy)
{
    return policy.modulePolicy;
}

ClipboardRuntimePolicyRules clipboardRuntimePolicyRulesFromProductPolicy(
    const ProductClipboardPolicy& policy)
{
    ClipboardRuntimePolicyRules rules = policy.runtimeRules;
    if (!policy.modulePolicy.allowFileContents) {
        rules.allowRemoteFileRangeRead = false;
        rules.allowRemoteObjectLock = false;
        rules.allowRemoteObjectUnlock = false;
    }
    return rules;
}

std::shared_ptr<IClipboardRuntimePolicy> makeClipboardRuntimePolicyFromProductPolicy(
    const ProductClipboardPolicy& policy)
{
    return std::make_shared<ConfigurableClipboardRuntimePolicy>(
        clipboardRuntimePolicyRulesFromProductPolicy(policy));
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
