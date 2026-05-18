#include <cassert>
#include <string>

#include <QCoreApplication>
#include <QTemporaryDir>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_coordinator.h"

using namespace fusiondesk;

namespace {

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

network::ChannelKey controlKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                               protocol::ChannelType::Control};
}

void savesRequestedLocalPeerProfiles()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    const std::string clientPath =
        directory.filePath(QStringLiteral("client-profile.json")).toStdString();
    const std::string agentPath =
        directory.filePath(QStringLiteral("agent-profile.json")).toStdString();

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channelKeys = {screenKey()};
    options.endpoint = "127.0.0.1:49101";
    options.clientReadyEndpoint = "coordinator-client";
    options.agentReadyEndpoint = "coordinator-agent";
    options.clientProfilePath = clientPath;
    options.agentProfilePath = agentPath;
    options.clientSessionId = 101;
    options.agentSessionId = 202;

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(planned.ok);
    assert(planned.clientProfilePath == clientPath);
    assert(planned.agentProfilePath == agentPath);
    assert(planned.pair.clientProfile.tcpChannels.size() == 1);
    assert(planned.pair.agentProfile.tcpListenChannels.size() == 1);
    assert(planned.pair.clientProfile.tcpChannels.front().endpoint == options.endpoint);
    assert(planned.pair.agentProfile.tcpListenChannels.front().endpoint == options.endpoint);

    const runtime::qt::QtTransportProfileLoadResult clientLoaded =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(clientPath, options.knownSpecs);
    assert(clientLoaded.ok);
    assert(clientLoaded.profiles.size() == 1);
    assert(clientLoaded.profiles.front().sessionId == 101);
    assert(clientLoaded.profiles.front().tcpChannels.size() == 1);
    assert(clientLoaded.profiles.front().tcpListenChannels.empty());
    assert(clientLoaded.profiles.front().tcpChannels.front().ready.endpoint ==
           options.clientReadyEndpoint);

    const runtime::qt::QtTransportProfileLoadResult agentLoaded =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(agentPath, options.knownSpecs);
    assert(agentLoaded.ok);
    assert(agentLoaded.profiles.size() == 1);
    assert(agentLoaded.profiles.front().sessionId == 202);
    assert(agentLoaded.profiles.front().tcpChannels.empty());
    assert(agentLoaded.profiles.front().tcpListenChannels.size() == 1);
    assert(agentLoaded.profiles.front().tcpListenChannels.front().ready.endpoint ==
           options.agentReadyEndpoint);
}

void savesMultiEndpointPeerProfiles()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channels = {
        runtime::qt::QtTcpPeerProfilePlanChannel{
            controlKey(),
            "127.0.0.1:49111",
            "client-control",
            "agent-control"},
        runtime::qt::QtTcpPeerProfilePlanChannel{
            screenKey(),
            "127.0.0.1:49112",
            "client-screen",
            "agent-screen"},
    };
    options.clientProfilePath =
        directory.filePath(QStringLiteral("client-profile.json")).toStdString();
    options.agentProfilePath =
        directory.filePath(QStringLiteral("agent-profile.json")).toStdString();
    options.clientSessionId = 303;
    options.agentSessionId = 404;

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(planned.ok);
    assert(planned.pair.clientProfile.tcpChannels.size() == 2);
    assert(planned.pair.agentProfile.tcpListenChannels.size() == 2);
    assert(planned.pair.clientProfile.tcpChannels.front().endpoint == "127.0.0.1:49111");
    assert(planned.pair.clientProfile.tcpChannels.back().endpoint == "127.0.0.1:49112");
    assert(planned.pair.agentProfile.tcpListenChannels.front().ready.endpoint ==
           "agent-control");
    assert(planned.pair.agentProfile.tcpListenChannels.back().ready.endpoint ==
           "agent-screen");

    const runtime::qt::QtTransportProfileLoadResult clientLoaded =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(options.clientProfilePath,
                                                          options.knownSpecs);
    assert(clientLoaded.ok);
    assert(clientLoaded.profiles.front().sessionId == 303);
    assert(clientLoaded.profiles.front().tcpChannels.size() == 2);

    const runtime::qt::QtTransportProfileLoadResult agentLoaded =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(options.agentProfilePath,
                                                          options.knownSpecs);
    assert(agentLoaded.ok);
    assert(agentLoaded.profiles.front().sessionId == 404);
    assert(agentLoaded.profiles.front().tcpListenChannels.size() == 2);
}

void rejectsMissingRequestedChannel()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.endpoint = "tcp://127.0.0.1:49102";
    options.clientProfilePath =
        directory.filePath(QStringLiteral("client-profile.json")).toStdString();
    options.agentProfilePath =
        directory.filePath(QStringLiteral("agent-profile.json")).toStdString();

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(!planned.ok);
    assert(!planned.messages.empty());
    assert(planned.pair.clientProfile.tcpChannels.empty());
    assert(planned.pair.agentProfile.tcpListenChannels.empty());
}

void savesNoSessionIdProfileShapeForPcShell()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channelKeys = {screenKey()};
    options.endpoint = "tcp://127.0.0.1:49102";
    options.clientProfilePath =
        directory.filePath(QStringLiteral("client-profile.json")).toStdString();
    options.agentProfilePath =
        directory.filePath(QStringLiteral("agent-profile.json")).toStdString();

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(planned.ok);
    assert(planned.pair.clientProfile.sessionId == 0);
    assert(planned.pair.agentProfile.sessionId == 0);
    assert(planned.pair.clientProfile.tcpChannels.size() == 1);
    assert(planned.pair.agentProfile.tcpListenChannels.size() == 1);

    const runtime::qt::QtTransportProfileLoadResult directLoad =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(options.clientProfilePath,
                                                          options.knownSpecs);
    assert(!directLoad.ok);
}

void rejectsMultipleRequestedChannelsForSingleEndpoint()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channelKeys = {
        controlKey(),
        screenKey(),
    };
    options.endpoint = "127.0.0.1:49103";
    options.clientProfilePath =
        directory.filePath(QStringLiteral("client-profile.json")).toStdString();
    options.agentProfilePath =
        directory.filePath(QStringLiteral("agent-profile.json")).toStdString();

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(!planned.ok);
    assert(!planned.messages.empty());
    assert(planned.pair.clientProfile.tcpChannels.empty());
    assert(planned.pair.agentProfile.tcpListenChannels.empty());
}

void rejectsDuplicateMultiEndpointChannels()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channels = {
        runtime::qt::QtTcpPeerProfilePlanChannel{
            controlKey(),
            "127.0.0.1:49121",
            {},
            {}},
        runtime::qt::QtTcpPeerProfilePlanChannel{
            controlKey(),
            "127.0.0.1:49122",
            {},
            {}},
        runtime::qt::QtTcpPeerProfilePlanChannel{
            screenKey(),
            "127.0.0.1:49121",
            {},
            {}},
    };
    options.clientProfilePath =
        directory.filePath(QStringLiteral("client-profile.json")).toStdString();
    options.agentProfilePath =
        directory.filePath(QStringLiteral("agent-profile.json")).toStdString();

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(!planned.ok);
    assert(!planned.messages.empty());
    assert(planned.pair.clientProfile.tcpChannels.empty());
    assert(planned.pair.agentProfile.tcpListenChannels.empty());
}

void rejectsInvalidPlans()
{
    QTemporaryDir directory;
    assert(directory.isValid());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channelKeys = {
        network::ChannelKey{static_cast<protocol::ChannelId>(9999),
                            protocol::ChannelType::Video},
    };
    options.endpoint = "127.0.0.1:0";
    options.clientProfilePath =
        directory.filePath(QStringLiteral("same-profile.json")).toStdString();
    options.agentProfilePath = options.clientProfilePath;

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(!planned.ok);
    assert(!planned.messages.empty());
    assert(planned.pair.clientProfile.tcpChannels.empty());
    assert(planned.pair.agentProfile.tcpListenChannels.empty());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    savesRequestedLocalPeerProfiles();
    savesMultiEndpointPeerProfiles();
    rejectsMissingRequestedChannel();
    savesNoSessionIdProfileShapeForPcShell();
    rejectsMultipleRequestedChannelsForSingleEndpoint();
    rejectsDuplicateMultiEndpointChannels();
    rejectsInvalidPlans();
    return 0;
}
