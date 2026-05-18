#ifndef FUSIONDESK_MODULES_INPUT_INPUT_FACTORY_H
#define FUSIONDESK_MODULES_INPUT_INPUT_FACTORY_H

#include <memory>
#include <vector>

#include "fusiondesk/core/module/module_factory.h"
#include "fusiondesk/modules/input/input_interfaces.h"

namespace fusiondesk {
namespace modules {
namespace input {

struct InputModuleDependencies
{
    std::shared_ptr<IInputCapture> capture;
    std::shared_ptr<IInputInjector> injector;
};

class InputModuleFactory : public module::IModuleFactory
{
public:
    InputModuleFactory(InputModuleKind kind, InputModuleDependencies dependencies = {});

    bool supports(const std::string& requestedModuleId,
                  const module::ModuleCreateOptions& options) const override;
    module::ModuleManifest manifest(const module::ModuleCreateOptions& options) const override;
    std::shared_ptr<module::IModule> create(const module::ModuleCreateOptions& options) const override;

private:
    InputModuleKind kind_;
    InputModuleDependencies dependencies_;
};

module::ModuleManifest inputClientManifest(InputModuleKind kind);
module::ModuleManifest inputAgentManifest(InputModuleKind kind);
std::vector<module::ModuleManifest> defaultInputModuleManifests();
std::vector<std::shared_ptr<module::IModuleFactory>> makeDefaultInputModuleFactories(
    InputModuleDependencies dependencies = {});

} // namespace input
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_INPUT_INPUT_FACTORY_H
