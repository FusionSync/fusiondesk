#include "fusiondesk/runtime/tunnel/tunnel_candidate_profile.h"

#include <array>
#include <cstddef>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace tunnel {

namespace {

void appendFailure(TunnelCandidateNegotiationResult& result,
                   std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

bool sameChannel(network::ChannelKey lhs, network::ChannelKey rhs)
{
    return lhs.channelId == rhs.channelId &&
           lhs.channelType == rhs.channelType;
}

bool sameMode(TunnelTransportMode lhs, TunnelTransportMode rhs)
{
    return lhs == rhs;
}

bool modeAllowed(TunnelTransportMode mode,
                 const TunnelReconnectExecutorOptions& options)
{
    switch (mode) {
    case TunnelTransportMode::LanTcp:
        return options.preferredMode == TunnelTransportMode::LanTcp ||
               options.allowLanTcpFallback;
    case TunnelTransportMode::Relay:
        return options.preferredMode == TunnelTransportMode::Relay ||
               options.allowRelayFallback;
    case TunnelTransportMode::DirectP2P:
        return options.allowDirectP2P;
    }
    return false;
}

bool candidateAllowed(const TunnelReplacementCandidate& candidate,
                      TunnelTransportMode mode,
                      bool listener,
                      const TunnelReconnectExecutorOptions& options)
{
    return candidate.listener == listener &&
           sameMode(candidate.mode, mode) &&
           modeAllowed(mode, options) &&
           !candidate.endpoint.empty() &&
           (!options.requireEncryptedTransport || candidate.encrypted);
}

std::array<TunnelTransportMode, 3> modeOrder(
    const TunnelReconnectExecutorOptions& options)
{
    if (options.preferredMode == TunnelTransportMode::DirectP2P) {
        return {TunnelTransportMode::DirectP2P,
                TunnelTransportMode::Relay,
                TunnelTransportMode::LanTcp};
    }
    if (options.preferredMode == TunnelTransportMode::Relay) {
        return {TunnelTransportMode::Relay,
                TunnelTransportMode::DirectP2P,
                TunnelTransportMode::LanTcp};
    }
    return {TunnelTransportMode::LanTcp,
            TunnelTransportMode::DirectP2P,
            TunnelTransportMode::Relay};
}

const TunnelReplacementCandidate* findCandidate(
    const std::vector<TunnelReplacementCandidate>& candidates,
    network::ChannelKey key,
    TunnelTransportMode mode,
    bool listener,
    const TunnelReconnectExecutorOptions& options)
{
    for (const TunnelReplacementCandidate& candidate : candidates) {
        if (sameChannel(candidate.spec.key, key) &&
            candidateAllowed(candidate, mode, listener, options)) {
            return &candidate;
        }
    }
    return nullptr;
}

bool hasDuplicateRequestedChannels(
    const std::vector<network::ChannelKey>& requested)
{
    for (std::size_t i = 0; i < requested.size(); ++i) {
        for (std::size_t j = i + 1; j < requested.size(); ++j) {
            if (sameChannel(requested[i], requested[j]))
                return true;
        }
    }
    return false;
}

} // namespace

TunnelCandidateProfile tunnelCandidateProfileFromReplacementRequest(
    const TunnelReplacementRequest& request)
{
    TunnelCandidateProfile profile;
    profile.sessionId = request.sessionId;
    profile.candidates = request.candidates;
    return profile;
}

TunnelCandidateNegotiationResult negotiateTunnelCandidates(
    const TunnelCandidateNegotiationRequest& request)
{
    TunnelCandidateNegotiationResult result;

    if (request.client.sessionId == 0)
        appendFailure(result, "tunnel candidate negotiation requires client session id");
    if (request.agent.sessionId == 0)
        appendFailure(result, "tunnel candidate negotiation requires agent session id");
    if (request.requestedChannels.empty())
        appendFailure(result, "tunnel candidate negotiation requires requested channels");
    if (hasDuplicateRequestedChannels(request.requestedChannels))
        appendFailure(result, "tunnel candidate negotiation has duplicate requested channels");
    if (!result.messages.empty())
        return result;

    const std::array<TunnelTransportMode, 3> modes =
        modeOrder(request.options);
    for (network::ChannelKey key : request.requestedChannels) {
        TunnelCandidateSelection selection;
        bool selected = false;
        for (TunnelTransportMode mode : modes) {
            const TunnelReplacementCandidate* client =
                findCandidate(request.client.candidates,
                              key,
                              mode,
                              false,
                              request.options);
            const TunnelReplacementCandidate* agent =
                findCandidate(request.agent.candidates,
                              key,
                              mode,
                              true,
                              request.options);
            if (client == nullptr || agent == nullptr)
                continue;

            selection.key = key;
            selection.mode = mode;
            selection.client = *client;
            selection.agent = *agent;
            selected = true;
            break;
        }

        if (!selected) {
            appendFailure(result, "tunnel candidate negotiation has no compatible candidate pair");
            return result;
        }
        result.selections.push_back(std::move(selection));
    }

    result.ok = true;
    return result;
}

TunnelTransportFactoryRequest makeTunnelTransportFactoryRequest(
    const TunnelReplacementRequest& request,
    const TunnelReplacementCandidate& candidate)
{
    TunnelTransportFactoryRequest factoryRequest;
    factoryRequest.side = request.side;
    factoryRequest.sessionId = request.sessionId;
    factoryRequest.candidate = candidate;
    factoryRequest.reason = request.reason;
    factoryRequest.requestDisplayKeyframe = request.requestDisplayKeyframe;
    return factoryRequest;
}

TunnelTransportFactoryRequest makeTunnelTransportFactoryRequest(
    const TunnelReplacementRequest& request,
    const TunnelCandidateSelection& selection)
{
    TunnelTransportFactoryRequest factoryRequest;
    factoryRequest.side = request.side;
    factoryRequest.sessionId = request.sessionId;
    if (request.side == TunnelReplacementSide::Agent) {
        factoryRequest.candidate = selection.agent;
        factoryRequest.peerCandidate = selection.client;
    } else {
        factoryRequest.candidate = selection.client;
        factoryRequest.peerCandidate = selection.agent;
    }
    factoryRequest.hasPeerCandidate = true;
    factoryRequest.reason = request.reason;
    factoryRequest.requestDisplayKeyframe = request.requestDisplayKeyframe;
    return factoryRequest;
}

} // namespace tunnel
} // namespace runtime
} // namespace fusiondesk
