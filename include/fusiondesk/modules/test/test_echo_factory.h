#ifndef FUSIONDESK_MODULES_TEST_TEST_ECHO_FACTORY_H
#define FUSIONDESK_MODULES_TEST_TEST_ECHO_FACTORY_H

#include <memory>

#include "fusiondesk/core/module/module_factory.h"

namespace fusiondesk {
namespace modules {
namespace test {

class TestEchoModuleFactory : public module::IModuleFactory
{
public:
    bool supports(const std::string& requestedModuleId,
                  const module::ModuleCreateOptions& options) const override;
    module::ModuleManifest manifest(const module::ModuleCreateOptions& options) const override;
    std::shared_ptr<module::IModule> create(const module::ModuleCreateOptions& options) const override;
};

module::ModuleManifest testEchoClientManifest();
module::ModuleManifest testEchoAgentManifest();
std::shared_ptr<module::IModuleFactory> makeTestEchoModuleFactory();

} // namespace test
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_TEST_TEST_ECHO_FACTORY_H
