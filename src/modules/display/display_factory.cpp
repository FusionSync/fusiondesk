#include "fusiondesk/modules/display/display_factory.h"

#include <utility>

#include "fusiondesk/core/module/module_catalog.h"
#include "fusiondesk/modules/display/display_modules.h"

namespace fusiondesk {
namespace modules {
namespace display {

namespace {

bool isAgentRequest(const std::string& requestedModuleId,
                    const module::ModuleCreateOptions& options)
{
    if (options.role != session::SessionRole::Agent)
        return false;

    return requestedModuleId == "display.screen" ||
           requestedModuleId == "display.screen.agent";
}

bool isClientRequest(const std::string& requestedModuleId,
                     const module::ModuleCreateOptions& options)
{
    if (options.role != session::SessionRole::Client)
        return false;

    return requestedModuleId == "display.screen" ||
           requestedModuleId == "display.screen.client";
}

} // namespace

DisplayModuleFactory::DisplayModuleFactory(DisplayModuleDependencies dependencies)
    : dependencies_(std::move(dependencies))
{
}

bool DisplayModuleFactory::supports(const std::string& requestedModuleId,
                                    const module::ModuleCreateOptions& options) const
{
    return isAgentRequest(requestedModuleId, options) ||
           isClientRequest(requestedModuleId, options);
}

module::ModuleManifest DisplayModuleFactory::manifest(const module::ModuleCreateOptions& options) const
{
    if (options.role == session::SessionRole::Agent)
        return module::catalog::displayScreenAgent();

    if (options.role == session::SessionRole::Client)
        return module::catalog::displayScreenClient();

    return {};
}

std::shared_ptr<module::IModule> DisplayModuleFactory::create(const module::ModuleCreateOptions& options) const
{
    if (options.role == session::SessionRole::Agent) {
        if (dependencies_.capture == nullptr || dependencies_.encoder == nullptr)
            return nullptr;

        return std::make_shared<DisplayAgentModule>(dependencies_.capture,
                                                    dependencies_.encoder,
                                                    dependencies_.captureOptions,
                                                    dependencies_.encoderCodec);
    }

    if (options.role == session::SessionRole::Client) {
        if (dependencies_.decoder == nullptr || dependencies_.renderer == nullptr)
            return nullptr;

        return std::make_shared<DisplayClientModule>(dependencies_.decoder,
                                                     dependencies_.renderer,
                                                     dependencies_.decoderCodec);
    }

    return nullptr;
}

} // namespace display
} // namespace modules
} // namespace fusiondesk
