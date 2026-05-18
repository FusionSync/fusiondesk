#include <cassert>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_catalog.h"

using namespace fusiondesk;

namespace {

const module::ChannelBinding* findChannel(const module::ModuleManifest& manifest, const std::string& name)
{
    for (const module::ChannelBinding& binding : manifest.channels) {
        if (binding.name == name)
            return &binding;
    }
    return nullptr;
}

bool containsPacket(const std::vector<protocol::PacketType>& packets, protocol::PacketType packetType)
{
    for (protocol::PacketType packet : packets) {
        if (packet == packetType)
            return true;
    }
    return false;
}

bool containsModule(const std::vector<module::ModuleManifest>& manifests, const std::string& moduleId)
{
    for (const module::ModuleManifest& manifest : manifests) {
        if (manifest.moduleId == moduleId)
            return true;
    }
    return false;
}

void displayAgentManifestIsRoleScoped()
{
    const module::ModuleManifest manifest = module::catalog::displayScreenAgent();
    assert(manifest.moduleId == "display.screen.agent");
    assert((manifest.roleFlags & module::ModuleRoleAgent) != 0);
    assert((manifest.roleFlags & module::ModuleRoleClient) == 0);
    assert(manifest.feature == protocol::feature::Screen);

    const module::ChannelBinding* main = findChannel(manifest, "main_screen");
    assert(main != nullptr);
    assert(main->required);
    assert(main->channelType == protocol::ChannelType::Video);
    assert(!containsPacket(main->consumes, protocol::PacketType::PayloadAck));
    assert(!containsPacket(main->consumes, protocol::PacketType::Video));
    assert(containsPacket(main->produces, protocol::PacketType::Video));
    assert(!containsPacket(main->produces, protocol::PacketType::PayloadAck));

    const module::ChannelBinding* smallData = findChannel(manifest, "small_data");
    assert(smallData != nullptr);
    assert(smallData->required);
    assert(smallData->shared);
    assert(smallData->channelType == protocol::ChannelType::Standard);
    assert(containsPacket(smallData->consumes, protocol::PacketType::PayloadAck));
    assert(containsPacket(smallData->produces, protocol::PacketType::PayloadAck));
}

void displayClientManifestIsRoleScoped()
{
    const module::ModuleManifest manifest = module::catalog::displayScreenClient();
    assert(manifest.moduleId == "display.screen.client");
    assert((manifest.roleFlags & module::ModuleRoleClient) != 0);
    assert((manifest.roleFlags & module::ModuleRoleAgent) == 0);
    assert(manifest.feature == protocol::feature::Screen);

    const module::ChannelBinding* main = findChannel(manifest, "main_screen");
    assert(main != nullptr);
    assert(main->required);
    assert(main->channelType == protocol::ChannelType::Video);
    assert(containsPacket(main->consumes, protocol::PacketType::Video));
    assert(!containsPacket(main->consumes, protocol::PacketType::PayloadAck));
    assert(!containsPacket(main->produces, protocol::PacketType::Video));
    assert(!containsPacket(main->produces, protocol::PacketType::PayloadAck));

    const module::ChannelBinding* smallData = findChannel(manifest, "small_data");
    assert(smallData != nullptr);
    assert(smallData->required);
    assert(smallData->shared);
    assert(smallData->channelType == protocol::ChannelType::Standard);
    assert(containsPacket(smallData->consumes, protocol::PacketType::PayloadAck));
    assert(containsPacket(smallData->produces, protocol::PacketType::PayloadAck));
}

void roleSelectionFiltersDisplayManifests()
{
    const std::vector<module::ModuleManifest> agent =
        module::catalog::remoteDesktopSuiteForRole(module::ModuleRoleAgent);
    assert(containsModule(agent, "display.screen.agent"));
    assert(!containsModule(agent, "display.screen.client"));

    const std::vector<module::ModuleManifest> client =
        module::catalog::remoteDesktopSuiteForRole(module::ModuleRoleClient);
    assert(containsModule(client, "display.screen.client"));
    assert(!containsModule(client, "display.screen.agent"));
}

void clipboardManifestsAreRoleScopedAndUseSmallData()
{
    const module::ModuleManifest agent = module::catalog::clipboardRedirectAgent();
    assert(agent.moduleId == "clipboard.redirect.agent");
    assert((agent.roleFlags & module::ModuleRoleAgent) != 0);
    assert((agent.roleFlags & module::ModuleRoleClient) == 0);
    assert(agent.feature == protocol::feature::Clipboard);
    assert(agent.compatiblePeers.front().peerModuleId == "clipboard.redirect.client");

    const module::ChannelBinding* agentSmallData = findChannel(agent, "small_data");
    assert(agentSmallData != nullptr);
    assert(agentSmallData->required);
    assert(agentSmallData->shared);
    assert(agentSmallData->channelType == protocol::ChannelType::Standard);
    assert(containsPacket(agentSmallData->consumes, protocol::PacketType::Clipboard));
    assert(containsPacket(agentSmallData->produces, protocol::PacketType::Clipboard));
    const module::ChannelBinding* agentLargeData = findChannel(agent, "large_data");
    assert(agentLargeData != nullptr);
    assert(!agentLargeData->required);
    assert(agentLargeData->shared);
    assert(agentLargeData->channelType == protocol::ChannelType::Standard);
    assert(containsPacket(agentLargeData->consumes, protocol::PacketType::Clipboard));
    assert(containsPacket(agentLargeData->produces, protocol::PacketType::Clipboard));

    const module::ModuleManifest client = module::catalog::clipboardRedirectClient();
    assert(client.moduleId == "clipboard.redirect.client");
    assert((client.roleFlags & module::ModuleRoleClient) != 0);
    assert((client.roleFlags & module::ModuleRoleAgent) == 0);
    assert(client.feature == protocol::feature::Clipboard);
    assert(client.compatiblePeers.front().peerModuleId == "clipboard.redirect.agent");

    const module::ChannelBinding* clientSmallData = findChannel(client, "small_data");
    assert(clientSmallData != nullptr);
    assert(containsPacket(clientSmallData->consumes, protocol::PacketType::Clipboard));
    assert(containsPacket(clientSmallData->produces, protocol::PacketType::Clipboard));
    const module::ChannelBinding* clientLargeData = findChannel(client, "large_data");
    assert(clientLargeData != nullptr);
    assert(!clientLargeData->required);
    assert(containsPacket(clientLargeData->consumes, protocol::PacketType::Clipboard));
    assert(containsPacket(clientLargeData->produces, protocol::PacketType::Clipboard));
}

void clipboardAliasDoesNotHideBothDirections()
{
    const module::ModuleManifest manifest = module::catalog::clipboardRedirect();
    assert(manifest.moduleId == "clipboard.redirect");
    assert((manifest.roleFlags & module::ModuleRoleAgent) != 0);
    assert((manifest.roleFlags & module::ModuleRoleClient) != 0);
    assert(manifest.feature == protocol::feature::Clipboard);
    assert(manifest.channels.empty());
}

void roleManifestSetContainsBothSides()
{
    const std::vector<module::ModuleManifest> manifests = module::catalog::displayScreenRoleManifests();
    assert(manifests.size() == 2);
    assert(containsModule(manifests, "display.screen.agent"));
    assert(containsModule(manifests, "display.screen.client"));
}

void displayScreenAliasDoesNotHideBothDirections()
{
    const module::ModuleManifest manifest = module::catalog::displayScreen();
    assert(manifest.moduleId == "display.screen");
    assert((manifest.roleFlags & module::ModuleRoleAgent) != 0);
    assert((manifest.roleFlags & module::ModuleRoleClient) != 0);
    assert(manifest.channels.empty());
}

} // namespace

int main()
{
    displayAgentManifestIsRoleScoped();
    displayClientManifestIsRoleScoped();
    roleSelectionFiltersDisplayManifests();
    clipboardManifestsAreRoleScopedAndUseSmallData();
    clipboardAliasDoesNotHideBothDirections();
    roleManifestSetContainsBothSides();
    displayScreenAliasDoesNotHideBothDirections();
    return 0;
}
