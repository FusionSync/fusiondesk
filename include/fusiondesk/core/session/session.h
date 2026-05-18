#ifndef FUSIONDESK_SESSION_SESSION_H
#define FUSIONDESK_SESSION_SESSION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/diagnostics/diagnostics_sink.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/network_manager.h"
#include "fusiondesk/core/policy/policy_engine.h"
#include "fusiondesk/core/session/session_context.h"

namespace fusiondesk {
namespace session {

enum class SessionState
{
    Created,
    Authorizing,
    Authorized,
    Connecting,
    NetworkReady,
    MountingModules,
    Running,
    Reconnecting,
    Draining,
    Stopping,
    Stopped,
    Failed
};

struct SessionStartOptions
{
    bool startModules = false;
};

struct SessionStopReason
{
    std::string message;
};

struct ReconnectChannelReplacement
{
    std::shared_ptr<network::IChannel> channel;
    network::ChannelReadyInfo ready;
};

struct ReconnectRequest
{
    std::string reason;
    std::vector<network::ChannelKey> degradedChannels;
    std::vector<ReconnectChannelReplacement> replacementChannels;
    bool requestDisplayKeyframe = true;
};

enum class ReconnectChannelOperation
{
    Degrade,
    Rebind
};

struct ReconnectChannelReport
{
    network::ChannelKey key;
    ReconnectChannelOperation operation = ReconnectChannelOperation::Degrade;
    bool ok = false;
    network::ChannelRegistryStatus registryStatus = network::ChannelRegistryStatus::InvalidArgument;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Failed;
    std::string message;
};

struct ReconnectReport
{
    bool attempted = false;
    bool ok = false;
    std::uint32_t reconnectCount = 0;
    std::string reason;
    bool requestedFreshState = false;
    std::vector<ReconnectChannelReport> degradedChannels;
    std::vector<ReconnectChannelReport> reboundChannels;
    std::vector<module::ModuleReconnectReport> pausedModules;
    std::vector<module::ModuleIngressReplayReport> replayedIngress;
    std::vector<module::ModuleReconnectReport> resumedModules;
};

struct RemoteModuleInventorySnapshot
{
    protocol::SessionId peerSessionId = 0;
    std::vector<module::ModuleManifest> manifests;
};

struct SessionSnapshot
{
    SessionContext context;
    SessionState state = SessionState::Created;
    std::size_t diagnosticsCount = 0;
    std::vector<module::ModuleSnapshot> moduleSnapshots;
    RemoteModuleInventorySnapshot remoteModuleInventory;
    ReconnectReport lastReconnect;
};

class Session
{
public:
    Session(SessionContext context,
            std::unique_ptr<network::NetworkManager> network,
            std::unique_ptr<policy::IPolicyEngine> policyEngine,
            diagnostics::DiagnosticsSink* diagnostics);
    virtual ~Session() = default;

    protocol::SessionId id() const;
    SessionRole role() const;
    SessionState state() const;
    const SessionContext& context() const;
    network::NetworkManager* network();
    policy::IPolicyEngine* policyEngine();
    module::ModuleHost* moduleHost();

    bool start(const SessionStartOptions& options = {});
    bool reconnect(const ReconnectRequest& request);
    void stop(const SessionStopReason& reason);
    const ReconnectReport& lastReconnectReport() const;
    void updateRemoteModuleInventory(protocol::SessionId peerSessionId,
                                     const std::vector<module::ModuleManifest>& manifests);
    const RemoteModuleInventorySnapshot& remoteModuleInventory() const;
    SessionSnapshot snapshot() const;

protected:
    void transition(SessionState next, const std::string& message);
    void publish(const std::string& code, const std::string& message);

private:
    void authorize();
    void createModuleHost();

private:
    SessionContext context_;
    SessionState state_ = SessionState::Created;
    std::unique_ptr<network::NetworkManager> network_;
    std::unique_ptr<policy::IPolicyEngine> policyEngine_;
    std::unique_ptr<module::ModuleHost> moduleHost_;
    diagnostics::DiagnosticsSink* diagnostics_ = nullptr;
    ReconnectReport lastReconnectReport_;
    RemoteModuleInventorySnapshot remoteModuleInventory_;
};

class ClientSession : public Session
{
public:
    using Session::Session;
};

class AgentSession : public Session
{
public:
    using Session::Session;
};

} // namespace session
} // namespace fusiondesk

#endif // FUSIONDESK_SESSION_SESSION_H
