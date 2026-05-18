#ifndef FUSIONDESK_RUNTIME_TUNNEL_TUNNEL_RECONNECT_EXECUTOR_H
#define FUSIONDESK_RUNTIME_TUNNEL_TUNNEL_RECONNECT_EXECUTOR_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/connection/reconnect_coordinator.h"

namespace fusiondesk {
namespace runtime {
namespace tunnel {

enum class TunnelTransportMode
{
    LanTcp,
    Relay,
    DirectP2P
};

enum class TunnelReplacementSide
{
    Client,
    Agent
};

struct TunnelReconnectExecutorOptions
{
    TunnelTransportMode preferredMode = TunnelTransportMode::LanTcp;
    bool allowLanTcpFallback = true;
    bool allowRelayFallback = true;
    bool allowDirectP2P = true;
    bool requireEncryptedTransport = true;
};

struct TunnelReplacementCandidate
{
    network::ChannelSpec spec;
    std::string endpoint;
    std::string readyEndpoint;
    TunnelTransportMode mode = TunnelTransportMode::LanTcp;
    bool listener = false;
    bool encrypted = true;
};

struct TunnelReplacementRequest
{
    TunnelReplacementSide side = TunnelReplacementSide::Client;
    protocol::SessionId sessionId = 0;
    std::vector<network::ChannelKey> degradedChannels;
    std::vector<TunnelReplacementCandidate> candidates;
    std::vector<network::ChannelKey> teardownAfterSuccessfulRebind;
    std::string reason;
    bool requestDisplayKeyframe = true;
    TunnelReconnectExecutorOptions options;
};

struct TunnelReplacementRequestBuildResult
{
    bool ok = false;
    TunnelReplacementRequest request;
    std::vector<std::string> messages;
};

class ITunnelReplacementBackend
{
public:
    virtual ~ITunnelReplacementBackend() = default;

    virtual connection::ReconnectReplacementExecutionResult startAgentReplacements(
        const TunnelReplacementRequest& request) = 0;
    virtual connection::ReconnectReplacementExecutionResult reconnectClientReplacements(
        const TunnelReplacementRequest& request) = 0;
};

class TunnelReconnectExecutor : public connection::IReconnectReplacementExecutor
{
public:
    explicit TunnelReconnectExecutor(
        ITunnelReplacementBackend& backend,
        TunnelReconnectExecutorOptions options = {});

    connection::ReconnectReplacementExecutionResult startAgentReplacements(
        const connection::ReconnectOrchestrationSidePlan& agent) override;
    connection::ReconnectReplacementExecutionResult reconnectClientReplacements(
        const connection::ReconnectOrchestrationSidePlan& client) override;

private:
    ITunnelReplacementBackend& backend_;
    TunnelReconnectExecutorOptions options_;
};

TunnelReplacementRequestBuildResult buildTunnelReplacementRequest(
    const connection::ReconnectOrchestrationSidePlan& sidePlan,
    TunnelReplacementSide side,
    const TunnelReconnectExecutorOptions& options = {});

const char* tunnelTransportModeName(TunnelTransportMode mode);
const char* tunnelReplacementSideName(TunnelReplacementSide side);

} // namespace tunnel
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_TUNNEL_TUNNEL_RECONNECT_EXECUTOR_H
