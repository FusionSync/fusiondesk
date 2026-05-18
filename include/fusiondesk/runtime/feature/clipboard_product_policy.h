#ifndef FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_PRODUCT_POLICY_H
#define FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_PRODUCT_POLICY_H

#include <memory>

#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_policy.h"

namespace fusiondesk {
namespace runtime {

struct ProductClipboardPolicy
{
    modules::clipboard::ClipboardPolicy modulePolicy;
    feature::ClipboardRuntimePolicyRules runtimeRules;
};

namespace feature {

modules::clipboard::ClipboardPolicy clipboardModulePolicyFromProductPolicy(
    const ProductClipboardPolicy& policy);

ClipboardRuntimePolicyRules clipboardRuntimePolicyRulesFromProductPolicy(
    const ProductClipboardPolicy& policy);

std::shared_ptr<IClipboardRuntimePolicy> makeClipboardRuntimePolicyFromProductPolicy(
    const ProductClipboardPolicy& policy);

} // namespace feature
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_PRODUCT_POLICY_H
