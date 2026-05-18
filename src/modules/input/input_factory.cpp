#include "fusiondesk/modules/input/input_factory.h"

#include <utility>

#include "fusiondesk/modules/input/input_modules.h"

namespace fusiondesk {
namespace modules {
namespace input {

namespace {

constexpr std::uint32_t inProcessHosted =
    module::ModuleRunModeInProcess | module::ModuleRunModeHosted;

module::ChannelBinding smallDataBinding(std::vector<protocol::PacketType> consumes,
                                         std::vector<protocol::PacketType> produces)
{
    module::ChannelBinding binding;
    binding.name = "small_data";
    binding.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData);
    binding.channelType = protocol::ChannelType::Standard;
    binding.required = true;
    binding.shared = true;
    binding.consumes = std::move(consumes);
    binding.produces = std::move(produces);
    return binding;
}

module::ModuleManifest makeManifest(InputModuleKind kind, session::SessionRole role)
{
    module::ModuleManifest manifest;
    const bool mouse = kind == InputModuleKind::Mouse;
    const bool client = role == session::SessionRole::Client;
    const std::string baseModuleId = mouse ? "input.mouse" : "input.keyboard";
    manifest.moduleId = baseModuleId + (client ? ".client" : ".agent");
    manifest.displayName = std::string(mouse ? "Mouse" : "Keyboard") +
                           (client ? " input sender" : " input injector");
    manifest.sku = mouse ? "fusiondesk.module.input.mouse" :
                           "fusiondesk.module.input.keyboard";
    manifest.version = module::ModuleVersion{1, 0, 0};
    manifest.feature = mouse ? protocol::feature::Mouse : protocol::feature::Keyboard;
    manifest.roleFlags = client ? module::ModuleRoleClient : module::ModuleRoleAgent;
    manifest.runModeFlags = inProcessHosted;
    manifest.standaloneSaleUnit = true;
    manifest.disableAtRuntime = true;
    manifest.supportedPlatforms = {"windows", "linux", "macos", "android"};
    module::ModulePeerCompatibility peer;
    peer.peerModuleId = baseModuleId + (client ? ".agent" : ".client");
    peer.minPeerVersion = module::ModuleVersion{1, 0, 0};
    peer.maxPeerVersion = module::ModuleVersion{1, 99, 99};
    peer.compatibilityMode = "v1-family";
    manifest.compatiblePeers = {peer};

    const protocol::PacketType packetType = mouse ? protocol::PacketType::Mouse :
                                                   protocol::PacketType::Keyboard;
    if (client)
        manifest.channels.push_back(smallDataBinding({}, {packetType}));
    else
        manifest.channels.push_back(smallDataBinding({packetType}, {}));
    return manifest;
}

bool roleSupported(session::SessionRole role)
{
    return role == session::SessionRole::Client ||
           role == session::SessionRole::Agent;
}

bool isRequestForKind(const std::string& requestedModuleId,
                      InputModuleKind kind,
                      session::SessionRole role)
{
    if (kind == InputModuleKind::Mouse) {
        return requestedModuleId == "input.mouse" ||
               (role == session::SessionRole::Client &&
                requestedModuleId == "input.mouse.client") ||
               (role == session::SessionRole::Agent &&
                requestedModuleId == "input.mouse.agent");
    }

    return requestedModuleId == "input.keyboard" ||
           (role == session::SessionRole::Client &&
            requestedModuleId == "input.keyboard.client") ||
           (role == session::SessionRole::Agent &&
            requestedModuleId == "input.keyboard.agent");
}

} // namespace

InputModuleFactory::InputModuleFactory(InputModuleKind kind, InputModuleDependencies dependencies)
    : kind_(kind),
      dependencies_(std::move(dependencies))
{
}

bool InputModuleFactory::supports(const std::string& requestedModuleId,
                                  const module::ModuleCreateOptions& options) const
{
    return roleSupported(options.role) &&
           isRequestForKind(requestedModuleId, kind_, options.role);
}

module::ModuleManifest InputModuleFactory::manifest(const module::ModuleCreateOptions& options) const
{
    if (!roleSupported(options.role))
        return {};

    return makeManifest(kind_, options.role);
}

std::shared_ptr<module::IModule> InputModuleFactory::create(
    const module::ModuleCreateOptions& options) const
{
    if (!roleSupported(options.role))
        return nullptr;

    module::ModuleManifest roleManifest = makeManifest(kind_, options.role);
    if (options.role == session::SessionRole::Client)
        return std::make_shared<InputClientModule>(std::move(roleManifest),
                                                   kind_,
                                                   dependencies_.capture);

    return std::make_shared<InputAgentModule>(std::move(roleManifest),
                                              kind_,
                                              dependencies_.injector);
}

module::ModuleManifest inputClientManifest(InputModuleKind kind)
{
    return makeManifest(kind, session::SessionRole::Client);
}

module::ModuleManifest inputAgentManifest(InputModuleKind kind)
{
    return makeManifest(kind, session::SessionRole::Agent);
}

std::vector<module::ModuleManifest> defaultInputModuleManifests()
{
    return {
        inputClientManifest(InputModuleKind::Mouse),
        inputAgentManifest(InputModuleKind::Mouse),
        inputClientManifest(InputModuleKind::Keyboard),
        inputAgentManifest(InputModuleKind::Keyboard),
    };
}

std::vector<std::shared_ptr<module::IModuleFactory>> makeDefaultInputModuleFactories(
    InputModuleDependencies dependencies)
{
    return {
        std::make_shared<InputModuleFactory>(InputModuleKind::Mouse, dependencies),
        std::make_shared<InputModuleFactory>(InputModuleKind::Keyboard, std::move(dependencies)),
    };
}

} // namespace input
} // namespace modules
} // namespace fusiondesk
