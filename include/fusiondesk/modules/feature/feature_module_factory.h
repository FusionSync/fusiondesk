#ifndef FUSIONDESK_MODULES_FEATURE_FEATURE_MODULE_FACTORY_H
#define FUSIONDESK_MODULES_FEATURE_FEATURE_MODULE_FACTORY_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_factory.h"

namespace fusiondesk {
namespace modules {
namespace feature {

struct FeatureModuleSnapshot
{
    std::string moduleId;
    module::ModuleState state = module::ModuleState::Created;
    int starts = 0;
    int stops = 0;
    int packetsReceived = 0;
};

class FeatureModule : public module::IModule
{
public:
    explicit FeatureModule(module::ModuleManifest manifest);

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    void handlePacket(const protocol::PacketEnvelope& packet) override;
    std::string diagnostics() const override;

    FeatureModuleSnapshot snapshot() const;

private:
    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    int starts_ = 0;
    int stops_ = 0;
    int packetsReceived_ = 0;
};

class FeatureModuleFactory : public module::IModuleFactory
{
public:
    explicit FeatureModuleFactory(module::ModuleManifest manifest);

    bool supports(const std::string& requestedModuleId,
                  const module::ModuleCreateOptions& options) const override;
    module::ModuleManifest manifest(const module::ModuleCreateOptions& options) const override;
    std::shared_ptr<module::IModule> create(const module::ModuleCreateOptions& options) const override;

private:
    module::ModuleManifest manifest_;
};

std::vector<module::ModuleManifest> defaultFeatureModuleManifests();
std::vector<std::shared_ptr<module::IModuleFactory>> makeDefaultFeatureModuleFactories();

} // namespace feature
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_FEATURE_FEATURE_MODULE_FACTORY_H
