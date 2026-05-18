#include "fusiondesk/modules/clipboard/clipboard_factory.h"

#include <utility>

#include "fusiondesk/core/module/module_catalog.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

module::ModuleManifest makeManifest(session::SessionRole role)
{
    if (role == session::SessionRole::Client)
        return module::catalog::clipboardRedirectClient();
    if (role == session::SessionRole::Agent)
        return module::catalog::clipboardRedirectAgent();
    return {};
}

bool roleSupported(session::SessionRole role)
{
    return role == session::SessionRole::Client ||
           role == session::SessionRole::Agent;
}

bool isRequestForRole(const std::string& requestedModuleId,
                      session::SessionRole role)
{
    if (requestedModuleId == "clipboard.redirect")
        return true;

    if (role == session::SessionRole::Client)
        return requestedModuleId == "clipboard.redirect.client";

    if (role == session::SessionRole::Agent)
        return requestedModuleId == "clipboard.redirect.agent";

    return false;
}

} // namespace

ClipboardModuleFactory::ClipboardModuleFactory(ClipboardModuleDependencies dependencies)
    : dependencies_(std::move(dependencies))
{
}

bool ClipboardModuleFactory::supports(const std::string& requestedModuleId,
                                      const module::ModuleCreateOptions& options) const
{
    return roleSupported(options.role) &&
           isRequestForRole(requestedModuleId, options.role);
}

module::ModuleManifest ClipboardModuleFactory::manifest(
    const module::ModuleCreateOptions& options) const
{
    if (!roleSupported(options.role))
        return {};
    return makeManifest(options.role);
}

std::shared_ptr<module::IModule> ClipboardModuleFactory::create(
    const module::ModuleCreateOptions& options) const
{
    if (!roleSupported(options.role))
        return nullptr;

    if (options.role == session::SessionRole::Client)
        return std::make_shared<ClipboardClientModule>(dependencies_);

    return std::make_shared<ClipboardAgentModule>(dependencies_);
}

module::ModuleManifest clipboardClientManifest()
{
    return makeManifest(session::SessionRole::Client);
}

module::ModuleManifest clipboardAgentManifest()
{
    return makeManifest(session::SessionRole::Agent);
}

std::shared_ptr<module::IModuleFactory> makeClipboardModuleFactory(
    ClipboardModuleDependencies dependencies)
{
    return std::make_shared<ClipboardModuleFactory>(std::move(dependencies));
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
