#ifndef FUSIONDESK_MODULE_MODULE_MANIFEST_H
#define FUSIONDESK_MODULE_MODULE_MANIFEST_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/feature_flags.h"
#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace module {

enum ModuleRole : std::uint32_t
{
    ModuleRoleClient = 0x01,
    ModuleRoleAgent = 0x02,
    ModuleRoleAuth = 0x04,
    ModuleRoleBridge = 0x08,
    ModuleRoleTool = 0x10
};

enum ModuleRunMode : std::uint32_t
{
    ModuleRunModeInProcess = 0x01,
    ModuleRunModeHosted = 0x02,
    ModuleRunModeStandalone = 0x04,
    ModuleRunModeProcessOut = 0x08
};

struct ChannelBinding
{
    std::string name;
    protocol::ChannelId channelId = 0;
    protocol::ChannelType channelType = protocol::ChannelType::Standard;
    bool required = true;
    bool shared = false;
    std::vector<protocol::PacketType> consumes;
    std::vector<protocol::PacketType> produces;
};

struct ModuleVersion
{
    std::uint16_t major = 1;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
};

struct ModulePeerCompatibility
{
    std::string peerModuleId;
    ModuleVersion minPeerVersion;
    ModuleVersion maxPeerVersion;
    std::string compatibilityMode = "native";
};

struct ModulePeerVersion
{
    std::string moduleId;
    ModuleVersion version;
    bool compatible = false;
    std::string compatibilityMode;
};

struct ModuleVersionConstraint
{
    std::string moduleId;
    ModuleVersion minVersion;
    ModuleVersion maxVersion;
};

struct ModuleManifest
{
    std::string moduleId;
    std::string displayName;
    std::string sku;
    ModuleVersion version;
    protocol::FeatureMask feature = 0;
    std::uint32_t roleFlags = 0;
    std::uint32_t runModeFlags = 0;
    bool standaloneSaleUnit = false;
    bool disableAtRuntime = true;
    std::vector<std::string> supportedPlatforms;
    std::vector<std::string> requiredModules;
    std::vector<std::string> optionalModules;
    std::vector<ModulePeerCompatibility> compatiblePeers;
    std::vector<ChannelBinding> channels;
};

} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_MANIFEST_H
