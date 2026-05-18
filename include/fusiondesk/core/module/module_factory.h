#ifndef FUSIONDESK_MODULE_MODULE_FACTORY_H
#define FUSIONDESK_MODULE_MODULE_FACTORY_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module.h"

namespace fusiondesk {
namespace module {

struct ModuleCreateOptions
{
    session::SessionRole role = session::SessionRole::Standalone;
    std::string localPlatform;
};

class IModuleFactory
{
public:
    virtual ~IModuleFactory() = default;

    virtual bool supports(const std::string& requestedModuleId,
                          const ModuleCreateOptions& options) const = 0;
    virtual ModuleManifest manifest(const ModuleCreateOptions& options) const = 0;
    virtual std::shared_ptr<IModule> create(const ModuleCreateOptions& options) const = 0;
};

struct ModuleCompositionRequest
{
    std::vector<std::string> requiredModules;
    std::vector<ModuleVersionConstraint> versionConstraints;
    ModuleCreateOptions createOptions;
};

struct ModuleCompositionResult
{
    std::vector<std::shared_ptr<IModule>> modules;
    std::vector<ModuleManifest> manifests;
    std::vector<std::string> missingModules;
    std::vector<std::string> dependencyFailures;
    std::vector<std::string> versionFailures;

    bool ok() const
    {
        return missingModules.empty() && dependencyFailures.empty() && versionFailures.empty();
    }
};

class ModuleComposer
{
public:
    void addFactory(std::shared_ptr<IModuleFactory> factory);
    ModuleCompositionResult compose(const ModuleCompositionRequest& request) const;

private:
    const IModuleFactory* findFactory(const std::string& requestedModuleId,
                                      const ModuleCreateOptions& options) const;

private:
    std::vector<std::shared_ptr<IModuleFactory>> factories_;
};

} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_FACTORY_H
