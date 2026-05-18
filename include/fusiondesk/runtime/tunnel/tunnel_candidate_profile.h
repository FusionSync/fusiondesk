#ifndef FUSIONDESK_RUNTIME_TUNNEL_TUNNEL_CANDIDATE_PROFILE_H
#define FUSIONDESK_RUNTIME_TUNNEL_TUNNEL_CANDIDATE_PROFILE_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel.h"
#include "fusiondesk/runtime/tunnel/tunnel_reconnect_executor.h"

namespace fusiondesk {
namespace runtime {
namespace tunnel {

struct TunnelCandidateProfile
{
    protocol::SessionId sessionId = 0;
    std::vector<TunnelReplacementCandidate> candidates;
};

struct TunnelCandidateNegotiationRequest
{
    TunnelCandidateProfile client;
    TunnelCandidateProfile agent;
    std::vector<network::ChannelKey> requestedChannels;
    TunnelReconnectExecutorOptions options;
};

struct TunnelCandidateSelection
{
    network::ChannelKey key;
    TunnelTransportMode mode = TunnelTransportMode::LanTcp;
    TunnelReplacementCandidate client;
    TunnelReplacementCandidate agent;
};

struct TunnelCandidateNegotiationResult
{
    bool ok = false;
    std::vector<TunnelCandidateSelection> selections;
    std::vector<std::string> messages;
};

struct TunnelTransportFactoryRequest
{
    TunnelReplacementSide side = TunnelReplacementSide::Client;
    protocol::SessionId sessionId = 0;
    TunnelReplacementCandidate candidate;
    bool hasPeerCandidate = false;
    TunnelReplacementCandidate peerCandidate;
    std::string reason;
    bool requestDisplayKeyframe = true;
};

struct TunnelTransportFactoryResult
{
    bool ok = false;
    std::vector<network::ChannelKey> preparedChannels;
    std::vector<std::unique_ptr<network::IChannel>> channels;
    std::vector<std::string> messages;
};

class ITunnelTransportFactory
{
public:
    virtual ~ITunnelTransportFactory() = default;

    virtual TunnelTransportFactoryResult prepareClientChannel(
        const TunnelTransportFactoryRequest& request) = 0;
    virtual TunnelTransportFactoryResult prepareAgentListener(
        const TunnelTransportFactoryRequest& request) = 0;
};

TunnelCandidateProfile tunnelCandidateProfileFromReplacementRequest(
    const TunnelReplacementRequest& request);

TunnelCandidateNegotiationResult negotiateTunnelCandidates(
    const TunnelCandidateNegotiationRequest& request);

TunnelTransportFactoryRequest makeTunnelTransportFactoryRequest(
    const TunnelReplacementRequest& request,
    const TunnelReplacementCandidate& candidate);

TunnelTransportFactoryRequest makeTunnelTransportFactoryRequest(
    const TunnelReplacementRequest& request,
    const TunnelCandidateSelection& selection);

} // namespace tunnel
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_TUNNEL_TUNNEL_CANDIDATE_PROFILE_H
