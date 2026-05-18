#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"

#ifndef FUSIONDESK_PC_PROFILE_PLAN_EXE
#error FUSIONDESK_PC_PROFILE_PLAN_EXE is required
#endif

namespace {

QString exePath()
{
    return QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE);
}

std::filesystem::path pathIn(QTemporaryDir& directory, const char* name)
{
    return std::filesystem::path(directory.path().toStdString()) / name;
}

void runProfilePlanToolWritesMultiChannelProfiles()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    const std::filesystem::path clientProfile = pathIn(directory, "client-profile.json");
    const std::filesystem::path agentProfile = pathIn(directory, "agent-profile.json");

    QProcess process;
    process.start(exePath(),
                  {"--client-profile",
                   QString::fromStdString(clientProfile.string()),
                   "--agent-profile",
                   QString::fromStdString(agentProfile.string()),
                   "--client-session-id",
                   "101",
                   "--agent-session-id",
                   "202",
                   "--client-ready-prefix",
                   "client-ready",
                   "--agent-ready-prefix",
                   "agent-ready",
                   "--channel",
                   "control=127.0.0.1:47101",
                   "--channel",
                   "small_data=127.0.0.1:47102",
                   "--channel",
                   "main_screen=127.0.0.1:47103"});

    assert(process.waitForStarted(3000));
    assert(process.waitForFinished(5000));
    assert(process.exitStatus() == QProcess::NormalExit);
    assert(process.exitCode() == 0);
    assert(std::filesystem::exists(clientProfile));
    assert(std::filesystem::exists(agentProfile));

    const std::vector<fusiondesk::network::ChannelSpec> knownSpecs =
        fusiondesk::network::defaultMvpChannelSpecs();
    const fusiondesk::runtime::qt::QtTransportProfileLoadResult clientLoaded =
        fusiondesk::runtime::qt::loadTcpTransportProfilesFromJsonFile(
            clientProfile.string(),
            knownSpecs);
    const fusiondesk::runtime::qt::QtTransportProfileLoadResult agentLoaded =
        fusiondesk::runtime::qt::loadTcpTransportProfilesFromJsonFile(
            agentProfile.string(),
            knownSpecs);

    assert(clientLoaded.ok);
    assert(agentLoaded.ok);
    assert(clientLoaded.profiles.size() == 1);
    assert(agentLoaded.profiles.size() == 1);
    assert(clientLoaded.profiles.front().sessionId == 101);
    assert(agentLoaded.profiles.front().sessionId == 202);
    assert(clientLoaded.profiles.front().tcpChannels.size() == 3);
    assert(clientLoaded.profiles.front().tcpListenChannels.empty());
    assert(agentLoaded.profiles.front().tcpChannels.empty());
    assert(agentLoaded.profiles.front().tcpListenChannels.size() == 3);

    assert(clientLoaded.profiles.front().tcpChannels.front().spec.name == "control");
    assert(clientLoaded.profiles.front().tcpChannels[1].spec.name == "small_data");
    assert(clientLoaded.profiles.front().tcpChannels.back().spec.name == "main_screen");
    assert(agentLoaded.profiles.front().tcpListenChannels.front().spec.name == "control");
    assert(agentLoaded.profiles.front().tcpListenChannels[1].spec.name == "small_data");
    assert(agentLoaded.profiles.front().tcpListenChannels.back().spec.name == "main_screen");
    assert(clientLoaded.profiles.front().tcpChannels.front().ready.endpoint ==
           "client-ready:control");
    assert(clientLoaded.profiles.front().tcpChannels[1].ready.endpoint ==
           "client-ready:small_data");
    assert(agentLoaded.profiles.front().tcpListenChannels.back().ready.endpoint ==
           "agent-ready:main_screen");
}

void rejectsUnknownChannelName()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    QProcess process;
    process.start(exePath(),
                  {"--client-profile",
                   QString::fromStdString(pathIn(directory, "client.json").string()),
                   "--agent-profile",
                   QString::fromStdString(pathIn(directory, "agent.json").string()),
                   "--channel",
                   "missing=127.0.0.1:47103"});

    assert(process.waitForStarted(3000));
    assert(process.waitForFinished(5000));
    assert(process.exitStatus() == QProcess::NormalExit);
    assert(process.exitCode() == 3);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    runProfilePlanToolWritesMultiChannelProfiles();
    rejectsUnknownChannelName();
    return 0;
}
