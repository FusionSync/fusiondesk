#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"
#include "pc_app_shell.h"

int main()
{
    namespace pc = fusiondesk::apps::pc;

    const fusiondesk::network::ChannelSpec screenSpec =
        fusiondesk::network::defaultMvpChannelSpecs().back();
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path profilePath =
        std::filesystem::temp_directory_path() /
        ("fusiondesk_pc_agent_listen_profile_" + std::to_string(now) + ".json");

    const fusiondesk::runtime::qt::QtTcpPeerProfilePair pair =
        fusiondesk::runtime::qt::makeTcpPeerProfilePair({screenSpec},
                                                         "127.0.0.1:0",
                                                         {},
                                                         "pc-agent-listen-smoke");
    const fusiondesk::runtime::qt::QtTransportProfileSaveResult saved =
        fusiondesk::runtime::qt::saveTcpTransportProfilesToJsonFile(
            profilePath.string(),
            {pair.agentProfile});
    assert(saved.ok);

    std::vector<std::string> args = {
        "fusiondesk_pc_agent_listen_profile_smoke",
        "--smoke",
        "--listen-profile",
        profilePath.string(),
        "--run-ms",
        "1",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    const int result = pc::runPcShell(argc, argv.data(), pc::PcShellRole::Agent);
    std::filesystem::remove(profilePath);
    assert(result == 0);
    return 0;
}
