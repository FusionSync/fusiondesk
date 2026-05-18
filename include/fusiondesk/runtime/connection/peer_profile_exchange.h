#ifndef FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_EXCHANGE_H
#define FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_EXCHANGE_H

#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/runtime/connection/peer_connection_plan.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct PeerProfileConnectChannel
{
    network::ChannelSpec spec;
    std::string endpoint;
    std::string readyEndpoint;
};

struct PeerProfileListenChannel
{
    network::ChannelSpec spec;
    std::string endpoint;
    std::string readyEndpoint;
};

struct PeerProfileSide
{
    protocol::SessionId sessionId = 0;
    std::vector<PeerProfileConnectChannel> tcpChannels;
    std::vector<PeerProfileListenChannel> tcpListenChannels;
};

struct PeerProfilePair
{
    PeerProfileSide client;
    PeerProfileSide agent;
};

struct PeerProfileExtension
{
    std::string key;
    protocol::ByteBuffer payload;
};

struct PeerProfileExchangeRequest
{
    PeerConnectionPlanRequest connectionPlan;
    protocol::SessionId clientSessionId = 0;
    protocol::SessionId agentSessionId = 0;
    std::vector<PeerProfileExtension> extensions;
};

struct PeerProfileExchangeResult
{
    bool ok = false;
    PeerProfilePair pair;
    std::vector<PeerProfileExtension> extensions;
    std::vector<std::string> messages;
};

PeerProfileExchangeResult resolvePeerProfileExchange(const PeerProfileExchangeRequest& request);

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_EXCHANGE_H
