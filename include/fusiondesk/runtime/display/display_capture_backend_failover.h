#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_FAILOVER_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_FAILOVER_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/runtime/display/display_capture_backend_factory.h"

namespace fusiondesk {
namespace runtime {
namespace display {

struct DisplayCaptureBackendFailoverRequest
{
    DisplayCaptureBackendSelectionRequest selectionRequest;
    std::string failedAdapterId;
    DisplayCaptureBackendKind failedBackend = DisplayCaptureBackendKind::Unknown;
    bool honorRequestedAdapter = true;
};

struct DisplayCaptureBackendFailoverResult
{
    bool ok = false;
    bool blockedByRequestedAdapter = false;
    DisplayCaptureBackendSelectionResult selection;
    std::vector<std::string> messages;
};

DisplayCaptureBackendFailoverResult selectDisplayCaptureBackendFailover(
    const DisplayCaptureBackendFailoverRequest& request);

DisplayCaptureBackendCreateResult createFailoverDisplayCapture(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendFailoverRequest& request);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_CAPTURE_BACKEND_FAILOVER_H
