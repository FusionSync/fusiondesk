#include "fusiondesk/core/module/module_compatibility.h"

namespace fusiondesk {
namespace module {

namespace {

bool matchesPeer(const ModulePeerCompatibility& compatibility, const std::string& peerModuleId)
{
    return compatibility.peerModuleId == peerModuleId;
}

} // namespace

int compareModuleVersion(const ModuleVersion& lhs, const ModuleVersion& rhs)
{
    if (lhs.major != rhs.major)
        return lhs.major < rhs.major ? -1 : 1;
    if (lhs.minor != rhs.minor)
        return lhs.minor < rhs.minor ? -1 : 1;
    if (lhs.patch != rhs.patch)
        return lhs.patch < rhs.patch ? -1 : 1;
    return 0;
}

bool moduleVersionInRange(const ModuleVersion& version,
                          const ModuleVersion& minimum,
                          const ModuleVersion& maximum)
{
    return compareModuleVersion(minimum, version) <= 0 &&
           compareModuleVersion(version, maximum) <= 0;
}

ModuleCompatibilityCheck checkModuleCompatibility(const ModuleManifest& local,
                                                  const std::string& peerModuleId,
                                                  const ModuleVersion& peerVersion)
{
    ModuleCompatibilityCheck result;
    result.localModuleId = local.moduleId;
    result.localVersion = local.version;
    result.peerModuleId = peerModuleId;
    result.peerVersion = peerVersion;

    if (local.moduleId.empty()) {
        result.message = "local module id is empty";
        return result;
    }

    if (peerModuleId.empty()) {
        result.message = "peer module id is empty";
        return result;
    }

    for (const ModulePeerCompatibility& compatibility : local.compatiblePeers) {
        if (!matchesPeer(compatibility, peerModuleId))
            continue;

        result.compatibilityMode = compatibility.compatibilityMode;
        if (!moduleVersionInRange(peerVersion,
                                  compatibility.minPeerVersion,
                                  compatibility.maxPeerVersion)) {
            result.message = "peer module version is outside declared range";
            return result;
        }

        result.compatible = true;
        result.message = "peer module version is compatible";
        return result;
    }

    result.message = "peer module is not declared as compatible";
    return result;
}

} // namespace module
} // namespace fusiondesk
