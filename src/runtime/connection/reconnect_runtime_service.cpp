#include "fusiondesk/runtime/connection/reconnect_runtime_service.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(ReconnectRuntimeServiceStartResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(ReconnectCoordinatorRunResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

} // namespace

ReconnectRuntimeService::ReconnectRuntimeService(
    IReconnectReplacementExecutor& replacementExecutor,
    network::INetworkRouter& router,
    IReconnectTeardownCloseTarget& closeTarget,
    protocol::MessageId firstMessageId)
    : requestTracker_(firstMessageId),
      teardownService_(router, requestTracker_, closeTarget),
      teardownExecutor_(teardownService_),
      coordinator_(replacementExecutor, &teardownExecutor_)
{
}

ReconnectRuntimeService::~ReconnectRuntimeService()
{
    stop();
}

ReconnectRuntimeServiceStartResult ReconnectRuntimeService::start(
    const ReconnectRuntimeServiceStartOptions& options)
{
    ReconnectRuntimeServiceStartResult result;
    if (active_) {
        appendFailure(result, "reconnect runtime service is already active");
        remember(result.messages);
        return result;
    }

    if (options.startTeardownService) {
        result.teardown = teardownService_.start(options.teardown);
        if (!result.teardown.ok) {
            result.messages.insert(result.messages.end(),
                                   result.teardown.messages.begin(),
                                   result.teardown.messages.end());
            if (result.messages.empty())
                appendFailure(result, "reconnect teardown service start failed");
            remember(result.messages);
            return result;
        }
    }

    active_ = true;
    result.ok = true;
    return result;
}

void ReconnectRuntimeService::stop()
{
    cancelPending(protocol::ResponseStatus::Cancelled);
    teardownService_.stop();
    active_ = false;
}

bool ReconnectRuntimeService::active() const
{
    return active_;
}

ReconnectCoordinatorRunResult ReconnectRuntimeService::run(
    const ReconnectOrchestrationRequest& request,
    const ReconnectCoordinatorOptions& options)
{
    ReconnectCoordinatorRunResult result;
    if (!active_) {
        appendFailure(result, "reconnect runtime service is not active");
        remember(result.messages);
        return result;
    }

    result = coordinator_.run(request, options);
    lastRun_ = result;
    hasLastRun_ = true;
    expiredRequests_ = 0;
    remember(result.messages);
    return result;
}

std::size_t ReconnectRuntimeService::expire(std::uint64_t nowUsec)
{
    if (!active_)
        return 0;

    const std::size_t expired = teardownService_.expire(nowUsec);
    expiredRequests_ += expired;
    return expired;
}

ReconnectRuntimeServiceSnapshot ReconnectRuntimeService::snapshot() const
{
    ReconnectRuntimeServiceSnapshot result;
    result.active = active_;
    result.teardown = teardownService_.snapshot();
    result.pendingRequests = result.teardown.pending;
    ReconnectDiagnosticsInput diagnostics;
    diagnostics.active = active_;
    diagnostics.hasRun = hasLastRun_;
    diagnostics.run = lastRun_;
    diagnostics.hasSessionReport = lastRun_.hasSessionReport;
    diagnostics.sessionReport = lastRun_.sessionReport;
    diagnostics.teardown = result.teardown;
    diagnostics.expiredRequests = expiredRequests_;
    diagnostics.runtimeMessages = messages_;
    result.diagnostics = buildReconnectDiagnosticsReport(diagnostics);
    result.messages = messages_;
    return result;
}

std::size_t ReconnectRuntimeService::cancelPending(protocol::ResponseStatus status)
{
    std::size_t cancelled = 0;
    const std::vector<network::PendingRequestSnapshot> pending =
        requestTracker_.snapshots();
    for (const network::PendingRequestSnapshot& request : pending) {
        cancelled += requestTracker_.cancelByChannel(request.channelId,
                                                     request.channelType,
                                                     status);
    }
    return cancelled;
}

void ReconnectRuntimeService::remember(const std::vector<std::string>& messages)
{
    messages_.insert(messages_.end(), messages.begin(), messages.end());
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
