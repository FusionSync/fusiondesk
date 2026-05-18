#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_DISPATCH_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_DISPATCH_H

#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_ack.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ReconnectTeardownDispatchOptions
{
    ReconnectTeardownWireOptions wire;
    bool assignMissingMessageIds = true;
};

struct ReconnectTeardownDispatchResult
{
    bool ok = false;
    std::vector<protocol::PacketEnvelope> sentRequests;
    std::vector<std::string> messages;
    ReconnectTeardownResponseSummary summary;
};

class ReconnectTeardownDispatcher
{
public:
    ReconnectTeardownDispatcher(network::INetworkRouter& router,
                                network::IRequestTracker& requestTracker);

    ReconnectTeardownDispatchResult dispatch(const ReconnectTeardownPlan& plan,
                                             const ReconnectTeardownDispatchOptions& options = {});

    bool complete(const protocol::PacketEnvelope& response);

    ReconnectTeardownResponseSummary summary() const;
    const std::vector<ReconnectTeardownResponse>& terminalResponses() const;
    const std::vector<ReconnectTeardownResponse>& interimResponses() const;
    const ReconnectTeardownPlan& activePlan() const;

private:
    static bool terminalMessageKind(protocol::MessageKind messageKind);
    static protocol::ResponseStatus sendStatusToResponseStatus(network::SendStatus status);

    void recordTrackedResponse(const ReconnectTeardownCommand& command,
                               const protocol::PacketEnvelope& response);
    void failTrackedRequest(const protocol::PacketEnvelope& request,
                            protocol::ResponseStatus status,
                            const std::string& message);

private:
    network::INetworkRouter& router_;
    network::IRequestTracker& requestTracker_;
    ReconnectTeardownPlan activePlan_;
    std::vector<ReconnectTeardownResponse> terminalResponses_;
    std::vector<ReconnectTeardownResponse> interimResponses_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_DISPATCH_H
