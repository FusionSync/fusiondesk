#include "fusiondesk/core/session/session.h"

#include <utility>

namespace fusiondesk {
namespace session {

namespace {

const char* stateName(SessionState state)
{
    switch (state) {
    case SessionState::Created:
        return "Created";
    case SessionState::Authorizing:
        return "Authorizing";
    case SessionState::Authorized:
        return "Authorized";
    case SessionState::Connecting:
        return "Connecting";
    case SessionState::NetworkReady:
        return "NetworkReady";
    case SessionState::MountingModules:
        return "MountingModules";
    case SessionState::Running:
        return "Running";
    case SessionState::Reconnecting:
        return "Reconnecting";
    case SessionState::Draining:
        return "Draining";
    case SessionState::Stopping:
        return "Stopping";
    case SessionState::Stopped:
        return "Stopped";
    case SessionState::Failed:
        return "Failed";
    }
    return "Unknown";
}

network::ChannelKey keyOf(const network::IChannel& channel)
{
    return network::ChannelKey{channel.id(), channel.type()};
}

bool containsKey(const std::vector<network::ChannelKey>& keys, network::ChannelKey key)
{
    for (const network::ChannelKey& item : keys) {
        if (item.channelId == key.channelId && item.channelType == key.channelType)
            return true;
    }
    return false;
}

std::vector<network::ChannelKey> affectedReconnectChannels(const ReconnectRequest& request)
{
    std::vector<network::ChannelKey> result = request.degradedChannels;
    for (const ReconnectChannelReplacement& replacement : request.replacementChannels) {
        if (!replacement.channel)
            continue;

        const network::ChannelKey key = keyOf(*replacement.channel);
        if (!containsKey(result, key))
            result.push_back(key);
    }
    return result;
}

ReconnectChannelReport makeChannelReport(ReconnectChannelOperation operation,
                                         network::ChannelKey key,
                                         const network::ChannelRegistryResult& result)
{
    ReconnectChannelReport report;
    report.key = key;
    report.operation = operation;
    report.ok = result.ok;
    report.registryStatus = result.status;
    report.responseStatus = result.responseStatus;
    report.message = result.message;
    return report;
}

} // namespace

Session::Session(SessionContext context,
                 std::unique_ptr<network::NetworkManager> network,
                 std::unique_ptr<policy::IPolicyEngine> policyEngine,
                 diagnostics::DiagnosticsSink* diagnostics)
    : context_(std::move(context))
    , network_(std::move(network))
    , policyEngine_(std::move(policyEngine))
    , diagnostics_(diagnostics)
{
    publish("session.created", "session created");
}

protocol::SessionId Session::id() const
{
    return context_.sessionId;
}

SessionRole Session::role() const
{
    return context_.role;
}

SessionState Session::state() const
{
    return state_;
}

const SessionContext& Session::context() const
{
    return context_;
}

network::NetworkManager* Session::network()
{
    return network_.get();
}

policy::IPolicyEngine* Session::policyEngine()
{
    return policyEngine_.get();
}

module::ModuleHost* Session::moduleHost()
{
    return moduleHost_.get();
}

bool Session::start(const SessionStartOptions& options)
{
    (void)options;

    if (state_ != SessionState::Created)
        return false;

    transition(SessionState::Authorizing, "authorizing features");
    authorize();
    transition(SessionState::Authorized, "features authorized");
    transition(SessionState::Connecting, "creating network context");
    transition(SessionState::NetworkReady, "network manager ready");
    transition(SessionState::MountingModules, "creating module host");
    createModuleHost();
    transition(SessionState::Running, "session running");
    return true;
}

bool Session::reconnect(const ReconnectRequest& request)
{
    if (state_ != SessionState::Running && state_ != SessionState::NetworkReady)
        return false;

    ++context_.reconnectCount;
    lastReconnectReport_ = ReconnectReport{};
    lastReconnectReport_.attempted = true;
    lastReconnectReport_.ok = true;
    lastReconnectReport_.reconnectCount = context_.reconnectCount;
    lastReconnectReport_.reason = request.reason;
    lastReconnectReport_.requestedFreshState = request.requestDisplayKeyframe;

    transition(SessionState::Reconnecting, request.reason.empty() ? "reconnecting" : request.reason);
    publish("network.reconnect_started", request.reason);
    if (network_) {
        for (const network::ChannelKey& key : request.degradedChannels) {
            const network::ChannelRegistryResult degraded =
                network_->markDegraded(key, request.reason.empty() ? "reconnecting" : request.reason);
            lastReconnectReport_.degradedChannels.push_back(
                makeChannelReport(ReconnectChannelOperation::Degrade, key, degraded));
            lastReconnectReport_.ok = lastReconnectReport_.ok && degraded.ok;
            if (degraded.ok)
                publish("network.reconnect_channel_degraded", request.reason);
            else
                publish("network.reconnect_channel_degraded_failed", degraded.message);
        }
    }
    module::ModuleReconnectOptions reconnectOptions;
    reconnectOptions.reason = request.reason;
    reconnectOptions.affectedChannels = affectedReconnectChannels(request);
    reconnectOptions.reconnectCount = context_.reconnectCount;
    reconnectOptions.requestFreshState = request.requestDisplayKeyframe;

    transition(SessionState::Draining, "draining for reconnect");
    const std::vector<module::ModuleReconnectReport> pausedModules =
        moduleHost_ ? moduleHost_->pauseRunningModulesForReconnect(reconnectOptions)
                    : std::vector<module::ModuleReconnectReport>{};
    lastReconnectReport_.pausedModules = pausedModules;
    publish("module.reconnect_paused", std::to_string(pausedModules.size()) + " modules paused");
    if (network_) {
        for (const ReconnectChannelReplacement& replacement : request.replacementChannels) {
            const network::ChannelKey key =
                replacement.channel ? keyOf(*replacement.channel) : network::ChannelKey{};
            const network::ChannelRegistryResult rebound =
                network_->rebindChannel(replacement.channel, replacement.ready);
            lastReconnectReport_.reboundChannels.push_back(
                makeChannelReport(ReconnectChannelOperation::Rebind, key, rebound));
            lastReconnectReport_.ok = lastReconnectReport_.ok && rebound.ok;
            if (rebound.ok)
                publish("network.reconnect_channel_rebound", request.reason);
            else
                publish("network.reconnect_channel_rebound_failed", rebound.message);
        }
    }
    const std::vector<module::ModuleIngressReplayReport> replayedIngress =
        moduleHost_ ? moduleHost_->replayRunningModuleIngressForReconnect(reconnectOptions)
                    : std::vector<module::ModuleIngressReplayReport>{};
    lastReconnectReport_.replayedIngress = replayedIngress;
    bool replayOk = true;
    for (const module::ModuleIngressReplayReport& report : replayedIngress)
        replayOk = replayOk && report.replayed;
    lastReconnectReport_.ok = lastReconnectReport_.ok && replayOk;
    publish(replayOk ? "module.ingress_replayed" : "module.ingress_replay_failed",
            std::to_string(replayedIngress.size()) + " module ingress routes replayed");
    if (request.requestDisplayKeyframe)
        publish("display.keyframe_requested", request.reason.empty() ? "reconnect" : request.reason);
    transition(SessionState::NetworkReady, "network rebound");
    const std::vector<module::ModuleReconnectReport> resumedModules =
        moduleHost_ ? moduleHost_->resumeRunningModulesAfterReconnect(reconnectOptions)
                    : std::vector<module::ModuleReconnectReport>{};
    lastReconnectReport_.resumedModules = resumedModules;
    publish("module.reconnect_resumed", std::to_string(resumedModules.size()) + " modules resumed");
    publish("network.reconnect_finished",
            lastReconnectReport_.ok ? "reconnect finished" : "reconnect finished with errors");
    transition(SessionState::Running, "session resumed");
    return lastReconnectReport_.ok;
}

void Session::stop(const SessionStopReason& reason)
{
    if (state_ == SessionState::Stopped)
        return;

    transition(SessionState::Stopping, reason.message.empty() ? "stopping session" : reason.message);
    if (moduleHost_)
        moduleHost_->stopAll(module::ModuleStopOptions{true, reason.message});
    transition(SessionState::Stopped, "session stopped");
}

const ReconnectReport& Session::lastReconnectReport() const
{
    return lastReconnectReport_;
}

void Session::updateRemoteModuleInventory(
    protocol::SessionId peerSessionId,
    const std::vector<module::ModuleManifest>& manifests)
{
    remoteModuleInventory_.peerSessionId = peerSessionId;
    remoteModuleInventory_.manifests = manifests;
    publish("module.remote_inventory_updated",
            std::to_string(remoteModuleInventory_.manifests.size()) + " remote modules");
}

const RemoteModuleInventorySnapshot& Session::remoteModuleInventory() const
{
    return remoteModuleInventory_;
}

SessionSnapshot Session::snapshot() const
{
    SessionSnapshot result;
    result.context = context_;
    result.state = state_;
    result.lastReconnect = lastReconnectReport_;
    result.remoteModuleInventory = remoteModuleInventory_;
    if (diagnostics_)
        result.diagnosticsCount = diagnostics_->eventsForSession(context_.sessionId).size();
    if (moduleHost_)
        result.moduleSnapshots = moduleHost_->snapshots();
    return result;
}

void Session::transition(SessionState next, const std::string& message)
{
    state_ = next;
    publish("session.state_changed", std::string(stateName(next)) + ": " + message);
}

void Session::publish(const std::string& code, const std::string& message)
{
    if (!diagnostics_)
        return;

    diagnostics::DiagnosticEvent event;
    event.sessionId = context_.sessionId;
    event.traceId = context_.traceId;
    event.code = code;
    event.message = message;
    event.policyVersion = context_.policyVersion;
    diagnostics_->publish(event);
}

void Session::authorize()
{
    if (policyEngine_) {
        policy::PolicyContext policyContext;
        policyContext.session = context_;
        policyContext.principal.userId = context_.userId;
        policyContext.principal.tenantId = context_.tenantId;
        policyContext.localDevice.deviceId = context_.clientDeviceId;
        policyContext.localDevice.platform = context_.localPlatform;
        policyContext.remoteDevice.deviceId = context_.agentDeviceId;
        policyContext.remoteDevice.platform = context_.remotePlatform;
        context_.allowedFeatures = policyEngine_->authorizeFeatures(policyContext, context_.requestedFeatures);
    } else {
        context_.allowedFeatures = context_.requestedFeatures;
    }

    publish("policy.features_authorized", "features authorized");
}

void Session::createModuleHost()
{
    if (!network_)
        return;

    module::ModuleRuntime runtime;
    runtime.session = context_;
    runtime.network = &network_->router();
    runtime.networkManager = network_.get();
    runtime.channels = &network_->registry();
    runtime.diagnostics = diagnostics_;
    moduleHost_ = std::make_unique<module::ModuleHost>(runtime, policyEngine_.get());
}

} // namespace session
} // namespace fusiondesk
