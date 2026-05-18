#include "fusiondesk/runtime/qt/qt_peer_profile_coordinator.h"

#include <utility>

#include <QFile>
#include <QHostAddress>
#include <QString>

#include "fusiondesk/runtime/connection/peer_coordination.h"
#include "fusiondesk/runtime/connection/peer_connection_plan.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

void appendFailure(QtTcpPeerProfilePlanResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

bool parseConcreteEndpoint(const std::string& endpoint)
{
    std::string value = endpoint;
    const std::string prefix = "tcp://";
    if (value.rfind(prefix, 0) == 0)
        value = value.substr(prefix.size());

    const std::size_t delimiter = value.rfind(':');
    if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size())
        return false;

    bool ok = false;
    const int parsedPort = QString::fromStdString(value.substr(delimiter + 1)).toInt(&ok);
    if (!ok || parsedPort <= 0 || parsedPort > 65535)
        return false;

    const QHostAddress address(QString::fromStdString(value.substr(0, delimiter)));
    return !address.isNull();
}

connection::PeerConnectionPlanRequest makeConnectionPlanRequest(
    const QtTcpPeerProfilePlanOptions& options,
    QtTcpPeerProfilePlanResult& result)
{
    connection::PeerConnectionPlanRequest request;
    request.knownSpecs = options.knownSpecs;

    if (!options.channels.empty()) {
        for (const QtTcpPeerProfilePlanChannel& channel : options.channels) {
            connection::PeerConnectionChannelRequest requested;
            requested.key = channel.key;
            requested.endpoint = channel.endpoint;
            requested.clientReadyEndpoint = channel.clientReadyEndpoint.empty()
                ? options.clientReadyEndpoint
                : channel.clientReadyEndpoint;
            requested.agentReadyEndpoint = channel.agentReadyEndpoint.empty()
                ? options.agentReadyEndpoint
                : channel.agentReadyEndpoint;
            request.channels.push_back(std::move(requested));
        }
        return request;
    }

    if (options.channelKeys.empty()) {
        appendFailure(result, "tcp peer profile plan requires one requested channel");
        return request;
    }

    if (options.channelKeys.size() > 1) {
        appendFailure(result, "single-endpoint tcp peer profile shorthand supports one requested channel");
        return request;
    }

    for (network::ChannelKey key : options.channelKeys) {
        connection::PeerConnectionChannelRequest requested;
        requested.key = key;
        requested.endpoint = options.endpoint;
        requested.clientReadyEndpoint = options.clientReadyEndpoint;
        requested.agentReadyEndpoint = options.agentReadyEndpoint;
        request.channels.push_back(std::move(requested));
    }

    return request;
}

connection::PeerCoordinationResult buildPeerCoordinationResult(
    const QtTcpPeerProfilePlanOptions& options,
    QtTcpPeerProfilePlanResult& result)
{
    connection::PeerCoordinationRequest request;
    request.profile.connectionPlan = makeConnectionPlanRequest(options, result);
    request.profile.clientSessionId = options.clientSessionId;
    request.profile.agentSessionId = options.agentSessionId;
    if (!result.messages.empty())
        return {};

    connection::PeerCoordinationResult resolved =
        connection::resolvePeerCoordination(request);
    for (std::string& message : resolved.messages)
        appendFailure(result, std::move(message));
    if (!resolved.ok)
        return resolved;

    for (const connection::PeerProfileConnectChannel& channel : resolved.plan.profile.pair.client.tcpChannels) {
        if (!parseConcreteEndpoint(channel.endpoint))
            appendFailure(result, "tcp peer profile plan endpoint must be a concrete tcp endpoint");
    }
    if (!result.messages.empty())
        return resolved;

    return resolved;
}

bool validatePlanOptions(const QtTcpPeerProfilePlanOptions& options,
                         QtTcpPeerProfilePlanResult& result)
{
    if (options.clientProfilePath.empty())
        appendFailure(result, "tcp peer profile plan requires client profile path");
    if (options.agentProfilePath.empty())
        appendFailure(result, "tcp peer profile plan requires agent profile path");
    if (!options.clientProfilePath.empty() &&
        options.clientProfilePath == options.agentProfilePath) {
        appendFailure(result, "tcp peer profile plan requires distinct client and agent profile paths");
    }

    return result.messages.empty();
}

QtTcpPeerProfilePair makeQtPairFromPeerProfilePair(const connection::PeerProfilePair& source)
{
    QtTcpPeerProfilePair pair;
    pair.clientProfile.sessionId = source.client.sessionId;
    pair.agentProfile.sessionId = source.agent.sessionId;

    for (const connection::PeerProfileConnectChannel& channel : source.client.tcpChannels) {
        QtTcpChannelProfile clientChannel;
        clientChannel.spec = channel.spec;
        clientChannel.endpoint = channel.endpoint;
        clientChannel.ready.endpoint = channel.readyEndpoint;
        pair.clientProfile.tcpChannels.push_back(std::move(clientChannel));
    }

    for (const connection::PeerProfileListenChannel& channel : source.agent.tcpListenChannels) {
        QtTcpListenChannelProfile agentChannel;
        agentChannel.spec = channel.spec;
        agentChannel.endpoint = channel.endpoint;
        agentChannel.ready.endpoint = channel.readyEndpoint;
        pair.agentProfile.tcpListenChannels.push_back(std::move(agentChannel));
    }

    return pair;
}

} // namespace

QtTcpPeerProfilePlanResult QtTcpPeerProfileCoordinator::saveLocalTcpPeerProfiles(
    const QtTcpPeerProfilePlanOptions& options) const
{
    QtTcpPeerProfilePlanResult result;
    result.clientProfilePath = options.clientProfilePath;
    result.agentProfilePath = options.agentProfilePath;

    const connection::PeerCoordinationResult resolved =
        buildPeerCoordinationResult(options, result);
    if (!result.messages.empty())
        return result;

    validatePlanOptions(options, result);
    if (!result.messages.empty())
        return result;

    result.pair = makeQtPairFromPeerProfilePair(resolved.plan.profile.pair);

    const QtTransportProfileSaveResult clientSaved =
        saveTcpTransportProfilesToJsonFile(options.clientProfilePath,
                                           {result.pair.clientProfile});
    if (!clientSaved.ok) {
        appendFailure(result, "client profile save failed: " + clientSaved.message);
        return result;
    }

    const QtTransportProfileSaveResult agentSaved =
        saveTcpTransportProfilesToJsonFile(options.agentProfilePath,
                                           {result.pair.agentProfile});
    if (!agentSaved.ok) {
        QFile::remove(QString::fromStdString(options.clientProfilePath));
        appendFailure(result, "agent profile save failed: " + agentSaved.message);
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
