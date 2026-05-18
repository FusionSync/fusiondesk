#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_SERVICE_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_dispatch.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_handler.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ReconnectTeardownServiceOptions
{
    ReconnectTeardownHandlerOptions handler;
    bool startPeerHandler = true;
    bool subscribeResponses = true;
};

struct ReconnectTeardownServiceStartResult
{
    bool ok = false;
    ReconnectTeardownHandlerStartResult handler;
    std::vector<network::SubscriptionToken> responseTokens;
    std::vector<std::string> messages;
};

struct ReconnectTeardownServiceSnapshot
{
    bool active = false;
    std::size_t pendingRequests = 0;
    std::size_t terminalResponses = 0;
    std::size_t interimResponses = 0;
    ReconnectTeardownResponseSummary summary;
    ReconnectTeardownHandlerSnapshot handler;
    std::vector<network::PendingRequestSnapshot> pending;
    std::vector<std::string> messages;
};

class ReconnectTeardownService
{
public:
    ReconnectTeardownService(network::INetworkRouter& router,
                             network::IRequestTracker& requestTracker,
                             IReconnectTeardownCloseTarget& closeTarget);
    ~ReconnectTeardownService();

    ReconnectTeardownServiceStartResult start(
        const ReconnectTeardownServiceOptions& options = {});
    void stop();
    bool active() const;

    ReconnectTeardownDispatchResult dispatch(
        const ReconnectTeardownPlan& plan,
        const ReconnectTeardownDispatchOptions& options = {});
    ReconnectTeardownDispatchResult dispatch(
        const ReconnectOrchestrationSidePlan& sidePlan,
        std::uint32_t timeoutMs,
        const ReconnectTeardownDispatchOptions& options = {});

    bool complete(const protocol::PacketEnvelope& response);
    std::size_t expire(std::uint64_t nowUsec);

    ReconnectTeardownServiceSnapshot snapshot() const;

private:
    static network::ChannelKey normalizedControlChannel(network::ChannelKey requested);
    static network::RouteMatch responseRoute(network::ChannelKey controlChannel,
                                             protocol::MessageKind messageKind);
    static bool responseMessageKind(protocol::MessageKind messageKind);

    bool subscribeResponse(protocol::MessageKind messageKind,
                           network::ChannelKey controlChannel,
                           ReconnectTeardownServiceStartResult& result);
    void clearResponseSubscriptions();

private:
    network::INetworkRouter& router_;
    network::IRequestTracker& requestTracker_;
    ReconnectTeardownDispatcher dispatcher_;
    ReconnectTeardownHandler handler_;
    bool active_ = false;
    std::vector<network::SubscriptionToken> responseTokens_;
    std::vector<std::string> messages_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_SERVICE_H
