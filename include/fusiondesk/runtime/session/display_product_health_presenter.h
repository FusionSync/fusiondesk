#ifndef FUSIONDESK_RUNTIME_SESSION_DISPLAY_PRODUCT_HEALTH_PRESENTER_H
#define FUSIONDESK_RUNTIME_SESSION_DISPLAY_PRODUCT_HEALTH_PRESENTER_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/session/session_runtime_diagnostics.h"

namespace fusiondesk {
namespace runtime {

struct DisplayProductHealthPresentation
{
    bool usable = false;
    DisplayProductHealthLevel health = DisplayProductHealthLevel::Unknown;
    std::string healthName;
    std::string statusCode;
    std::string primaryActionCode;
    std::string captureState;
    std::string codecState;
    bool showCodecFallbackWarning = false;
    bool showCodecLatencyWarning = false;
    bool showCaptureRecoveryWarning = false;
    std::vector<std::string> detailMessages;
};

DisplayProductHealthPresentation buildDisplayProductHealthPresentation(
    const DisplayProductDiagnosticsSnapshot& snapshot);

DisplayProductHealthPresentation buildDisplayProductHealthPresentation(
    const SessionRuntimeDiagnosticsSnapshot& snapshot);

} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_SESSION_DISPLAY_PRODUCT_HEALTH_PRESENTER_H
