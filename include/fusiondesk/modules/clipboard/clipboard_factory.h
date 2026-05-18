#ifndef FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_FACTORY_H
#define FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_FACTORY_H

#include <memory>

#include "fusiondesk/core/module/module_factory.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

class ClipboardModuleFactory : public module::IModuleFactory
{
public:
    explicit ClipboardModuleFactory(ClipboardModuleDependencies dependencies = {});

    bool supports(const std::string& requestedModuleId,
                  const module::ModuleCreateOptions& options) const override;
    module::ModuleManifest manifest(const module::ModuleCreateOptions& options) const override;
    std::shared_ptr<module::IModule> create(const module::ModuleCreateOptions& options) const override;

private:
    ClipboardModuleDependencies dependencies_;
};

module::ModuleManifest clipboardClientManifest();
module::ModuleManifest clipboardAgentManifest();
std::shared_ptr<module::IModuleFactory> makeClipboardModuleFactory(
    ClipboardModuleDependencies dependencies = {});

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_CLIPBOARD_FACTORY_H
