#include "fusiondesk/runtime/display/display_capture_backend_failover.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

bool isFailedBackend(const DisplayCaptureBackendCapability& capability,
                     const DisplayCaptureBackendFailoverRequest& request)
{
    if (!request.failedAdapterId.empty())
        return capability.adapterId == request.failedAdapterId;

    if (request.failedBackend != DisplayCaptureBackendKind::Unknown)
        return capability.backend == request.failedBackend;

    return false;
}

void addSelectionFailureMessages(DisplayCaptureBackendFailoverResult& result)
{
    for (const DisplayCaptureBackendRejection& rejection :
         result.selection.rejected) {
        std::string message = "display capture failover backend rejected: ";
        message += rejection.adapterId.empty()
                       ? displayCaptureBackendKindName(rejection.backend)
                       : rejection.adapterId;
        if (!rejection.reason.empty()) {
            message += ": ";
            message += rejection.reason;
        }
        result.messages.push_back(std::move(message));
    }
}

} // namespace

DisplayCaptureBackendFailoverResult selectDisplayCaptureBackendFailover(
    const DisplayCaptureBackendFailoverRequest& request)
{
    DisplayCaptureBackendFailoverResult result;

    if (request.failedAdapterId.empty() &&
        request.failedBackend == DisplayCaptureBackendKind::Unknown) {
        result.messages.push_back(
            "display capture failover requires a failed backend identity");
        return result;
    }

    DisplayCaptureBackendSelectionRequest selectionRequest =
        request.selectionRequest;

    std::vector<DisplayCaptureBackendCapability> candidates =
        selectionRequest.candidates;
    if (candidates.empty()) {
        candidates =
            defaultDisplayCaptureBackendCapabilities(selectionRequest.platform);
    }

    std::vector<DisplayCaptureBackendCapability> remaining;
    remaining.reserve(candidates.size());
    bool removedFailedBackend = false;
    for (const DisplayCaptureBackendCapability& candidate : candidates) {
        if (request.honorRequestedAdapter &&
            !selectionRequest.requestedAdapterId.empty() &&
            candidate.adapterId == selectionRequest.requestedAdapterId &&
            isFailedBackend(candidate, request)) {
            result.blockedByRequestedAdapter = true;
            result.messages.push_back(
                "display capture failover blocked by explicit requested adapter");
            return result;
        }

        if (isFailedBackend(candidate, request)) {
            removedFailedBackend = true;
            continue;
        }
        remaining.push_back(candidate);
    }

    if (!removedFailedBackend) {
        result.messages.push_back(
            "display capture failover did not find the failed backend");
        return result;
    }

    if (!request.honorRequestedAdapter)
        selectionRequest.requestedAdapterId.clear();

    selectionRequest.candidates = std::move(remaining);
    result.selection = selectDisplayCaptureBackend(selectionRequest);
    if (!result.selection.ok) {
        result.messages = result.selection.messages;
        addSelectionFailureMessages(result);
        if (result.messages.empty())
            result.messages.push_back(
                "display capture failover found no usable backend");
        return result;
    }

    result.ok = true;
    return result;
}

DisplayCaptureBackendCreateResult createFailoverDisplayCapture(
    const IDisplayCaptureBackendFactory& factory,
    const DisplayCaptureBackendFailoverRequest& request)
{
    DisplayCaptureBackendCreateResult result;

    DisplayCaptureBackendFailoverRequest planRequest = request;
    if (planRequest.selectionRequest.candidates.empty())
        planRequest.selectionRequest.candidates = factory.capabilities();

    const DisplayCaptureBackendFailoverResult plan =
        selectDisplayCaptureBackendFailover(planRequest);
    result.selection = plan.selection;
    result.messages = plan.messages;
    if (!plan.ok)
        return result;

    result.capture = factory.createCapture(plan.selection.selected);
    if (result.capture == nullptr) {
        result.messages.push_back(
            "display capture failover factory could not create selected backend");
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
