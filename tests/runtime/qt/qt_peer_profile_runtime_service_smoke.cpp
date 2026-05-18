#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_runtime_service.h"

using namespace fusiondesk;

namespace {

network::ChannelKey controlKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
}

network::ChannelSpec specFor(network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : network::defaultMvpChannelSpecs()) {
        if (spec.key == key)
            return spec;
    }
    assert(false);
    return {};
}

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Login,
                                protocol::PacketType::Control,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video,
                                protocol::PacketType::CursorChange,
                                protocol::PacketType::Watermark};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Ack,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::Progress,
                                 protocol::MessageKind::StreamEnd};
    return capabilities;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "qt-peer-profile-user";
    options.context.tenantId = "qt-peer-profile-tenant";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

std::uint16_t reserveLocalPort()
{
    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 0));
    const std::uint16_t port = static_cast<std::uint16_t>(server.serverPort());
    server.close();
    return port;
}

void pollTransports(runtime::qt::QtRuntimeTransportManager& manager,
                    const std::vector<protocol::SessionId>& sessions)
{
    for (protocol::SessionId sessionId : sessions) {
        runtime::qt::QtSessionTransportConnector* connector =
            manager.connector(sessionId);
        if (connector == nullptr)
            continue;
        for (const auto& transport : connector->transports())
            transport->poll();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

bool waitUntil(const std::function<bool()>& done,
               runtime::qt::QtRuntimeTransportManager& manager,
               const std::vector<protocol::SessionId>& sessions,
               int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        pollTransports(manager, sessions);
    return done();
}

runtime::connection::PeerProfileExchangeRequest profileRequest(
    protocol::SessionId clientId,
    protocol::SessionId agentId,
    std::uint16_t screenPort)
{
    runtime::connection::PeerProfileExchangeRequest request;
    request.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:" + std::to_string(screenPort),
            "client-screen-ready-from-fdpp",
            "agent-screen-ready-from-fdpp"},
    };
    request.clientSessionId = clientId;
    request.agentSessionId = agentId;
    return request;
}

void qtRuntimeAppliesPeerProfileExchangeOverControlChannel()
{
    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId =
        host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    session::Session* agent = host.sessions().find(agentId);
    assert(client != nullptr);
    assert(agent != nullptr);
    assert(client->start());
    assert(agent->start());
    assert(client->network() != nullptr);
    assert(agent->network() != nullptr);

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const std::uint16_t controlPort = reserveLocalPort();
    const std::uint16_t screenPort = reserveLocalPort();

    runtime::qt::QtTcpListenChannelProfile controlListen;
    controlListen.spec = specFor(controlKey());
    controlListen.endpoint = "127.0.0.1:" + std::to_string(controlPort);
    controlListen.ready.endpoint = "agent-control-ready";
    const runtime::qt::QtTransportConnectResult agentListening =
        transportManager.listenTcpChannels(agentId, {controlListen});
    assert(agentListening.ok);
    assert(agentListening.listeningChannels.size() == 1);

    runtime::qt::QtTcpChannelProfile controlConnect;
    controlConnect.spec = controlListen.spec;
    controlConnect.endpoint = controlListen.endpoint;
    controlConnect.ready.endpoint = "client-control-ready";
    const runtime::qt::QtTransportConnectResult clientConnected =
        transportManager.connectTcpChannels(clientId, {controlConnect});
    assert(clientConnected.ok);
    assert(clientConnected.readyChannels.size() == 1);
    assert(waitUntil([agent]() {
                         return agent->network()->registry().isReady(controlKey());
                     },
                     transportManager,
                     {clientId, agentId}));

    runtime::qt::QtPeerProfileRuntimeService agentProfileRuntime(
        transportManager,
        agent->network()->router(),
        5000);
    runtime::qt::QtPeerProfileRuntimeServiceStartOptions agentStart;
    agentStart.runtime.subscribeResponses = false;
    agentStart.runtime.responder.firstResponseMessageId = 6000;
    agentStart.startTimer = false;
    assert(agentProfileRuntime.start(agentStart).ok);

    runtime::qt::QtPeerProfileRuntimeService clientProfileRuntime(
        transportManager,
        client->network()->router(),
        7000);
    runtime::qt::QtPeerProfileRuntimeServiceStartOptions clientStart;
    clientStart.runtime.startResponder = false;
    clientStart.startTimer = false;
    assert(clientProfileRuntime.start(clientStart).ok);

    runtime::connection::PeerProfileRuntimeExchangeOptions requestOptions;
    requestOptions.wire.messageId = 0;
    requestOptions.wire.timeoutMs = 1500;
    requestOptions.wire.monotonicTimestampUsec = 10000;
    const runtime::connection::PeerProfileRuntimeDispatchResult requested =
        clientProfileRuntime.requestPeerProfile(
            profileRequest(clientId, agentId, screenPort),
            requestOptions);
    assert(requested.ok);
    assert(requested.request.messageId == 7000);

    assert(waitUntil([&clientProfileRuntime]() {
                         return clientProfileRuntime.snapshot()
                                    .runtime.completedResponses == 1;
                     },
                     transportManager,
                     {clientId, agentId}));

    const runtime::qt::QtPeerProfileRuntimeApplyResult clientApplied =
        clientProfileRuntime.applyCompletedClientProfiles();
    assert(clientApplied.ok);
    assert(clientApplied.appliedCompletions == 1);
    assert(clientApplied.readyChannels.size() == 1);
    assert(client->network()->registry().isReady(screenKey()));

    assert(waitUntil([agent]() {
                         return agent->network()->registry().isReady(screenKey());
                     },
                     transportManager,
                     {clientId, agentId}));

    const runtime::qt::QtPeerProfileRuntimeServiceSnapshot agentSnapshot =
        agentProfileRuntime.snapshot();
    assert(agentSnapshot.runtime.responder.handledRequests == 1);
    assert(agentSnapshot.runtime.responder.sentResponses == 1);
    assert(agent->network()->socketSnapshotForChannel(screenKey()).state ==
           network::SocketState::Open);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qtRuntimeAppliesPeerProfileExchangeOverControlChannel();
    return 0;
}
