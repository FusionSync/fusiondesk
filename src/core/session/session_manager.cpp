#include "fusiondesk/core/session/session_manager.h"

#include <utility>

namespace fusiondesk {
namespace session {

namespace {

bool empty(protocol::FeatureSet features)
{
    return features.bits == 0;
}

} // namespace

diagnostics::DiagnosticsSink& SessionManager::diagnostics()
{
    return diagnostics_;
}

const diagnostics::DiagnosticsSink& SessionManager::diagnostics() const
{
    return diagnostics_;
}

protocol::SessionId SessionManager::createClientSession(const SessionCreateOptions& options)
{
    return createSession(options, SessionRole::Client);
}

protocol::SessionId SessionManager::createAgentSession(const SessionCreateOptions& options)
{
    return createSession(options, SessionRole::Agent);
}

Session* SessionManager::find(protocol::SessionId id) const
{
    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return nullptr;
    return it->second.get();
}

bool SessionManager::reconnect(protocol::SessionId id, const ReconnectRequest& request)
{
    Session* target = find(id);
    if (!target)
        return false;
    return target->reconnect(request);
}

bool SessionManager::close(protocol::SessionId id, const SessionStopReason& reason)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return false;

    it->second->stop(reason);
    sessions_.erase(it);
    return true;
}

std::vector<SessionSnapshot> SessionManager::snapshots() const
{
    std::vector<SessionSnapshot> result;
    result.reserve(sessions_.size());
    for (const auto& item : sessions_)
        result.push_back(item.second->snapshot());
    return result;
}

protocol::SessionId SessionManager::createSession(SessionCreateOptions options, SessionRole role)
{
    SessionContext context = normalizeContext(std::move(options.context), role);
    const protocol::SessionId id = context.sessionId;

    std::unique_ptr<network::NetworkManager> network = createNetworkManager(context, options.minimumChannels);
    std::unique_ptr<policy::IPolicyEngine> policy = createPolicyEngine(context);

    if (role == SessionRole::Client) {
        sessions_[id] = std::make_unique<ClientSession>(std::move(context), std::move(network),
                                                        std::move(policy), &diagnostics_);
    } else {
        sessions_[id] = std::make_unique<AgentSession>(std::move(context), std::move(network),
                                                       std::move(policy), &diagnostics_);
    }
    return id;
}

SessionContext SessionManager::normalizeContext(SessionContext context, SessionRole role)
{
    if (context.sessionId == 0)
        context.sessionId = nextSessionId_++;
    else if (context.sessionId >= nextSessionId_)
        nextSessionId_ = context.sessionId + 1;

    if (context.traceId == 0)
        context.traceId = context.sessionId;

    context.role = role;
    context.protocolMajor = context.negotiatedCapabilities.protocolMajor;
    context.protocolMinor = context.negotiatedCapabilities.protocolMinor;

    if (empty(context.licensedFeatures))
        context.licensedFeatures = context.requestedFeatures;

    if (empty(context.policyFeatures))
        context.policyFeatures = context.licensedFeatures;

    return context;
}

std::unique_ptr<policy::IPolicyEngine> SessionManager::createPolicyEngine(const SessionContext& context) const
{
    auto engine = std::make_unique<policy::StaticPolicyEngine>(context.licensedFeatures);
    engine->setPolicyFeatures(context.policyFeatures);
    return engine;
}

std::unique_ptr<network::NetworkManager> SessionManager::createNetworkManager(
    const SessionContext& context,
    const std::vector<network::ChannelSpec>& minimumChannels) const
{
    auto manager = std::make_unique<network::NetworkManager>(context.negotiatedCapabilities);
    for (const network::ChannelSpec& spec : minimumChannels)
        manager->registerSpec(spec);
    return manager;
}

} // namespace session
} // namespace fusiondesk
