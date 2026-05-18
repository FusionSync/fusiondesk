#ifndef FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_PRODUCT_PRESENTER_H
#define FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_PRODUCT_PRESENTER_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/runtime/feature/clipboard_product_policy.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

enum class ClipboardProductHealthLevel
{
    Unknown,
    Ok,
    Warning,
    Degraded,
    Blocked
};

struct ClipboardProductHealthPresentation
{
    bool usable = false;
    ClipboardProductHealthLevel health = ClipboardProductHealthLevel::Unknown;
    std::string healthName;
    std::string statusCode;
    std::string primaryActionCode;
    std::string runtimeState;
    std::string policyState;
    bool showPolicyDenialWarning = false;
    bool showAuditIndicator = false;
    bool showTransferWarning = false;
    std::vector<std::string> detailMessages;
};

struct ClipboardProductPolicyPresentation
{
    bool usable = false;
    std::string modeCode;
    std::string primaryActionCode;
    std::string directionState;
    std::string contentState;
    std::string fileState;
    std::string dragState;
    std::string customFormatState;
    std::string runtimeState;
    std::string auditState;
    bool allowPlainText = false;
    bool allowRichText = false;
    bool allowImage = false;
    bool allowFiles = false;
    bool allowFileContents = false;
    bool allowDrag = false;
    bool allowCustomFormats = false;
    bool auditEnabled = false;
    bool showRestrictionWarning = false;
    bool showFileTransferWarning = false;
    bool showAuditIndicator = false;
    std::uint64_t maxInlineBytes = 0;
    std::uint64_t maxFileRangeBytes = 0;
    std::uint64_t maxFileCount = 0;
    std::uint64_t maxSingleFileBytes = 0;
    std::vector<std::string> detailMessages;
};

const char* clipboardProductHealthLevelName(
    ClipboardProductHealthLevel level);

ClipboardProductHealthPresentation
buildClipboardProductHealthPresentation(
    const ClipboardRuntimeServiceSnapshot& runtime,
    const ClipboardRuntimePolicySnapshot& policy = {});

ClipboardProductPolicyPresentation
buildClipboardProductPolicyPresentation(
    const ProductClipboardPolicy& policy);

} // namespace feature
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_FEATURE_CLIPBOARD_PRODUCT_PRESENTER_H
