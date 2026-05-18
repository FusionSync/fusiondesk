#ifndef FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_RUNTIME_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/runtime/connection/peer_profile_service.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct PeerProfileRuntimeServiceStartOptions
{
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    bool startResponder = true;
    bool subscribeResponses = true;
    PeerProfileServiceStartOptions responder;
};

struct PeerProfileRuntimeServiceStartResult
{
    bool ok = false;
    PeerProfileServiceStartResult responder;
    std::vector<network::SubscriptionToken> responseTokens;
    std::vector<std::string> messages;
};

struct PeerProfileRuntimeExchangeOptions
{
    PeerProfileServiceWireOptions wire;
    bool assignMissingMessageId = true;
};

struct PeerProfileRuntimeDispatchResult
{
    bool ok = false;
    protocol::PacketEnvelope request;
    std::vector<std::string> messages;
};

struct PeerProfileRuntimeCompletion
{
    bool terminal = false;
    bool ok = false;
    protocol::PacketEnvelope response;
    PeerProfileExchangeResult exchange;
    std::vector<std::string> messages;
};

struct PeerProfileRuntimeServiceSnapshot
{
    bool active = false;
    PeerProfileServiceSnapshot responder;
    std::size_t pendingRequests = 0;
    std::size_t interimResponses = 0;
    std::size_t completedResponses = 0;
    std::size_t expiredRequests = 0;
    std::vector<network::PendingRequestSnapshot> pending;
    std::vector<protocol::PacketEnvelope> interimPackets;
    std::vector<PeerProfileRuntimeCompletion> completions;
    std::vector<std::string> messages;
};

class PeerProfileRuntimeService
{
public:
    explicit PeerProfileRuntimeService(network::INetworkRouter& router,
                                       protocol::MessageId firstMessageId = 1);
    ~PeerProfileRuntimeService();

    PeerProfileRuntimeService(const PeerProfileRuntimeService&) = delete;
    PeerProfileRuntimeService& operator=(const PeerProfileRuntimeService&) = delete;

    PeerProfileRuntimeServiceStartResult start(
        const PeerProfileRuntimeServiceStartOptions& options = {});
    void stop();
    bool active() const;

    PeerProfileRuntimeDispatchResult requestPeerProfile(
        const PeerProfileExchangeRequest& request,
        const PeerProfileRuntimeExchangeOptions& options = {});
    bool complete(const protocol::PacketEnvelope& response);
    std::size_t expire(std::uint64_t nowUsec);

    PeerProfileRuntimeServiceSnapshot snapshot() const;

private:
    static network::ChannelKey defaultControlChannel();
    static network::ChannelKey normalizedControlChannel(network::ChannelKey requested);
    static network::RouteMatch responseRoute(network::ChannelKey controlChannel,
                                             protocol::MessageKind messageKind);
    static bool responseMessageKind(protocol::MessageKind messageKind);
    static bool terminalMessageKind(protocol::MessageKind messageKind);
    static protocol::ResponseStatus sendStatusToResponseStatus(network::SendStatus status);

    bool subscribeResponse(protocol::MessageKind messageKind,
                           PeerProfileRuntimeServiceStartResult& result);
    void clearResponseSubscriptions();
    void recordResponse(const protocol::PacketEnvelope& response);
    void failTrackedRequest(const protocol::PacketEnvelope& request,
                            protocol::ResponseStatus status,
                            const std::string& message);
    void remember(const std::vector<std::string>& messages);
    void remember(std::string message);

private:
    network::INetworkRouter& router_;
    network::RequestTracker requestTracker_;
    PeerProfileService responder_;
    network::ChannelKey controlChannel_{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    bool active_ = false;
    std::size_t expiredRequests_ = 0;
    std::vector<network::SubscriptionToken> responseTokens_;
    std::vector<protocol::PacketEnvelope> interimResponses_;
    std::vector<PeerProfileRuntimeCompletion> completions_;
    std::vector<std::string> messages_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_RUNTIME_SERVICE_H
