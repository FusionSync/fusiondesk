#ifndef FUSIONDESK_SESSION_SESSION_MANAGER_H
#define FUSIONDESK_SESSION_SESSION_MANAGER_H

#include <map>
#include <memory>
#include <vector>

#include "fusiondesk/core/session/session.h"

namespace fusiondesk {
namespace session {

struct SessionCreateOptions
{
    SessionContext context;
    std::vector<network::ChannelSpec> minimumChannels;
};

class SessionManager
{
public:
    diagnostics::DiagnosticsSink& diagnostics();
    const diagnostics::DiagnosticsSink& diagnostics() const;

    protocol::SessionId createClientSession(const SessionCreateOptions& options);
    protocol::SessionId createAgentSession(const SessionCreateOptions& options);
    Session* find(protocol::SessionId id) const;
    bool reconnect(protocol::SessionId id, const ReconnectRequest& request);
    bool close(protocol::SessionId id, const SessionStopReason& reason);
    std::vector<SessionSnapshot> snapshots() const;

private:
    protocol::SessionId createSession(SessionCreateOptions options, SessionRole role);
    SessionContext normalizeContext(SessionContext context, SessionRole role);
    std::unique_ptr<policy::IPolicyEngine> createPolicyEngine(const SessionContext& context) const;
    std::unique_ptr<network::NetworkManager> createNetworkManager(
        const SessionContext& context,
        const std::vector<network::ChannelSpec>& minimumChannels) const;

private:
    diagnostics::DiagnosticsSink diagnostics_;
    std::map<protocol::SessionId, std::unique_ptr<Session>> sessions_;
    protocol::SessionId nextSessionId_ = 1;
};

} // namespace session
} // namespace fusiondesk

#endif // FUSIONDESK_SESSION_SESSION_MANAGER_H
