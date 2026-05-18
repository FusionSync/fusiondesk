#include "fusiondesk/modules/test/test_echo_factory.h"

#include "fusiondesk/modules/test/test_echo_modules.h"

namespace fusiondesk {
namespace modules {
namespace test {

namespace {

constexpr std::uint32_t inProcessHosted =
    module::ModuleRunModeInProcess | module::ModuleRunModeHosted;

module::ChannelBinding controlBinding()
{
    module::ChannelBinding binding;
    binding.name = "control";
    binding.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain);
    binding.channelType = protocol::ChannelType::Control;
    binding.required = true;
    binding.shared = true;
    binding.consumes = {protocol::PacketType::Exchange};
    binding.produces = {protocol::PacketType::Exchange};
    return binding;
}

module::ModulePeerCompatibility peerCompatibility(const std::string& peerModuleId)
{
    module::ModulePeerCompatibility peer;
    peer.peerModuleId = peerModuleId;
    peer.minPeerVersion = module::ModuleVersion{1, 0, 0};
    peer.maxPeerVersion = module::ModuleVersion{1, 99, 99};
    peer.compatibilityMode = "echo-v1";
    return peer;
}

module::ModuleManifest makeManifest(session::SessionRole role)
{
    const bool client = role == session::SessionRole::Client;

    module::ModuleManifest manifest;
    manifest.moduleId = client ? "test.echo.client" : "test.echo.agent";
    manifest.displayName = client ? "Test Echo Client" : "Test Echo Agent";
    manifest.sku = "fusiondesk.module.test.echo";
    manifest.version = module::ModuleVersion{1, 0, 0};
    manifest.feature = 0;
    manifest.roleFlags = client ? module::ModuleRoleClient : module::ModuleRoleAgent;
    manifest.runModeFlags = inProcessHosted;
    manifest.standaloneSaleUnit = false;
    manifest.disableAtRuntime = true;
    manifest.supportedPlatforms = {"windows", "linux", "macos", "android"};
    manifest.compatiblePeers = {
        peerCompatibility(client ? "test.echo.agent" : "test.echo.client")};
    manifest.channels = {controlBinding()};
    return manifest;
}

bool roleSupported(session::SessionRole role)
{
    return role == session::SessionRole::Client ||
           role == session::SessionRole::Agent;
}

bool matchesRequestedId(const std::string& requestedModuleId,
                        session::SessionRole role)
{
    if (requestedModuleId == "test.echo")
        return true;

    if (role == session::SessionRole::Client)
        return requestedModuleId == "test.echo.client";

    if (role == session::SessionRole::Agent)
        return requestedModuleId == "test.echo.agent";

    return false;
}

} // namespace

bool TestEchoModuleFactory::supports(const std::string& requestedModuleId,
                                     const module::ModuleCreateOptions& options) const
{
    return roleSupported(options.role) &&
           matchesRequestedId(requestedModuleId, options.role);
}

module::ModuleManifest TestEchoModuleFactory::manifest(
    const module::ModuleCreateOptions& options) const
{
    if (!roleSupported(options.role))
        return {};

    return makeManifest(options.role);
}

std::shared_ptr<module::IModule> TestEchoModuleFactory::create(
    const module::ModuleCreateOptions& options) const
{
    if (!roleSupported(options.role))
        return nullptr;

    return std::make_shared<TestEchoModule>(makeManifest(options.role));
}

module::ModuleManifest testEchoClientManifest()
{
    return makeManifest(session::SessionRole::Client);
}

module::ModuleManifest testEchoAgentManifest()
{
    return makeManifest(session::SessionRole::Agent);
}

std::shared_ptr<module::IModuleFactory> makeTestEchoModuleFactory()
{
    return std::make_shared<TestEchoModuleFactory>();
}

} // namespace test
} // namespace modules
} // namespace fusiondesk
