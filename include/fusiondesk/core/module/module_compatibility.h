#ifndef FUSIONDESK_MODULE_MODULE_COMPATIBILITY_H
#define FUSIONDESK_MODULE_MODULE_COMPATIBILITY_H

#include <string>

#include "fusiondesk/core/module/module_manifest.h"

namespace fusiondesk {
namespace module {

struct ModuleCompatibilityCheck
{
    bool compatible = false;
    std::string localModuleId;
    ModuleVersion localVersion;
    std::string peerModuleId;
    ModuleVersion peerVersion;
    std::string compatibilityMode;
    std::string message;
};

int compareModuleVersion(const ModuleVersion& lhs, const ModuleVersion& rhs);
bool moduleVersionInRange(const ModuleVersion& version,
                          const ModuleVersion& minimum,
                          const ModuleVersion& maximum);
ModuleCompatibilityCheck checkModuleCompatibility(const ModuleManifest& local,
                                                  const std::string& peerModuleId,
                                                  const ModuleVersion& peerVersion);

} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_COMPATIBILITY_H
