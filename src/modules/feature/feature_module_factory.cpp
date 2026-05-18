#include "fusiondesk/modules/feature/feature_module_factory.h"

#include <algorithm>
#include <utility>

#include "fusiondesk/core/module/module_catalog.h"

namespace fusiondesk {
namespace modules {
namespace feature {

namespace {

std::uint32_t roleFlag(session::SessionRole role)
{
    switch (role) {
    case session::SessionRole::Client:
        return module::ModuleRoleClient;
    case session::SessionRole::Agent:
        return module::ModuleRoleAgent;
    case session::SessionRole::Auth:
        return module::ModuleRoleAuth;
    case session::SessionRole::Relay:
        return module::ModuleRoleBridge;
    case session::SessionRole::Standalone:
        return module::ModuleRoleTool;
    }
    return 0;
}

bool supportsRole(const module::ModuleManifest& manifest,
                  session::SessionRole role)
{
    if (manifest.roleFlags == 0)
        return true;

    return (manifest.roleFlags & roleFlag(role)) != 0;
}

bool supportsPlatform(const module::ModuleManifest& manifest,
                      const std::string& platform)
{
    if (platform.empty() || manifest.supportedPlatforms.empty())
        return true;

    return std::find(manifest.supportedPlatforms.begin(),
                     manifest.supportedPlatforms.end(),
                     platform) != manifest.supportedPlatforms.end();
}

const char* stateName(module::ModuleState state)
{
    switch (state) {
    case module::ModuleState::Created:
        return "created";
    case module::ModuleState::Attached:
        return "attached";
    case module::ModuleState::Starting:
        return "starting";
    case module::ModuleState::Running:
        return "running";
    case module::ModuleState::Stopping:
        return "stopping";
    case module::ModuleState::Stopped:
        return "stopped";
    case module::ModuleState::Detached:
        return "detached";
    case module::ModuleState::Failed:
        return "failed";
    }
    return "unknown";
}

void publish(const module::ModuleRuntime& runtime,
             const std::string& moduleId,
             const std::string& code,
             const std::string& message)
{
    if (runtime.diagnostics == nullptr)
        return;

    diagnostics::DiagnosticEvent event;
    event.sessionId = runtime.session.sessionId;
    event.traceId = runtime.session.traceId;
    event.moduleId = moduleId;
    event.severity = diagnostics::DiagnosticSeverity::Info;
    event.code = code;
    event.message = message;
    event.policyVersion = runtime.session.policyVersion;
    runtime.diagnostics->publish(event);
}

} // namespace

FeatureModule::FeatureModule(module::ModuleManifest manifest)
    : manifest_(std::move(manifest))
{
}

const module::ModuleManifest& FeatureModule::manifest() const
{
    return manifest_;
}

module::ModuleState FeatureModule::state() const
{
    return state_;
}

bool FeatureModule::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    return !manifest_.moduleId.empty();
}

bool FeatureModule::start(const module::ModuleStartOptions&)
{
    if (state_ != module::ModuleState::Attached &&
        state_ != module::ModuleState::Stopped) {
        state_ = module::ModuleState::Failed;
        return false;
    }

    state_ = module::ModuleState::Running;
    ++starts_;
    publish(runtime_,
            manifest_.moduleId,
            "feature.module_started",
            manifest_.moduleId + " framework module started");
    return true;
}

void FeatureModule::stop(const module::ModuleStopOptions&)
{
    state_ = module::ModuleState::Stopped;
    ++stops_;
    publish(runtime_,
            manifest_.moduleId,
            "feature.module_stopped",
            manifest_.moduleId + " framework module stopped");
}

void FeatureModule::detach()
{
    state_ = module::ModuleState::Detached;
}

void FeatureModule::handlePacket(const protocol::PacketEnvelope&)
{
    if (state_ == module::ModuleState::Running)
        ++packetsReceived_;
}

std::string FeatureModule::diagnostics() const
{
    return "feature.module id=" + manifest_.moduleId +
           " state=" + stateName(state_) +
           " starts=" + std::to_string(starts_) +
           " stops=" + std::to_string(stops_) +
           " packets=" + std::to_string(packetsReceived_);
}

FeatureModuleSnapshot FeatureModule::snapshot() const
{
    FeatureModuleSnapshot snapshot;
    snapshot.moduleId = manifest_.moduleId;
    snapshot.state = state_;
    snapshot.starts = starts_;
    snapshot.stops = stops_;
    snapshot.packetsReceived = packetsReceived_;
    return snapshot;
}

FeatureModuleFactory::FeatureModuleFactory(module::ModuleManifest manifest)
    : manifest_(std::move(manifest))
{
}

bool FeatureModuleFactory::supports(
    const std::string& requestedModuleId,
    const module::ModuleCreateOptions& options) const
{
    return requestedModuleId == manifest_.moduleId &&
           supportsRole(manifest_, options.role) &&
           supportsPlatform(manifest_, options.localPlatform);
}

module::ModuleManifest FeatureModuleFactory::manifest(
    const module::ModuleCreateOptions& options) const
{
    if (!supportsRole(manifest_, options.role) ||
        !supportsPlatform(manifest_, options.localPlatform)) {
        return {};
    }
    return manifest_;
}

std::shared_ptr<module::IModule> FeatureModuleFactory::create(
    const module::ModuleCreateOptions& options) const
{
    if (!supportsRole(manifest_, options.role) ||
        !supportsPlatform(manifest_, options.localPlatform)) {
        return nullptr;
    }

    return std::make_shared<FeatureModule>(manifest_);
}

std::vector<module::ModuleManifest> defaultFeatureModuleManifests()
{
    return {
        module::catalog::desktopAudio(),
        module::catalog::microphone(),
        module::catalog::filesystem(),
        module::catalog::printer(),
        module::catalog::keyboard(),
        module::catalog::mouse(),
        module::catalog::touch(),
        module::catalog::gamepad(),
        module::catalog::camera(),
    };
}

std::vector<std::shared_ptr<module::IModuleFactory>>
makeDefaultFeatureModuleFactories()
{
    std::vector<std::shared_ptr<module::IModuleFactory>> factories;
    for (module::ModuleManifest manifest : defaultFeatureModuleManifests()) {
        factories.push_back(
            std::make_shared<FeatureModuleFactory>(std::move(manifest)));
    }
    return factories;
}

} // namespace feature
} // namespace modules
} // namespace fusiondesk
