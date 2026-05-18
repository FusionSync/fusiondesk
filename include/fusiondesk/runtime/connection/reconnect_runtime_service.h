#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_RUNTIME_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/runtime/connection/reconnect_coordinator.h"
#include "fusiondesk/runtime/connection/reconnect_diagnostics_report.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ReconnectRuntimeServiceStartOptions
{
    // If disabled, callers should also disable coordinator teardown dispatch.
    bool startTeardownService = true;
    ReconnectTeardownServiceOptions teardown;
};

struct ReconnectRuntimeServiceStartResult
{
    bool ok = false;
    ReconnectTeardownServiceStartResult teardown;
    std::vector<std::string> messages;
};

struct ReconnectRuntimeServiceSnapshot
{
    bool active = false;
    ReconnectTeardownServiceSnapshot teardown;
    std::vector<network::PendingRequestSnapshot> pendingRequests;
    ReconnectDiagnosticsReport diagnostics;
    std::vector<std::string> messages;
};

class ReconnectRuntimeService
{
public:
    ReconnectRuntimeService(IReconnectReplacementExecutor& replacementExecutor,
                            network::INetworkRouter& router,
                            IReconnectTeardownCloseTarget& closeTarget,
                            protocol::MessageId firstMessageId = 1);
    ~ReconnectRuntimeService();

    ReconnectRuntimeService(const ReconnectRuntimeService&) = delete;
    ReconnectRuntimeService& operator=(const ReconnectRuntimeService&) = delete;

    ReconnectRuntimeServiceStartResult start(
        const ReconnectRuntimeServiceStartOptions& options = {});
    void stop();
    bool active() const;

    ReconnectCoordinatorRunResult run(
        const ReconnectOrchestrationRequest& request,
        const ReconnectCoordinatorOptions& options = {});
    std::size_t expire(std::uint64_t nowUsec);

    ReconnectRuntimeServiceSnapshot snapshot() const;

private:
    std::size_t cancelPending(protocol::ResponseStatus status);
    void remember(const std::vector<std::string>& messages);

private:
    network::RequestTracker requestTracker_;
    ReconnectTeardownService teardownService_;
    ReconnectTeardownServiceExecutor teardownExecutor_;
    ReconnectCoordinator coordinator_;
    ReconnectCoordinatorRunResult lastRun_;
    bool hasLastRun_ = false;
    std::size_t expiredRequests_ = 0;
    bool active_ = false;
    std::vector<std::string> messages_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_RUNTIME_SERVICE_H
