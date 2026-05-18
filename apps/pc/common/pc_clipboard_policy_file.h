#ifndef FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_POLICY_FILE_H
#define FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_POLICY_FILE_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/feature/clipboard_product_policy.h"

namespace fusiondesk {
namespace apps {
namespace pc {

struct PcClipboardPolicyFileLoadResult
{
    bool ok = false;
    bool loaded = false;
    runtime::ProductClipboardPolicy policy;
    std::vector<std::string> messages;
};

struct PcClipboardPolicyFileSaveResult
{
    bool ok = false;
    std::vector<std::string> messages;
};

PcClipboardPolicyFileLoadResult loadClipboardProductPolicyFromJsonFile(
    const std::string& path,
    runtime::ProductClipboardPolicy basePolicy = {});

std::string clipboardProductPolicyToJson(
    runtime::ProductClipboardPolicy policy,
    bool compact = false);

PcClipboardPolicyFileSaveResult saveClipboardProductPolicyToJsonFile(
    const std::string& path,
    runtime::ProductClipboardPolicy policy);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_POLICY_FILE_H
