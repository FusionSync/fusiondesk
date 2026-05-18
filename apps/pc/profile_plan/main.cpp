#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_coordinator.h"

namespace {

bool hasArg(int argc, char** argv, const std::string& name)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && name == argv[i])
            return true;
    }
    return false;
}

std::string optionValue(int argc, char** argv, const std::string& name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] != nullptr && name == argv[i] && argv[i + 1] != nullptr)
            return argv[i + 1];
    }
    return {};
}

std::vector<std::string> optionValues(int argc, char** argv, const std::string& name)
{
    std::vector<std::string> values;
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] != nullptr && name == argv[i] && argv[i + 1] != nullptr)
            values.push_back(argv[i + 1]);
    }
    return values;
}

bool parseSessionId(const std::string& value,
                    fusiondesk::protocol::SessionId& parsed)
{
    if (value.empty())
        return true;

    char* end = nullptr;
    const unsigned long long number = std::strtoull(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0')
        return false;

    parsed = static_cast<fusiondesk::protocol::SessionId>(number);
    return true;
}

const fusiondesk::network::ChannelSpec* findSpecByName(
    const std::vector<fusiondesk::network::ChannelSpec>& specs,
    const std::string& name)
{
    for (const fusiondesk::network::ChannelSpec& spec : specs) {
        if (spec.name == name)
            return &spec;
    }
    return nullptr;
}

std::string knownChannelNames(const std::vector<fusiondesk::network::ChannelSpec>& specs)
{
    std::string names;
    for (const fusiondesk::network::ChannelSpec& spec : specs) {
        if (!names.empty())
            names += ", ";
        names += spec.name;
    }
    return names;
}

bool parseChannelPlan(const std::string& value,
                      const std::vector<fusiondesk::network::ChannelSpec>& specs,
                      const std::string& clientReadyPrefix,
                      const std::string& agentReadyPrefix,
                      fusiondesk::runtime::qt::QtTcpPeerProfilePlanChannel& channel,
                      std::string& error)
{
    const std::size_t delimiter = value.find('=');
    if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size()) {
        error = "channel must use name=host:port";
        return false;
    }

    const std::string name = value.substr(0, delimiter);
    const std::string endpoint = value.substr(delimiter + 1);
    const fusiondesk::network::ChannelSpec* spec = findSpecByName(specs, name);
    if (spec == nullptr) {
        error = "unknown channel '" + name + "', known channels: " + knownChannelNames(specs);
        return false;
    }

    channel.key = spec->key;
    channel.endpoint = endpoint;
    channel.clientReadyEndpoint = clientReadyPrefix + ":" + spec->name;
    channel.agentReadyEndpoint = agentReadyPrefix + ":" + spec->name;
    return true;
}

void printUsage()
{
    std::cout
        << "Usage: fusiondesk_pc_profile_plan "
        << "--client-profile <path> --agent-profile <path> "
        << "--channel <name=host:port> [--channel <name=host:port> ...] "
        << "[--client-session-id <id>] [--agent-session-id <id>] "
        << "[--client-ready-prefix <name>] [--agent-ready-prefix <name>]\n"
        << "\n"
        << "Known PC startup channels: control, small_data, main_screen, large_data\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        printUsage();
        return 0;
    }

    const std::string clientProfilePath = optionValue(argc, argv, "--client-profile");
    const std::string agentProfilePath = optionValue(argc, argv, "--agent-profile");
    const std::vector<std::string> channelValues = optionValues(argc, argv, "--channel");
    if (clientProfilePath.empty() || agentProfilePath.empty() || channelValues.empty()) {
        printUsage();
        return 2;
    }

    std::vector<fusiondesk::network::ChannelSpec> knownSpecs =
        fusiondesk::network::defaultMvpChannelSpecs();
    knownSpecs.push_back(fusiondesk::network::defaultLargeDataChannelSpec());

    fusiondesk::runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = knownSpecs;
    options.clientProfilePath = clientProfilePath;
    options.agentProfilePath = agentProfilePath;

    const std::string clientReadyPrefix =
        optionValue(argc, argv, "--client-ready-prefix").empty()
            ? "pc-client"
            : optionValue(argc, argv, "--client-ready-prefix");
    const std::string agentReadyPrefix =
        optionValue(argc, argv, "--agent-ready-prefix").empty()
            ? "pc-agent"
            : optionValue(argc, argv, "--agent-ready-prefix");

    for (const std::string& value : channelValues) {
        fusiondesk::runtime::qt::QtTcpPeerProfilePlanChannel channel;
        std::string error;
        if (!parseChannelPlan(value,
                              knownSpecs,
                              clientReadyPrefix,
                              agentReadyPrefix,
                              channel,
                              error)) {
            std::cerr << error << "\n";
            return 3;
        }
        options.channels.push_back(std::move(channel));
    }

    if (!parseSessionId(optionValue(argc, argv, "--client-session-id"),
                        options.clientSessionId) ||
        !parseSessionId(optionValue(argc, argv, "--agent-session-id"),
                        options.agentSessionId)) {
        std::cerr << "session id must be an unsigned integer\n";
        return 3;
    }

    const fusiondesk::runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const fusiondesk::runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    if (!planned.ok) {
        for (const std::string& message : planned.messages)
            std::cerr << message << "\n";
        return 4;
    }

    std::cout << "clientProfile=" << planned.clientProfilePath << "\n";
    std::cout << "agentProfile=" << planned.agentProfilePath << "\n";
    std::cout << "channels=" << planned.pair.clientProfile.tcpChannels.size() << "\n";
    return 0;
}
