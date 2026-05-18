#include "fusiondesk/core/module/module_host.h"

#include <algorithm>
#include <utility>

#include "fusiondesk/core/module/module_compatibility.h"

namespace fusiondesk {
namespace module {

namespace {

std::uint32_t roleFlag(session::SessionRole role)
{
    switch (role) {
    case session::SessionRole::Client:
        return ModuleRoleClient;
    case session::SessionRole::Agent:
        return ModuleRoleAgent;
    case session::SessionRole::Auth:
        return ModuleRoleAuth;
    case session::SessionRole::Relay:
        return ModuleRoleBridge;
    case session::SessionRole::Standalone:
        return ModuleRoleTool;
    }
    return 0;
}

bool supportsRole(const ModuleManifest& manifest, session::SessionRole role)
{
    if (manifest.roleFlags == 0)
        return true;

    return (manifest.roleFlags & roleFlag(role)) != 0;
}

bool supportsPlatform(const ModuleManifest& manifest, const std::string& platform)
{
    if (manifest.supportedPlatforms.empty())
        return true;

    return std::find(manifest.supportedPlatforms.begin(),
                     manifest.supportedPlatforms.end(),
                     platform) != manifest.supportedPlatforms.end();
}

bool hasConsumedPackets(const ModuleManifest& manifest)
{
    for (const ChannelBinding& binding : manifest.channels) {
        if (!binding.consumes.empty())
            return true;
    }
    return false;
}

bool channelMatches(const ChannelBinding& binding, network::ChannelKey key)
{
    return binding.channelId == key.channelId && binding.channelType == key.channelType;
}

bool affectedByReconnect(const ModuleManifest& manifest, const std::vector<network::ChannelKey>& channels)
{
    if (channels.empty())
        return true;

    for (const ChannelBinding& binding : manifest.channels) {
        for (const network::ChannelKey& key : channels) {
            if (channelMatches(binding, key))
                return true;
        }
    }
    return false;
}

struct PeerCompatibilityEvaluation
{
    std::vector<ModulePeerVersion> peerVersions;
    bool blocked = false;
    std::string message;
};

struct ModuleStartCandidate
{
    IModule* target = nullptr;
    ModuleStartOptions options;
    ModuleStartReport report;
};

PeerCompatibilityEvaluation evaluatePeerVersions(const ModuleManifest& manifest,
                                                  const std::vector<ModulePeerVersion>& peers)
{
    PeerCompatibilityEvaluation result;
    if (peers.empty() || manifest.compatiblePeers.empty())
        return result;

    for (const ModulePeerCompatibility& expected : manifest.compatiblePeers) {
        bool found = false;
        ModuleCompatibilityCheck lastCheck;

        for (const ModulePeerVersion& peer : peers) {
            if (peer.moduleId != expected.peerModuleId)
                continue;

            found = true;
            lastCheck = checkModuleCompatibility(manifest, peer.moduleId, peer.version);
            if (lastCheck.compatible) {
                result.peerVersions.push_back(ModulePeerVersion{
                    peer.moduleId,
                    peer.version,
                    true,
                    lastCheck.compatibilityMode});
                break;
            }
        }

        if (!found) {
            result.blocked = true;
            result.message = "required peer module version is missing: " + expected.peerModuleId;
            return result;
        }

        if (!lastCheck.compatible) {
            result.blocked = true;
            result.message = lastCheck.message;
            return result;
        }
    }

    return result;
}

void deny(ModuleStartReport& report,
          policy::DenyReason reason,
          const std::string& message,
          const IModule& target)
{
    report.decision.allowed = false;
    report.decision.reason = reason;
    report.decision.message = message;
    report.diagnostics = target.diagnostics();
}

void markSkippedByBatch(ModuleStartCandidate& candidate)
{
    if (candidate.report.decision.allowed) {
        candidate.report.decision.allowed = false;
        candidate.report.decision.reason = policy::DenyReason::RuntimeHealthBlocked;
        candidate.report.decision.message = "module start skipped because partial start is disabled";
    }

    if (candidate.target != nullptr)
        candidate.report.diagnostics = candidate.target->diagnostics();
}

ModuleReconnectReport notifyPause(IModule& target, const ModuleReconnectOptions& options)
{
    ModuleReconnectReport report;
    report.moduleId = target.manifest().moduleId;
    if (IReconnectAwareModule* reconnectAware = dynamic_cast<IReconnectAwareModule*>(&target)) {
        reconnectAware->pauseForReconnect(options);
        report.notified = true;
    }
    report.diagnostics = target.diagnostics();
    return report;
}

ModuleReconnectReport notifyResume(IModule& target, const ModuleReconnectOptions& options)
{
    ModuleReconnectReport report;
    report.moduleId = target.manifest().moduleId;
    if (IReconnectAwareModule* reconnectAware = dynamic_cast<IReconnectAwareModule*>(&target)) {
        reconnectAware->resumeAfterReconnect(options);
        report.notified = true;
    }
    report.diagnostics = target.diagnostics();
    return report;
}

ModuleIngressReplayReport replayIngress(ModuleIngressRegistry& ingress,
                                        IModule& target,
                                        ModulePacketHandler handler)
{
    ModuleIngressReplayReport report;
    report.moduleId = target.manifest().moduleId;
    report.diagnostics = target.diagnostics();

    const RegisteredIngress registered = ingress.registerManifest(target.manifest(), std::move(handler));
    report.tokenCount = registered.tokens.size();
    report.replayed = !registered.tokens.empty();
    report.message = report.replayed ? "module ingress replayed" : "module ingress replay failed";
    return report;
}

} // namespace

ModuleHost::ModuleHost(ModuleRuntime runtime, policy::IPolicyEngine* policyEngine)
    : runtime_(std::move(runtime)),
      ingress_(runtime_.network),
      policyEngine_(policyEngine)
{
}

bool ModuleHost::addModule(std::shared_ptr<IModule> module)
{
    if (!module)
        return false;

    const std::string& moduleId = module->manifest().moduleId;
    if (moduleId.empty() || modules_.find(moduleId) != modules_.end())
        return false;

    if (!supportsRole(module->manifest(), runtime_.session.role))
        return false;

    if (!supportsPlatform(module->manifest(), runtime_.session.localPlatform))
        return false;

    if (!module->attach(runtime_))
        return false;

    modules_[moduleId] = std::move(module);
    moduleOrder_.push_back(moduleId);
    return true;
}

std::vector<ModuleStartReport> ModuleHost::startAllowedModules()
{
    return startAllowedModules(ModuleStartOptions{});
}

std::vector<ModuleStartReport> ModuleHost::startAllowedModules(const ModuleStartOptions& options)
{
    std::vector<ModuleStartCandidate> candidates;
    bool preflightFailed = false;

    for (const std::string& moduleId : moduleOrder_) {
        auto item = modules_.find(moduleId);
        if (item == modules_.end())
            continue;

        IModule& target = *item->second;
        ModuleStartCandidate candidate;
        candidate.target = &target;
        candidate.options = options;
        candidate.report.moduleId = target.manifest().moduleId;

        bool dependencyMissing = false;
        for (const std::string& required : target.manifest().requiredModules) {
            if (modules_.find(required) == modules_.end()) {
                dependencyMissing = true;
                break;
            }
        }

        if (dependencyMissing) {
            deny(candidate.report,
                 policy::DenyReason::MissingDependency,
                 "required module dependency is missing",
                 target);
            preflightFailed = true;
            candidates.push_back(std::move(candidate));
            continue;
        }

        bool channelNotReady = false;
        if (runtime_.channels != nullptr) {
            for (const ChannelBinding& binding : target.manifest().channels) {
                if (!binding.required)
                    continue;

                if (!runtime_.channels->isReady(network::ChannelKey{binding.channelId, binding.channelType})) {
                    channelNotReady = true;
                    break;
                }
            }
        }

        if (channelNotReady) {
            deny(candidate.report,
                 policy::DenyReason::MissingDependency,
                 "required channel is not ready",
                 target);
            preflightFailed = true;
            candidates.push_back(std::move(candidate));
            continue;
        }

        const PeerCompatibilityEvaluation peerCompatibility =
            evaluatePeerVersions(target.manifest(), options.peerVersions);
        if (peerCompatibility.blocked) {
            deny(candidate.report,
                 policy::DenyReason::ModuleVersionMismatch,
                 peerCompatibility.message,
                 target);
            preflightFailed = true;
            candidates.push_back(std::move(candidate));
            continue;
        }
        candidate.options.peerVersions = peerCompatibility.peerVersions;

        if (policyEngine_ != nullptr) {
            policy::PolicyContext context;
            context.session = runtime_.session;
            candidate.report.decision = policyEngine_->authorizeModule(context, target.manifest());
        } else {
            candidate.report.decision.allowed = true;
            candidate.report.decision.reason = policy::DenyReason::None;
            candidate.report.decision.effectiveFeatures = runtime_.session.allowedFeatures;
        }

        if (!candidate.report.decision.allowed)
            preflightFailed = true;

        candidate.report.diagnostics = target.diagnostics();
        candidates.push_back(std::move(candidate));
    }

    if (preflightFailed && !options.allowPartialStart) {
        std::vector<ModuleStartReport> reports;
        reports.reserve(candidates.size());
        for (ModuleStartCandidate& candidate : candidates) {
            markSkippedByBatch(candidate);
            reports.push_back(std::move(candidate.report));
        }
        return reports;
    }

    std::vector<ModuleStartReport> reports;
    reports.reserve(candidates.size());
    std::vector<IModule*> startedModules;

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        ModuleStartCandidate& candidate = candidates[index];
        if (candidate.target == nullptr) {
            reports.push_back(std::move(candidate.report));
            continue;
        }

        IModule& target = *candidate.target;

        if (candidate.report.decision.allowed) {
            candidate.report.started = target.start(candidate.options);
            if (!candidate.report.started) {
                candidate.report.decision.allowed = false;
                candidate.report.decision.reason = policy::DenyReason::RuntimeHealthBlocked;
                candidate.report.decision.message = "module start failed";
            }
        }

        if (candidate.report.started && hasConsumedPackets(target.manifest())) {
            const RegisteredIngress registered = ingress_.registerManifest(
                target.manifest(),
                [this](const std::string& moduleId, const protocol::PacketEnvelope& packet) {
                    IModule* receiver = module(moduleId);
                    if (receiver != nullptr)
                        receiver->handlePacket(packet);
                });

            if (registered.tokens.empty()) {
                target.stop(ModuleStopOptions{});
                candidate.report.started = false;
                candidate.report.decision.allowed = false;
                candidate.report.decision.reason = policy::DenyReason::RuntimeHealthBlocked;
                candidate.report.decision.message = "module ingress registration failed";
            }
        }

        if (candidate.report.started)
            startedModules.push_back(&target);
        else if (!options.allowPartialStart) {
            for (auto it = startedModules.rbegin(); it != startedModules.rend(); ++it) {
                ingress_.unregisterModule((*it)->manifest().moduleId);
                (*it)->stop(ModuleStopOptions{true, "rolling back failed module batch start"});
            }

            for (ModuleStartReport& previous : reports) {
                if (!previous.started)
                    continue;

                previous.started = false;
                previous.decision.allowed = false;
                previous.decision.reason = policy::DenyReason::RuntimeHealthBlocked;
                previous.decision.message = "module start rolled back because batch start failed";
            }

            candidate.report.diagnostics = target.diagnostics();
            reports.push_back(std::move(candidate.report));

            for (std::size_t remaining = index + 1; remaining < candidates.size(); ++remaining) {
                markSkippedByBatch(candidates[remaining]);
                reports.push_back(std::move(candidates[remaining].report));
            }

            return reports;
        }

        candidate.report.diagnostics = target.diagnostics();
        reports.push_back(std::move(candidate.report));
    }

    return reports;
}

std::vector<ModuleReconnectReport> ModuleHost::pauseRunningModulesForReconnect(
    const ModuleReconnectOptions& options)
{
    std::vector<ModuleReconnectReport> reports;

    for (auto it = moduleOrder_.rbegin(); it != moduleOrder_.rend(); ++it) {
        auto module = modules_.find(*it);
        if (module == modules_.end())
            continue;

        IModule& target = *module->second;
        if (target.state() != ModuleState::Running)
            continue;

        if (!affectedByReconnect(target.manifest(), options.affectedChannels))
            continue;

        reports.push_back(notifyPause(target, options));
    }

    return reports;
}

std::vector<ModuleIngressReplayReport> ModuleHost::replayRunningModuleIngressForReconnect(
    const ModuleReconnectOptions& options)
{
    std::vector<ModuleIngressReplayReport> reports;

    for (const std::string& moduleId : moduleOrder_) {
        auto module = modules_.find(moduleId);
        if (module == modules_.end())
            continue;

        IModule& target = *module->second;
        if (target.state() != ModuleState::Running)
            continue;

        if (!hasConsumedPackets(target.manifest()))
            continue;

        if (!affectedByReconnect(target.manifest(), options.affectedChannels))
            continue;

        reports.push_back(replayIngress(
            ingress_,
            target,
            [this](const std::string& receiverModuleId, const protocol::PacketEnvelope& packet) {
                IModule* receiver = this->module(receiverModuleId);
                if (receiver != nullptr)
                    receiver->handlePacket(packet);
            }));
    }

    return reports;
}

std::vector<ModuleReconnectReport> ModuleHost::resumeRunningModulesAfterReconnect(
    const ModuleReconnectOptions& options)
{
    std::vector<ModuleReconnectReport> reports;

    for (const std::string& moduleId : moduleOrder_) {
        auto module = modules_.find(moduleId);
        if (module == modules_.end())
            continue;

        IModule& target = *module->second;
        if (target.state() != ModuleState::Running)
            continue;

        if (!affectedByReconnect(target.manifest(), options.affectedChannels))
            continue;

        reports.push_back(notifyResume(target, options));
    }

    return reports;
}

void ModuleHost::stopAll(const ModuleStopOptions& options)
{
    for (auto it = moduleOrder_.rbegin(); it != moduleOrder_.rend(); ++it) {
        auto module = modules_.find(*it);
        if (module == modules_.end())
            continue;

        ingress_.unregisterModule(module->second->manifest().moduleId);
        module->second->stop(options);
    }
}

IModule* ModuleHost::module(const std::string& moduleId) const
{
    auto it = modules_.find(moduleId);
    if (it == modules_.end())
        return nullptr;

    return it->second.get();
}

std::vector<ModuleManifest> ModuleHost::manifests() const
{
    std::vector<ModuleManifest> result;
    for (const std::string& moduleId : moduleOrder_) {
        auto item = modules_.find(moduleId);
        if (item != modules_.end())
            result.push_back(item->second->manifest());
    }
    return result;
}

std::vector<ModuleSnapshot> ModuleHost::snapshots() const
{
    std::vector<ModuleSnapshot> result;
    result.reserve(modules_.size());
    for (const std::string& moduleId : moduleOrder_) {
        auto item = modules_.find(moduleId);
        if (item == modules_.end())
            continue;

        const IModule& target = *item->second;
        ModuleSnapshot snapshot;
        snapshot.moduleId = target.manifest().moduleId;
        snapshot.state = target.state();
        snapshot.manifest = target.manifest();
        snapshot.diagnostics = target.diagnostics();
        result.push_back(std::move(snapshot));
    }
    return result;
}

} // namespace module
} // namespace fusiondesk
