#ifndef FUSIONDESK_MODULE_MODULE_HOST_H
#define FUSIONDESK_MODULE_MODULE_HOST_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module.h"
#include "fusiondesk/core/module/module_ingress.h"
#include "fusiondesk/core/policy/policy_engine.h"

namespace fusiondesk {
namespace module {

struct ModuleStartReport
{
    std::string moduleId;
    bool started = false;
    policy::PolicyDecision decision;
    std::string diagnostics;
};

struct ModuleSnapshot
{
    std::string moduleId;
    ModuleState state = ModuleState::Created;
    ModuleManifest manifest;
    std::string diagnostics;
};

class ModuleHost
{
public:
    explicit ModuleHost(ModuleRuntime runtime, policy::IPolicyEngine* policyEngine);

    bool addModule(std::shared_ptr<IModule> module);
    std::vector<ModuleStartReport> startAllowedModules();
    std::vector<ModuleStartReport> startAllowedModules(const ModuleStartOptions& options);
    std::vector<ModuleReconnectReport> pauseRunningModulesForReconnect(const ModuleReconnectOptions& options);
    std::vector<ModuleIngressReplayReport> replayRunningModuleIngressForReconnect(
        const ModuleReconnectOptions& options);
    std::vector<ModuleReconnectReport> resumeRunningModulesAfterReconnect(const ModuleReconnectOptions& options);
    void stopAll(const ModuleStopOptions& options);
    IModule* module(const std::string& moduleId) const;
    std::vector<ModuleManifest> manifests() const;
    std::vector<ModuleSnapshot> snapshots() const;

private:
    ModuleRuntime runtime_;
    ModuleIngressRegistry ingress_;
    policy::IPolicyEngine* policyEngine_ = nullptr;
    std::map<std::string, std::shared_ptr<IModule>> modules_;
    std::vector<std::string> moduleOrder_;
};

} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_HOST_H
