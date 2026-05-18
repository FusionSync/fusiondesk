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
#include <QTcpSocket>
#include <QTemporaryFile>

#include "fusiondesk/adapters/qt/qt_channel_binder.h"
#include "fusiondesk/adapters/qt/qt_packet_channel.h"
#include "fusiondesk/adapters/qt/qt_tcp_transport_socket.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_manager.h"
#include "fusiondesk/core/protocol/packet_codec.h"
#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/runtime/connection/reconnect_coordinator.h"
#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_handler.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_coordinator.h"
#include "fusiondesk/runtime/qt/qt_reconnect_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"

using namespace fusiondesk;

namespace {

protocol::PacketEnvelope makeRequest()
{
    protocol::PacketEnvelope packet;
    packet.sessionId = 77;
    packet.traceId = 88;
    packet.messageId = 11;
    packet.correlationId = 11;
    packet.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    packet.channelType = protocol::ChannelType::Video;
    packet.packetType = protocol::PacketType::PayloadAck;
    packet.messageKind = protocol::MessageKind::Request;
    packet.priority = protocol::PacketPriority::Interactive;
    packet.sequence = 1;
    packet.monotonicTimestampUsec = 100;
    packet.timeoutMs = 1000;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.payload = {1, 2, 3, 4};
    return packet;
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
                                 protocol::MessageKind::Error};
    return capabilities;
}

network::ChannelKey controlKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
                               protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

network::ChannelKey smallDataKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
                               protocol::ChannelType::Standard};
}

network::ChannelSpec specForKey(network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : network::defaultMvpChannelSpecs()) {
        if (spec.key == key)
            return spec;
    }
    assert(false);
    return network::ChannelSpec{};
}

class CapturingChannel : public network::IChannel
{
public:
    explicit CapturingChannel(network::ChannelKey key)
        : key_(key)
    {
    }

    protocol::ChannelId id() const override
    {
        return key_.channelId;
    }

    protocol::ChannelType type() const override
    {
        return key_.channelType;
    }

    bool isOpen() const override
    {
        return open_;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    network::ChannelKey key_;
    bool open_ = true;
    std::vector<protocol::PacketEnvelope> sentPackets;
};

class FakeCapture : public modules::display::IDisplayCapture
{
public:
    modules::display::CapturedFrame captureNextFrame(bool keyFrame) override
    {
        modules::display::CapturedFrame frame;
        frame.frameId = ++frames_;
        frame.keyFrame = keyFrame;
        frame.pixels = {static_cast<std::uint8_t>(frame.frameId), 42};
        return frame;
    }

private:
    std::uint64_t frames_ = 0;
};

class FakeEncoder : public modules::display::IVideoEncoder
{
public:
    modules::display::EncodedFrame encode(const modules::display::CapturedFrame& frame) override
    {
        modules::display::EncodedFrame encoded;
        encoded.frameId = frame.frameId;
        encoded.keyFrame = frame.keyFrame;
        encoded.payload = frame.pixels;
        return encoded;
    }
};

class FakeDecoder : public modules::display::IVideoDecoder
{
public:
    modules::display::DecodedFrame decode(const modules::display::EncodedFrame& frame) override
    {
        modules::display::DecodedFrame decoded;
        decoded.frameId = frame.frameId;
        decoded.keyFrame = frame.keyFrame;
        decoded.pixels = frame.payload;
        return decoded;
    }
};

class FakeRenderer : public modules::display::IDisplayRenderer
{
public:
    bool render(const modules::display::DecodedFrame& frame) override
    {
        lastFrameId_ = frame.frameId;
        lastKeyFrame_ = frame.keyFrame;
        ++renderedFrames_;
        return true;
    }

    int renderedFrames() const
    {
        return renderedFrames_;
    }

    std::uint64_t lastFrameId() const
    {
        return lastFrameId_;
    }

    bool lastKeyFrame() const
    {
        return lastKeyFrame_;
    }

private:
    int renderedFrames_ = 0;
    std::uint64_t lastFrameId_ = 0;
    bool lastKeyFrame_ = false;
};

protocol::PacketEnvelope makeResponse(const protocol::PacketEnvelope& request)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = request.sessionId;
    packet.traceId = request.traceId;
    packet.messageId = 12;
    packet.correlationId = request.correlationId;
    packet.responseTo = request.messageId;
    packet.channelId = request.channelId;
    packet.channelType = request.channelType;
    packet.packetType = protocol::PacketType::PayloadAck;
    packet.messageKind = protocol::MessageKind::Response;
    packet.priority = protocol::PacketPriority::Interactive;
    packet.responseStatus = protocol::ResponseStatus::Ok;
    packet.sequence = 2;
    packet.monotonicTimestampUsec = 200;
    packet.timeoutMs = request.timeoutMs;
    packet.payload = {5, 6};
    return packet;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "qt-display-user";
    options.context.tenantId = "qt-display-tenant";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

bool waitUntil(const std::function<bool()>& done,
               adapters::qt::QtTcpTransportSocket& client,
               adapters::qt::QtTcpTransportSocket& server,
               int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        client.poll();
        server.poll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

bool waitUntilConnectors(const std::function<bool()>& done,
                         runtime::qt::QtSessionTransportConnector& client,
                         runtime::qt::QtSessionTransportConnector& server,
                         int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        for (const auto& transport : client.transports())
            transport->poll();
        for (const auto& transport : server.transports())
            transport->poll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

void roundTripsPacketBytesThroughQtTcpTransport()
{
    QTcpServer listener;
    assert(listener.listen(QHostAddress::LocalHost, 0));

    auto client = std::make_shared<adapters::qt::QtTcpTransportSocket>(network::SocketClass::Realtime);
    network::SocketOpenOptions options;
    options.socketClass = network::SocketClass::Realtime;
    options.endpoint = "127.0.0.1:" + std::to_string(listener.serverPort());
    assert(client->open(options));
    assert(client->state() == network::SocketState::Open);

    if (!listener.hasPendingConnections())
        assert(listener.waitForNewConnection(3000));

    auto server = adapters::qt::QtTcpTransportSocket::adopt(network::SocketClass::Realtime,
                                                           listener.nextPendingConnection());
    assert(server != nullptr);
    assert(server->state() == network::SocketState::Open);

    protocol::ByteBuffer serverBytes;
    protocol::ByteBuffer clientBytes;
    server->setBytesReceivedHandler([&serverBytes](protocol::ByteBuffer bytes) {
        serverBytes.insert(serverBytes.end(), bytes.begin(), bytes.end());
    });
    client->setBytesReceivedHandler([&clientBytes](protocol::ByteBuffer bytes) {
        clientBytes.insert(clientBytes.end(), bytes.begin(), bytes.end());
    });

    protocol::PacketCodec codec;
    const protocol::PacketEnvelope request = makeRequest();
    assert(client->write(codec.encode(request)).status == network::SendStatus::Sent);
    assert(waitUntil([&serverBytes]() { return !serverBytes.empty(); }, *client, *server));

    const protocol::PacketDecodeResult decodedRequest = codec.decode(serverBytes);
    assert(decodedRequest.ok());
    assert(decodedRequest.packet.messageId == request.messageId);
    assert(decodedRequest.packet.correlationId == request.correlationId);
    assert(decodedRequest.packet.payload == request.payload);

    const protocol::PacketEnvelope response = makeResponse(decodedRequest.packet);
    assert(server->write(codec.encode(response)).status == network::SendStatus::Sent);
    assert(waitUntil([&clientBytes]() { return !clientBytes.empty(); }, *client, *server));

    const protocol::PacketDecodeResult decodedResponse = codec.decode(clientBytes);
    assert(decodedResponse.ok());
    assert(decodedResponse.packet.messageKind == protocol::MessageKind::Response);
    assert(decodedResponse.packet.responseTo == request.messageId);
    assert(decodedResponse.packet.responseStatus == protocol::ResponseStatus::Ok);
}

void routesPacketsThroughNetworkManagersAndQtPacketChannels()
{
    QTcpServer listener;
    assert(listener.listen(QHostAddress::LocalHost, 0));

    auto clientTransport = std::make_shared<adapters::qt::QtTcpTransportSocket>(network::SocketClass::Realtime);
    network::SocketOpenOptions options;
    options.socketClass = network::SocketClass::Realtime;
    options.endpoint = "127.0.0.1:" + std::to_string(listener.serverPort());
    assert(clientTransport->open(options));

    if (!listener.hasPendingConnections())
        assert(listener.waitForNewConnection(3000));

    auto serverTransport = adapters::qt::QtTcpTransportSocket::adopt(network::SocketClass::Realtime,
                                                                     listener.nextPendingConnection());
    assert(serverTransport != nullptr);

    network::NetworkManager clientManager(makeNegotiated());
    network::NetworkManager serverManager(makeNegotiated());
    const network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(clientManager.registerSpec(spec).ok);
    assert(serverManager.registerSpec(spec).ok);

    adapters::qt::QtChannelBinder clientBinder(clientManager);
    adapters::qt::QtChannelBinder serverBinder(serverManager);
    adapters::qt::QtChannelBindOptions clientBind;
    clientBind.spec = spec;
    clientBind.transport = clientTransport;
    adapters::qt::QtChannelBindOptions serverBind;
    serverBind.spec = spec;
    serverBind.transport = serverTransport;
    assert(clientBinder.bindChannel(clientBind).ok);
    assert(serverBinder.bindChannel(serverBind).ok);
    assert(clientManager.registry().isReady(screenKey()));
    assert(serverManager.registry().isReady(screenKey()));
    assert(clientManager.socketSnapshotForChannel(screenKey()).state == network::SocketState::Open);
    assert(serverManager.socketSnapshotForChannel(screenKey()).state == network::SocketState::Open);

    auto clientChannel = clientBinder.channels().front();
    auto serverChannel = serverBinder.channels().front();

    int receivedRequests = 0;
    protocol::PacketEnvelope receivedRequest;
    network::RouteMatch requestRoute;
    requestRoute.channelId = screenKey().channelId;
    requestRoute.channelType = screenKey().channelType;
    requestRoute.messageKind = protocol::MessageKind::Request;
    requestRoute.packetType = protocol::PacketType::PayloadAck;
    serverManager.router().subscribe(requestRoute, [&receivedRequests, &receivedRequest](const protocol::PacketEnvelope& packet) {
        ++receivedRequests;
        receivedRequest = packet;
    });

    const protocol::PacketEnvelope request = makeRequest();
    assert(clientManager.router().send(request).status == network::SendStatus::Sent);
    assert(waitUntil([&receivedRequests]() { return receivedRequests == 1; },
                     *clientTransport,
                     *serverTransport));
    assert(serverChannel->decodedPackets() == 1);
    assert(receivedRequest.messageId == request.messageId);
    assert(receivedRequest.correlationId == request.correlationId);

    int receivedResponses = 0;
    network::RouteMatch responseRoute;
    responseRoute.channelId = screenKey().channelId;
    responseRoute.channelType = screenKey().channelType;
    responseRoute.messageKind = protocol::MessageKind::Response;
    responseRoute.packetType = protocol::PacketType::PayloadAck;
    clientManager.router().subscribe(responseRoute, [&receivedResponses](const protocol::PacketEnvelope& packet) {
        if (packet.responseStatus == protocol::ResponseStatus::Ok)
            ++receivedResponses;
    });

    assert(serverManager.router().send(makeResponse(receivedRequest)).status == network::SendStatus::Sent);
    assert(waitUntil([&receivedResponses]() { return receivedResponses == 1; },
                     *clientTransport,
                     *serverTransport));
    assert(clientChannel->decodedPackets() == 1);
    assert(clientChannel->decodeFailures() == 0);
    assert(serverChannel->decodeFailures() == 0);
}

void packetChannelDoesNotRedispatchCurrentFrameDuringReentrantRead()
{
    QTcpServer listener;
    assert(listener.listen(QHostAddress::LocalHost, 0));

    auto clientTransport = std::make_shared<adapters::qt::QtTcpTransportSocket>(
        network::SocketClass::Realtime);
    network::SocketOpenOptions options;
    options.socketClass = network::SocketClass::Realtime;
    options.endpoint = "127.0.0.1:" + std::to_string(listener.serverPort());
    assert(clientTransport->open(options));

    if (!listener.hasPendingConnections())
        assert(listener.waitForNewConnection(3000));

    auto serverTransport = adapters::qt::QtTcpTransportSocket::adopt(
        network::SocketClass::Realtime,
        listener.nextPendingConnection());
    assert(serverTransport != nullptr);

    network::NetworkManager clientManager(makeNegotiated());
    network::NetworkManager serverManager(makeNegotiated());
    const network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();

    adapters::qt::QtChannelBinder clientBinder(clientManager);
    adapters::qt::QtChannelBinder serverBinder(serverManager);
    adapters::qt::QtChannelBindOptions clientBind;
    clientBind.spec = spec;
    clientBind.transport = clientTransport;
    adapters::qt::QtChannelBindOptions serverBind;
    serverBind.spec = spec;
    serverBind.transport = serverTransport;
    assert(clientBinder.bindChannel(clientBind).ok);
    assert(serverBinder.bindChannel(serverBind).ok);

    int totalRequests = 0;
    int firstRequestDeliveries = 0;
    bool nestedSent = false;
    const protocol::PacketEnvelope first = makeRequest();

    network::RouteMatch route;
    route.channelId = screenKey().channelId;
    route.channelType = screenKey().channelType;
    route.messageKind = protocol::MessageKind::Request;
    route.packetType = protocol::PacketType::PayloadAck;
    serverManager.router().subscribe(
        route,
        [&](const protocol::PacketEnvelope& packet) {
            ++totalRequests;
            if (packet.messageId == first.messageId)
                ++firstRequestDeliveries;

            if (nestedSent)
                return;

            nestedSent = true;
            protocol::PacketEnvelope nested = first;
            nested.messageId = first.messageId + 1;
            nested.correlationId = nested.messageId;
            nested.sequence = first.sequence + 1;
            assert(clientManager.router().send(nested).status ==
                   network::SendStatus::Sent);
            serverTransport->poll();
        });

    assert(clientManager.router().send(first).status ==
           network::SendStatus::Sent);
    assert(waitUntil([&totalRequests]() { return totalRequests >= 2; },
                     *clientTransport,
                     *serverTransport));
    assert(totalRequests == 2);
    assert(firstRequestDeliveries == 1);
    assert(serverBinder.channels().front()->decodedPackets() == 2);
}

void connectsProfilesThroughRuntimeQtConnector()
{
    QTcpServer listener;
    assert(listener.listen(QHostAddress::LocalHost, 0));

    network::NetworkManager clientManager(makeNegotiated());
    network::NetworkManager serverManager(makeNegotiated());
    runtime::qt::QtSessionTransportConnector clientConnector(clientManager);
    runtime::qt::QtSessionTransportConnector serverConnector(serverManager);

    runtime::qt::QtTcpChannelProfile profile;
    profile.spec = network::defaultMvpChannelSpecs().back();
    profile.endpoint = "127.0.0.1:" + std::to_string(listener.serverPort());
    profile.ready.endpoint = "client-loopback";

    runtime::qt::QtTransportConnectResult clientConnected =
        clientConnector.connectTcpChannels({profile});
    assert(clientConnected.ok);
    assert(clientConnected.readyChannels.size() == 1);
    assert(clientConnector.transportCount() == 1);
    assert(clientManager.registry().isReady(screenKey()));
    assert(clientManager.socketSnapshotForChannel(screenKey()).state == network::SocketState::Open);

    if (!listener.hasPendingConnections())
        assert(listener.waitForNewConnection(3000));

    network::ChannelReadyInfo serverReady;
    serverReady.endpoint = "server-loopback";
    runtime::qt::QtTransportConnectResult serverConnected =
        serverConnector.adoptTcpChannel(profile.spec,
                                        listener.nextPendingConnection(),
                                        serverReady);
    assert(serverConnected.ok);
    assert(serverConnected.readyChannels.size() == 1);
    assert(serverConnector.transportCount() == 1);
    assert(serverManager.registry().isReady(screenKey()));
    assert(serverManager.socketSnapshotForChannel(screenKey()).state == network::SocketState::Open);

    int receivedRequests = 0;
    protocol::PacketEnvelope receivedRequest;
    network::RouteMatch requestRoute;
    requestRoute.channelId = screenKey().channelId;
    requestRoute.channelType = screenKey().channelType;
    requestRoute.messageKind = protocol::MessageKind::Request;
    requestRoute.packetType = protocol::PacketType::PayloadAck;
    serverManager.router().subscribe(requestRoute, [&receivedRequests, &receivedRequest](const protocol::PacketEnvelope& packet) {
        ++receivedRequests;
        receivedRequest = packet;
    });

    const protocol::PacketEnvelope request = makeRequest();
    assert(clientManager.router().send(request).status == network::SendStatus::Sent);
    assert(waitUntil([&receivedRequests]() { return receivedRequests == 1; },
                     *clientConnector.transports().front(),
                     *serverConnector.transports().front()));

    int receivedResponses = 0;
    network::RouteMatch responseRoute;
    responseRoute.channelId = screenKey().channelId;
    responseRoute.channelType = screenKey().channelType;
    responseRoute.messageKind = protocol::MessageKind::Response;
    responseRoute.packetType = protocol::PacketType::PayloadAck;
    clientManager.router().subscribe(responseRoute, [&receivedResponses](const protocol::PacketEnvelope&) {
        ++receivedResponses;
    });

    assert(serverManager.router().send(makeResponse(receivedRequest)).status == network::SendStatus::Sent);
    assert(waitUntil([&receivedResponses]() { return receivedResponses == 1; },
                     *clientConnector.transports().front(),
                     *serverConnector.transports().front()));
}

void runtimeTransportManagerAppliesProfileToCurrentSession()
{
    QTcpServer listener;
    assert(listener.listen(QHostAddress::LocalHost, 0));

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    assert(clientSession != nullptr);
    assert(clientSession->start());
    assert(clientSession->network() != nullptr);

    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();
    QTemporaryFile profileFile;
    assert(profileFile.open());
    const std::string profileJson =
        "{\"sessions\":[{\"tcpChannels\":[{\"channelId\":" +
        std::to_string(screenSpec.key.channelId) +
        ",\"channelType\":\"video\",\"endpoint\":\"127.0.0.1:" +
        std::to_string(listener.serverPort()) +
        "\",\"readyEndpoint\":\"client-current-session\"}]}]}";
    assert(profileFile.write(profileJson.data(),
                             static_cast<qint64>(profileJson.size())) ==
           static_cast<qint64>(profileJson.size()));
    assert(profileFile.flush());

    const runtime::qt::QtTransportProfileLoadResult directLoad =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(profileFile.fileName().toStdString(),
                                                          network::defaultMvpChannelSpecs());
    assert(!directLoad.ok);

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const runtime::qt::QtTransportConnectResult applied =
        transportManager.applyProfilesFromJsonFileForSession(clientId,
                                                             profileFile.fileName().toStdString(),
                                                             network::defaultMvpChannelSpecs());
    assert(applied.ok);
    assert(applied.readyChannels.size() == 1);
    assert(applied.listeningChannels.empty());
    assert(clientSession->network()->registry().isReady(screenKey()));
    assert(transportManager.connector(clientId) != nullptr);

    if (!listener.hasPendingConnections())
        assert(listener.waitForNewConnection(3000));
    std::unique_ptr<QTcpSocket> accepted(listener.nextPendingConnection());
    assert(accepted != nullptr);
}

void serializesTcpPeerProfilePairRoundtrip()
{
    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();
    const runtime::qt::QtTcpPeerProfilePair pair =
        runtime::qt::makeTcpPeerProfilePair({screenSpec},
                                            "127.0.0.1:48123",
                                            "client-ready",
                                            "agent-ready",
                                            101,
                                            202);
    QTemporaryFile profileFile;
    assert(profileFile.open());
    const runtime::qt::QtTransportProfileSaveResult saved =
        runtime::qt::saveTcpTransportProfilesToJsonFile(profileFile.fileName().toStdString(),
                                                        {pair.clientProfile, pair.agentProfile});
    assert(saved.ok);

    const runtime::qt::QtTransportProfileLoadResult loadedProfile =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(profileFile.fileName().toStdString(),
                                                          network::defaultMvpChannelSpecs());
    assert(loadedProfile.ok);
    assert(loadedProfile.profiles.size() == 2);
    assert(loadedProfile.profiles.front().sessionId == 101);
    assert(loadedProfile.profiles.front().tcpChannels.size() == 1);
    assert(loadedProfile.profiles.front().tcpChannels.front().ready.endpoint == "client-ready");
    assert(loadedProfile.profiles.back().sessionId == 202);
    assert(loadedProfile.profiles.back().tcpListenChannels.size() == 1);
    assert(loadedProfile.profiles.back().tcpListenChannels.front().ready.endpoint == "agent-ready");
}

quint16 nextAvailablePort()
{
    QTcpServer probe;
    assert(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

void runtimeTransportManagerListensAndAdoptsAcceptedTcpChannel()
{
    const quint16 port = nextAvailablePort();

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* agentSession = host.sessions().find(agentId);
    assert(agentSession != nullptr);
    assert(agentSession->start());
    assert(agentSession->network() != nullptr);

    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();
    const std::string endpoint = "127.0.0.1:" + std::to_string(port);
    QTemporaryFile profileFile;
    assert(profileFile.open());
    const std::string profileJson =
        "{\"sessions\":[{\"tcpListenChannels\":[{\"channelId\":" + std::to_string(screenSpec.key.channelId) +
        ",\"channelType\":\"video\",\"endpoint\":\"" + endpoint +
        "\",\"readyEndpoint\":\"agent-listener\"}]}]}";
    assert(profileFile.write(profileJson.data(),
                             static_cast<qint64>(profileJson.size())) ==
           static_cast<qint64>(profileJson.size()));
    assert(profileFile.flush());

    const runtime::qt::QtTransportProfileLoadResult loadedProfile =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(profileFile.fileName().toStdString(),
                                                          network::defaultMvpChannelSpecs());
    assert(!loadedProfile.ok);

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const runtime::qt::QtTransportConnectResult listening =
        transportManager.applyListenProfilesFromJsonFileForSession(agentId,
                                                                   profileFile.fileName().toStdString(),
                                                                   network::defaultMvpChannelSpecs());
    assert(listening.ok);
    assert(listening.readyChannels.empty());
    assert(listening.listeningChannels.size() == 1);
    assert(transportManager.listenerCount() == 1);
    assert(transportManager.connector(agentId) != nullptr);

    network::NetworkManager clientManager(makeNegotiated());
    runtime::qt::QtSessionTransportConnector clientConnector(clientManager);
    runtime::qt::QtTcpChannelProfile clientProfile;
    clientProfile.spec = screenSpec;
    clientProfile.endpoint = endpoint;
    clientProfile.ready.endpoint = "client-to-agent-listener";
    assert(clientConnector.connectTcpChannels({clientProfile}).ok);
    assert(clientConnector.transportCount() == 1);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        for (const auto& transport : clientConnector.transports())
            transport->poll();
        runtime::qt::QtSessionTransportConnector* agentConnector =
            transportManager.connector(agentId);
        if (agentConnector != nullptr) {
            for (const auto& transport : agentConnector->transports())
                transport->poll();
            if (agentConnector->transportCount() == 1 &&
                agentSession->network()->registry().isReady(screenKey())) {
                break;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    runtime::qt::QtSessionTransportConnector* agentConnector =
        transportManager.connector(agentId);
    assert(agentConnector != nullptr);
    assert(agentConnector->transportCount() == 1);
    assert(agentSession->network()->registry().isReady(screenKey()));

    int receivedRequests = 0;
    network::RouteMatch requestRoute;
    requestRoute.channelId = screenKey().channelId;
    requestRoute.channelType = screenKey().channelType;
    requestRoute.messageKind = protocol::MessageKind::Request;
    requestRoute.packetType = protocol::PacketType::PayloadAck;
    agentSession->network()->router().subscribe(requestRoute, [&receivedRequests](const protocol::PacketEnvelope&) {
        ++receivedRequests;
    });

    assert(clientManager.router().send(makeRequest()).status == network::SendStatus::Sent);
    assert(waitUntilConnectors([&receivedRequests]() { return receivedRequests == 1; },
                               clientConnector,
                               *agentConnector));

    transportManager.releaseSession(agentId);
    assert(transportManager.connector(agentId) == nullptr);
    assert(transportManager.listenerCount() == 0);
}

void runtimeTransportManagerAppliesMultiEndpointCoordinatorProfiles()
{
    const quint16 controlPort = nextAvailablePort();
    quint16 screenPort = nextAvailablePort();
    while (screenPort == controlPort)
        screenPort = nextAvailablePort();

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());
    assert(clientSession->network() != nullptr);
    assert(agentSession->network() != nullptr);

    QTemporaryFile clientProfileFile;
    QTemporaryFile agentProfileFile;
    assert(clientProfileFile.open());
    assert(agentProfileFile.open());

    runtime::qt::QtTcpPeerProfilePlanOptions options;
    options.knownSpecs = network::defaultMvpChannelSpecs();
    options.channels = {
        runtime::qt::QtTcpPeerProfilePlanChannel{
            controlKey(),
            "127.0.0.1:" + std::to_string(controlPort),
            "client-control",
            "agent-control"},
        runtime::qt::QtTcpPeerProfilePlanChannel{
            screenKey(),
            "127.0.0.1:" + std::to_string(screenPort),
            "client-screen",
            "agent-screen"},
    };
    options.clientProfilePath = clientProfileFile.fileName().toStdString();
    options.agentProfilePath = agentProfileFile.fileName().toStdString();

    const runtime::qt::QtTcpPeerProfileCoordinator coordinator;
    const runtime::qt::QtTcpPeerProfilePlanResult planned =
        coordinator.saveLocalTcpPeerProfiles(options);
    assert(planned.ok);
    assert(planned.pair.clientProfile.tcpChannels.size() == 2);
    assert(planned.pair.agentProfile.tcpListenChannels.size() == 2);

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const runtime::qt::QtTransportConnectResult listening =
        transportManager.applyListenProfilesFromJsonFileForSession(
            agentId,
            agentProfileFile.fileName().toStdString(),
            host.profile().minimumChannels);
    assert(listening.ok);
    assert(listening.readyChannels.empty());
    assert(listening.listeningChannels.size() == 2);
    assert(transportManager.listenerCount() == 2);

    const runtime::qt::QtTransportConnectResult connected =
        transportManager.applyProfilesFromJsonFileForSession(
            clientId,
            clientProfileFile.fileName().toStdString(),
            host.profile().minimumChannels);
    assert(connected.ok);
    assert(connected.readyChannels.size() == 2);
    assert(connected.listeningChannels.empty());
    assert(clientSession->network()->registry().isReady(controlKey()));
    assert(clientSession->network()->registry().isReady(screenKey()));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        runtime::qt::QtSessionTransportConnector* clientConnector =
            transportManager.connector(clientId);
        if (clientConnector != nullptr) {
            for (const auto& transport : clientConnector->transports())
                transport->poll();
        }

        runtime::qt::QtSessionTransportConnector* agentConnector =
            transportManager.connector(agentId);
        if (agentConnector != nullptr) {
            for (const auto& transport : agentConnector->transports())
                transport->poll();
            if (agentConnector->transportCount() == 2 &&
                agentSession->network()->registry().isReady(controlKey()) &&
                agentSession->network()->registry().isReady(screenKey())) {
                break;
            }
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    runtime::qt::QtSessionTransportConnector* clientConnector =
        transportManager.connector(clientId);
    runtime::qt::QtSessionTransportConnector* agentConnector =
        transportManager.connector(agentId);
    assert(clientConnector != nullptr);
    assert(agentConnector != nullptr);
    assert(clientConnector->transportCount() == 2);
    assert(agentConnector->transportCount() == 2);
    assert(agentSession->network()->registry().isReady(controlKey()));
    assert(agentSession->network()->registry().isReady(screenKey()));

    transportManager.releaseSession(clientId);
    transportManager.releaseSession(agentId);
    assert(transportManager.connectorCount() == 0);
    assert(transportManager.listenerCount() == 0);
}

void runtimeTransportManagerPreparesReconnectReplacementsFromQtTcpProfiles()
{
    QTcpServer listener;
    assert(listener.listen(QHostAddress::LocalHost, 0));

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());
    assert(clientSession->network() != nullptr);
    assert(agentSession->network() != nullptr);
    assert(!clientSession->network()->registry().isReady(screenKey()));
    assert(!agentSession->network()->registry().isReady(screenKey()));

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();

    runtime::qt::QtTcpChannelProfile clientProfile;
    clientProfile.spec = screenSpec;
    clientProfile.endpoint = "127.0.0.1:" + std::to_string(listener.serverPort());
    clientProfile.ready.endpoint = "client-reconnect";

    const runtime::qt::QtReconnectReplacementResult clientPrepared =
        transportManager.prepareTcpChannelReplacements(clientId, {clientProfile});
    assert(clientPrepared.ok);
    assert(clientPrepared.preparedChannels.size() == 1);
    assert(clientPrepared.replacementChannels.size() == 1);
    assert(!clientSession->network()->registry().isReady(screenKey()));

    if (!listener.hasPendingConnections())
        assert(listener.waitForNewConnection(3000));

    network::ChannelReadyInfo agentReady;
    agentReady.endpoint = "agent-reconnect";
    const runtime::qt::QtReconnectReplacementResult agentPrepared =
        transportManager.prepareAdoptedTcpChannelReplacement(agentId,
                                                             screenSpec,
                                                             listener.nextPendingConnection(),
                                                             agentReady);
    assert(agentPrepared.ok);
    assert(agentPrepared.preparedChannels.size() == 1);
    assert(agentPrepared.replacementChannels.size() == 1);
    assert(!agentSession->network()->registry().isReady(screenKey()));

    session::ReconnectRequest clientReconnect;
    clientReconnect.reason = "qt client tcp reconnect";
    clientReconnect.degradedChannels = {screenKey()};
    clientReconnect.replacementChannels = clientPrepared.replacementChannels;
    assert(host.sessions().reconnect(clientId, clientReconnect));

    session::ReconnectRequest agentReconnect;
    agentReconnect.reason = "qt agent tcp reconnect";
    agentReconnect.degradedChannels = {screenKey()};
    agentReconnect.replacementChannels = agentPrepared.replacementChannels;
    assert(host.sessions().reconnect(agentId, agentReconnect));

    assert(clientSession->network()->registry().isReady(screenKey()));
    assert(agentSession->network()->registry().isReady(screenKey()));
    assert(clientSession->lastReconnectReport().reboundChannels.size() == 1);
    assert(agentSession->lastReconnectReport().reboundChannels.size() == 1);

    runtime::qt::QtSessionTransportConnector* clientConnector =
        transportManager.connector(clientId);
    runtime::qt::QtSessionTransportConnector* agentConnector =
        transportManager.connector(agentId);
    assert(clientConnector != nullptr);
    assert(agentConnector != nullptr);
    assert(clientConnector->transportCount() == 1);
    assert(agentConnector->transportCount() == 1);

    int receivedRequests = 0;
    network::RouteMatch requestRoute;
    requestRoute.channelId = screenKey().channelId;
    requestRoute.channelType = screenKey().channelType;
    requestRoute.messageKind = protocol::MessageKind::Request;
    requestRoute.packetType = protocol::PacketType::PayloadAck;
    agentSession->network()->router().subscribe(requestRoute, [&receivedRequests](const protocol::PacketEnvelope&) {
        ++receivedRequests;
    });

    assert(clientSession->network()->router().send(makeRequest()).status == network::SendStatus::Sent);
    assert(waitUntilConnectors([&receivedRequests]() { return receivedRequests == 1; },
                               *clientConnector,
                               *agentConnector));

    transportManager.releaseSession(clientId);
    transportManager.releaseSession(agentId);
    assert(transportManager.connectorCount() == 0);
}

void runtimeTransportManagerReconnectsQtTcpProfilesThroughSession()
{
    QTcpServer initialListener;
    QTcpServer reconnectListener;
    assert(initialListener.listen(QHostAddress::LocalHost, 0));
    assert(reconnectListener.listen(QHostAddress::LocalHost, 0));

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();

    runtime::qt::QtTcpChannelProfile initialClientProfile;
    initialClientProfile.spec = screenSpec;
    initialClientProfile.endpoint = "127.0.0.1:" + std::to_string(initialListener.serverPort());
    initialClientProfile.ready.endpoint = "client-initial";
    assert(transportManager.connectTcpChannels(clientId, {initialClientProfile}).ok);

    if (!initialListener.hasPendingConnections())
        assert(initialListener.waitForNewConnection(3000));

    network::ChannelReadyInfo initialAgentReady;
    initialAgentReady.endpoint = "agent-initial";
    assert(transportManager.adoptTcpChannel(agentId,
                                            screenSpec,
                                            initialListener.nextPendingConnection(),
                                            initialAgentReady).ok);

    runtime::qt::QtSessionTransportConnector* clientConnector =
        transportManager.connector(clientId);
    runtime::qt::QtSessionTransportConnector* agentConnector =
        transportManager.connector(agentId);
    assert(clientConnector != nullptr);
    assert(agentConnector != nullptr);
    assert(clientConnector->transportCount() == 1);
    assert(agentConnector->transportCount() == 1);
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> oldClientTransport =
        clientConnector->transports().front();
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> oldAgentTransport =
        agentConnector->transports().front();
    assert(oldClientTransport->state() == network::SocketState::Open);
    assert(oldAgentTransport->state() == network::SocketState::Open);

    runtime::qt::QtTcpChannelProfile clientProfile;
    clientProfile.spec = screenSpec;
    clientProfile.endpoint = "127.0.0.1:" + std::to_string(reconnectListener.serverPort());
    clientProfile.ready.endpoint = "client-reconnect-one-call";

    const runtime::qt::QtReconnectResult clientReconnect =
        transportManager.reconnectTcpChannels(clientId,
                                              {clientProfile},
                                              "qt client reconnect one call");
    assert(clientReconnect.ok);
    assert(clientReconnect.prepared.preparedChannels.size() == 1);
    assert(clientReconnect.report.attempted);
    assert(clientReconnect.report.ok);
    assert(clientReconnect.report.reboundChannels.size() == 1);
    assert(clientSession->network()->registry().isReady(screenKey()));
    assert(oldClientTransport->state() == network::SocketState::Closed);
    assert(clientConnector->transportCount() == 1);
    assert(clientConnector->transports().front() != oldClientTransport);
    assert(clientConnector->transports().front()->state() == network::SocketState::Open);

    {
        auto control = std::make_shared<CapturingChannel>(controlKey());
        assert(clientSession->network()->router().registerChannel(control));

        runtime::connection::ReconnectTeardownHandler handler(
            clientSession->network()->router(),
            transportManager);
        runtime::connection::ReconnectTeardownHandlerOptions handlerOptions;
        handlerOptions.firstResponseMessageId = 8100;
        assert(handler.start(handlerOptions).ok);

        runtime::connection::ReconnectTeardownCommand command;
        command.sessionId = clientId;
        command.targetChannel = screenKey();
        command.messageId = 8001;
        command.correlationId = 8001;
        command.timeoutMs = 1000;
        command.reason = "qt handler close target smoke";
        clientSession->network()->router().submitIncoming(
            runtime::connection::makeReconnectTeardownRequestPacket(command));

        assert(control->sentPackets.size() == 1);
        const protocol::PacketEnvelope& teardownResponse = control->sentPackets.front();
        assert(teardownResponse.messageKind == protocol::MessageKind::Response);
        assert(teardownResponse.responseStatus == protocol::ResponseStatus::Ok);
        assert(teardownResponse.responseTo == 8001);
        assert(teardownResponse.correlationId == 8001);
        assert(oldClientTransport->state() == network::SocketState::Closed);
        assert(clientConnector->transportCount() == 1);
        assert(clientConnector->transports().front()->state() == network::SocketState::Open);

        const runtime::connection::ReconnectTeardownCloseResult duplicateClose =
            transportManager.closeOldTransport({clientId, screenKey(), "duplicate qt teardown"});
        assert(duplicateClose.ok);

        const runtime::connection::ReconnectTeardownCloseResult missingSession =
            transportManager.closeOldTransport({999999, screenKey(), "missing qt teardown session"});
        assert(!missingSession.ok);
        assert(missingSession.status == protocol::ResponseStatus::NotFound);
    }

    if (!reconnectListener.hasPendingConnections())
        assert(reconnectListener.waitForNewConnection(3000));

    network::ChannelReadyInfo agentReady;
    agentReady.endpoint = "agent-reconnect-one-call";
    const runtime::qt::QtReconnectResult agentReconnect =
        transportManager.reconnectAdoptedTcpChannel(agentId,
                                                    screenSpec,
                                                    reconnectListener.nextPendingConnection(),
                                                    agentReady,
                                                    "qt agent reconnect one call");
    assert(agentReconnect.ok);
    assert(agentReconnect.prepared.preparedChannels.size() == 1);
    assert(agentReconnect.report.attempted);
    assert(agentReconnect.report.ok);
    assert(agentReconnect.report.reboundChannels.size() == 1);
    assert(agentSession->network()->registry().isReady(screenKey()));
    assert(oldAgentTransport->state() == network::SocketState::Closed);
    assert(agentConnector->transportCount() == 1);
    assert(agentConnector->transports().front() != oldAgentTransport);

    int receivedRequests = 0;
    network::RouteMatch requestRoute;
    requestRoute.channelId = screenKey().channelId;
    requestRoute.channelType = screenKey().channelType;
    requestRoute.messageKind = protocol::MessageKind::Request;
    requestRoute.packetType = protocol::PacketType::PayloadAck;
    agentSession->network()->router().subscribe(requestRoute, [&receivedRequests](const protocol::PacketEnvelope&) {
        ++receivedRequests;
    });

    assert(clientSession->network()->router().send(makeRequest()).status == network::SendStatus::Sent);
    assert(waitUntilConnectors([&receivedRequests]() { return receivedRequests == 1; },
                               *clientConnector,
                               *agentConnector));
}

void runtimeTransportManagerReconnectsProfilesFromJsonAndAdoptsListenerReconnect()
{
    QTcpServer portProbe;
    assert(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = portProbe.serverPort();
    portProbe.close();

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();

    QTemporaryFile clientProfileFile;
    QTemporaryFile agentProfileFile;
    assert(clientProfileFile.open());
    assert(agentProfileFile.open());

    const runtime::qt::QtTcpPeerProfilePair pair =
        runtime::qt::makeTcpPeerProfilePair({screenSpec},
                                            "127.0.0.1:" + std::to_string(port),
                                            "client-reconnect-json",
                                            "agent-reconnect-json",
                                            clientId,
                                            agentId);
    assert(runtime::qt::saveTcpTransportProfilesToJsonFile(clientProfileFile.fileName().toStdString(),
                                                           {pair.clientProfile}).ok);
    assert(runtime::qt::saveTcpTransportProfilesToJsonFile(agentProfileFile.fileName().toStdString(),
                                                           {pair.agentProfile}).ok);

    assert(transportManager.applyListenProfilesFromJsonFileForSession(
               agentId,
               agentProfileFile.fileName().toStdString(),
               host.profile().minimumChannels).ok);
    assert(transportManager.applyProfilesFromJsonFileForSession(
               clientId,
               clientProfileFile.fileName().toStdString(),
               host.profile().minimumChannels).ok);

    runtime::qt::QtSessionTransportConnector* clientConnector = transportManager.connector(clientId);
    runtime::qt::QtSessionTransportConnector* agentConnector = transportManager.connector(agentId);
    assert(clientConnector != nullptr);
    assert(agentConnector != nullptr);
    assert(waitUntilConnectors([clientConnector, agentConnector]() {
        return clientConnector->transportCount() == 1 && agentConnector->transportCount() == 1;
    },
                               *clientConnector,
                               *agentConnector));
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> oldClientTransport =
        clientConnector->transports().front();
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> oldAgentTransport =
        agentConnector->transports().front();

    const runtime::qt::QtReconnectResult clientReconnect =
        transportManager.reconnectProfilesFromJsonFileForSession(
            clientId,
            clientProfileFile.fileName().toStdString(),
            host.profile().minimumChannels,
            "qt reconnect profile smoke");
    assert(clientReconnect.ok);
    assert(clientReconnect.prepared.ok);
    assert(clientReconnect.report.attempted);
    assert(clientReconnect.report.ok);

    assert(waitUntilConnectors([&clientSession, &agentSession]() {
        return clientSession->lastReconnectReport().attempted &&
               clientSession->lastReconnectReport().ok &&
               agentSession->lastReconnectReport().attempted &&
               agentSession->lastReconnectReport().ok;
    },
                               *clientConnector,
                               *agentConnector));

    assert(clientSession->network()->registry().isReady(screenKey()));
    assert(agentSession->network()->registry().isReady(screenKey()));
    assert(clientSession->lastReconnectReport().reboundChannels.size() == 1);
    assert(agentSession->lastReconnectReport().reboundChannels.size() == 1);
    assert(oldClientTransport->state() == network::SocketState::Closed);
    assert(oldAgentTransport->state() == network::SocketState::Closed);
    assert(clientConnector->transportCount() == 1);
    assert(agentConnector->transportCount() == 1);
}

void runtimeTransportManagerAppliesReconnectOrchestrationPlan()
{
    QTcpServer initialListener;
    QTcpServer portProbe;
    assert(initialListener.listen(QHostAddress::LocalHost, 0));
    assert(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 reconnectPort = portProbe.serverPort();
    portProbe.close();

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    const network::ChannelSpec screenSpec = network::defaultMvpChannelSpecs().back();

    runtime::qt::QtTcpChannelProfile initialClientProfile;
    initialClientProfile.spec = screenSpec;
    initialClientProfile.endpoint = "127.0.0.1:" + std::to_string(initialListener.serverPort());
    initialClientProfile.ready.endpoint = "client-orchestrated-initial";
    assert(transportManager.connectTcpChannels(clientId, {initialClientProfile}).ok);

    if (!initialListener.hasPendingConnections())
        assert(initialListener.waitForNewConnection(3000));

    network::ChannelReadyInfo initialAgentReady;
    initialAgentReady.endpoint = "agent-orchestrated-initial";
    assert(transportManager.adoptTcpChannel(agentId,
                                            screenSpec,
                                            initialListener.nextPendingConnection(),
                                            initialAgentReady).ok);

    runtime::qt::QtSessionTransportConnector* clientConnector =
        transportManager.connector(clientId);
    runtime::qt::QtSessionTransportConnector* agentConnector =
        transportManager.connector(agentId);
    assert(clientConnector != nullptr);
    assert(agentConnector != nullptr);
    assert(clientConnector->transportCount() == 1);
    assert(agentConnector->transportCount() == 1);
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> oldClientTransport =
        clientConnector->transports().front();
    std::shared_ptr<adapters::qt::QtTcpTransportSocket> oldAgentTransport =
        agentConnector->transports().front();

    runtime::connection::ReconnectOrchestrationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:" + std::to_string(reconnectPort),
            "client-orchestrated-reconnect",
            "agent-orchestrated-reconnect"},
    };
    request.profile.clientSessionId = clientId;
    request.profile.agentSessionId = agentId;
    request.degradedChannels = {screenKey()};
    request.reason = "qt reconnect orchestration smoke";
    request.requestDisplayKeyframe = false;

    const runtime::connection::ReconnectOrchestrationResult plan =
        runtime::connection::resolveReconnectOrchestrationPlan(request);
    assert(plan.ok);
    assert(plan.plan.client.tcpChannels.size() == 1);
    assert(plan.plan.agent.tcpListenChannels.size() == 1);

    assert(clientSession->network() != nullptr);
    runtime::qt::QtReconnectRuntimeService reconnectService(
        transportManager,
        clientSession->network()->router(),
        9000);
    const runtime::qt::QtReconnectRuntimeServiceStartResult started =
        reconnectService.start();
    assert(started.ok);
    assert(started.timerStarted);
    assert(reconnectService.snapshot().active);
    assert(reconnectService.snapshot().timerRunning);

    runtime::connection::ReconnectCoordinatorOptions options;
    options.dispatchClientTeardown = false;
    const runtime::connection::ReconnectCoordinatorRunResult applied =
        reconnectService.run(request, options);
    assert(applied.ok);
    assert(applied.steps.size() == 3);
    assert(applied.steps[0].phase ==
           runtime::connection::ReconnectCoordinatorPhase::ResolvePlan);
    assert(applied.steps[1].phase ==
           runtime::connection::ReconnectCoordinatorPhase::StartAgentReplacements);
    assert(applied.steps[1].channels.size() == 1);
    assert(applied.steps[2].phase ==
           runtime::connection::ReconnectCoordinatorPhase::ReconnectClientReplacements);
    assert(applied.steps[2].channels.size() == 1);
    assert(clientSession->lastReconnectReport().reason == request.reason);
    assert(!clientSession->lastReconnectReport().requestedFreshState);

    assert(waitUntilConnectors([&clientSession, &agentSession]() {
        return clientSession->lastReconnectReport().attempted &&
               clientSession->lastReconnectReport().ok &&
               agentSession->lastReconnectReport().attempted &&
               agentSession->lastReconnectReport().ok;
    },
                               *clientConnector,
                               *agentConnector));

    assert(agentSession->lastReconnectReport().reason == request.reason);
    assert(!agentSession->lastReconnectReport().requestedFreshState);
    assert(clientSession->network()->registry().isReady(screenKey()));
    assert(agentSession->network()->registry().isReady(screenKey()));
    assert(oldClientTransport->state() == network::SocketState::Closed);
    assert(oldAgentTransport->state() == network::SocketState::Closed);
    assert(clientConnector->transportCount() == 1);
    assert(agentConnector->transportCount() == 1);

    int receivedRequests = 0;
    network::RouteMatch requestRoute;
    requestRoute.channelId = screenKey().channelId;
    requestRoute.channelType = screenKey().channelType;
    requestRoute.messageKind = protocol::MessageKind::Request;
    requestRoute.packetType = protocol::PacketType::PayloadAck;
    agentSession->network()->router().subscribe(requestRoute, [&receivedRequests](const protocol::PacketEnvelope&) {
        ++receivedRequests;
    });

    assert(clientSession->network()->router().send(makeRequest()).status == network::SendStatus::Sent);
    assert(waitUntilConnectors([&receivedRequests]() { return receivedRequests == 1; },
                               *clientConnector,
                               *agentConnector));
}

void runtimeSessionsRenderDisplayOverQtTransport()
{
    QTcpServer smallDataListener;
    QTcpServer screenListener;
    assert(smallDataListener.listen(QHostAddress::LocalHost, 0));
    assert(screenListener.listen(QHostAddress::LocalHost, 0));

    runtime::RuntimeHost host;
    assert(host.initialize());

    const protocol::SessionId clientId = host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId = host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* clientSession = host.sessions().find(clientId);
    session::Session* agentSession = host.sessions().find(agentId);
    assert(clientSession != nullptr);
    assert(agentSession != nullptr);
    assert(clientSession->start());
    assert(agentSession->start());
    assert(clientSession->network() != nullptr);
    assert(agentSession->network() != nullptr);

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    runtime::qt::QtTransportConnectResult missingSession =
        transportManager.connectTcpChannels(999999, {});
    assert(!missingSession.ok);
    assert(transportManager.connectorCount() == 0);

    const network::ChannelSpec smallDataSpec = specForKey(smallDataKey());
    const network::ChannelSpec screenSpec = specForKey(screenKey());
    QTemporaryFile profileFile;
    assert(profileFile.open());
    const std::string profileJson =
        "{\"sessions\":[{\"sessionId\":" + std::to_string(clientId) +
        ",\"tcpChannels\":[{\"channelId\":" + std::to_string(smallDataSpec.key.channelId) +
        ",\"channelType\":\"standard\",\"endpoint\":\"127.0.0.1:" +
        std::to_string(smallDataListener.serverPort()) +
        "\",\"readyEndpoint\":\"client-display-control\"},{\"channelId\":" +
        std::to_string(screenSpec.key.channelId) +
        ",\"channelType\":\"video\",\"endpoint\":\"127.0.0.1:" +
        std::to_string(screenListener.serverPort()) +
        "\",\"readyEndpoint\":\"client-display\"}]}]}";
    assert(profileFile.write(profileJson.data(),
                             static_cast<qint64>(profileJson.size())) ==
           static_cast<qint64>(profileJson.size()));
    assert(profileFile.flush());

    const runtime::qt::QtTransportProfileLoadResult loadedProfile =
        runtime::qt::loadTcpTransportProfilesFromJsonFile(profileFile.fileName().toStdString(),
                                                          network::defaultMvpChannelSpecs());
    assert(loadedProfile.ok);
    assert(loadedProfile.profiles.size() == 1);
    assert(loadedProfile.profiles.front().sessionId == clientId);
    assert(loadedProfile.profiles.front().tcpChannels.size() == 2);
    const runtime::qt::QtTransportConnectResult appliedProfile =
        transportManager.applyProfilesFromJsonFile(profileFile.fileName().toStdString(),
                                                   network::defaultMvpChannelSpecs());
    assert(appliedProfile.ok);
    assert(appliedProfile.readyChannels.size() == 2);
    assert(clientSession->network()->registry().isReady(smallDataKey()));
    assert(clientSession->network()->registry().isReady(screenKey()));

    if (!smallDataListener.hasPendingConnections())
        assert(smallDataListener.waitForNewConnection(3000));
    if (!screenListener.hasPendingConnections())
        assert(screenListener.waitForNewConnection(3000));

    network::ChannelReadyInfo agentSmallReady;
    agentSmallReady.endpoint = "agent-display-control";
    assert(transportManager.adoptTcpChannel(agentId,
                                            smallDataSpec,
                                            smallDataListener.nextPendingConnection(),
                                            agentSmallReady).ok);
    network::ChannelReadyInfo agentReady;
    agentReady.endpoint = "agent-display";
    assert(transportManager.adoptTcpChannel(agentId,
                                            screenSpec,
                                            screenListener.nextPendingConnection(),
                                            agentReady).ok);
    assert(agentSession->network()->registry().isReady(smallDataKey()));
    assert(agentSession->network()->registry().isReady(screenKey()));

    runtime::qt::QtSessionTransportConnector* clientConnector =
        transportManager.connector(clientId);
    runtime::qt::QtSessionTransportConnector* agentConnector =
        transportManager.connector(agentId);
    assert(clientConnector != nullptr);
    assert(agentConnector != nullptr);
    assert(clientConnector->transportCount() == 2);
    assert(agentConnector->transportCount() == 2);
    assert(transportManager.connectorCount() == 2);

    auto capture = std::make_shared<FakeCapture>();
    auto encoder = std::make_shared<FakeEncoder>();
    auto decoder = std::make_shared<FakeDecoder>();
    auto renderer = std::make_shared<FakeRenderer>();

    runtime::DisplayMvpDependencies clientDependencies;
    clientDependencies.decoder = decoder;
    clientDependencies.renderer = renderer;
    runtime::DisplayMvpDependencies agentDependencies;
    agentDependencies.capture = capture;
    agentDependencies.encoder = encoder;

    assert(host.mountProfileModules(*clientSession, clientDependencies).ok());
    assert(host.mountProfileModules(*agentSession, agentDependencies).ok());

    const std::vector<module::ModuleStartReport> clientReports =
        clientSession->moduleHost()->startAllowedModules();
    assert(clientReports.size() == 1);
    assert(clientReports.front().started);
    assert(renderer->renderedFrames() == 0);

    const std::vector<module::ModuleStartReport> agentReports =
        agentSession->moduleHost()->startAllowedModules();
    assert(agentReports.size() == 1);
    assert(agentReports.front().started);

    auto* clientModule = dynamic_cast<modules::display::DisplayClientModule*>(
        clientSession->moduleHost()->module("display.screen.client"));
    auto* agentModule = dynamic_cast<modules::display::DisplayAgentModule*>(
        agentSession->moduleHost()->module("display.screen.agent"));
    assert(clientModule != nullptr);
    assert(agentModule != nullptr);

    assert(waitUntilConnectors([renderer]() { return renderer->renderedFrames() == 1; },
                               *clientConnector,
                               *agentConnector));
    assert(agentModule->sentFrames() == 1);
    assert(clientModule->renderedFrames() == 1);
    assert(renderer->lastFrameId() == 1);
    assert(renderer->lastKeyFrame());
    modules::display::DisplayAgentSnapshot agentSnapshot = agentModule->snapshot();
    assert(agentSnapshot.state == module::ModuleState::Running);
    assert(agentSnapshot.capturedFrames == 1);
    assert(agentSnapshot.encodedFrames == 1);
    assert(agentSnapshot.sentFrames == 1);
    assert(agentSnapshot.droppedFrames == 0);
    modules::display::DisplayClientSnapshot clientSnapshot = clientModule->snapshot();
    assert(clientSnapshot.state == module::ModuleState::Running);
    assert(clientSnapshot.receivedFrames == 1);
    assert(clientSnapshot.decodedFrames == 1);
    assert(clientSnapshot.renderedFrames == 1);
    assert(clientSnapshot.renderErrors == 0);

    assert(clientModule->requestKeyFrame());
    assert(waitUntilConnectors([clientModule]() {
        return clientModule->renderedFrames() == 2 && clientModule->keyframeResponses() == 1;
    },
                               *clientConnector,
                               *agentConnector));
    assert(agentModule->keyframeRequests() == 1);
    assert(agentModule->responsesSent() == 1);
    assert(agentModule->sentFrames() == 2);
    assert(renderer->lastFrameId() == 2);
    agentSnapshot = agentModule->snapshot();
    assert(agentSnapshot.capturedFrames == 2);
    assert(agentSnapshot.encodedFrames == 2);
    assert(agentSnapshot.sentFrames == 2);
    assert(agentSnapshot.keyframeRequests == 1);
    assert(agentSnapshot.responsesSent == 1);
    clientSnapshot = clientModule->snapshot();
    assert(clientSnapshot.receivedFrames == 2);
    assert(clientSnapshot.decodedFrames == 2);
    assert(clientSnapshot.renderedFrames == 2);
    assert(clientSnapshot.renderErrors == 0);
    assert(clientSnapshot.keyframeResponses == 1);
    assert(clientSnapshot.lastKeyframeRequestId == clientModule->lastKeyframeRequestId());

    transportManager.releaseSession(clientId);
    assert(transportManager.connector(clientId) == nullptr);
    assert(transportManager.connectorCount() == 1);
    transportManager.releaseSession(agentId);
    assert(transportManager.connectorCount() == 0);
}

void rejectsInvalidEndpoint()
{
    adapters::qt::QtTcpTransportSocket socket(network::SocketClass::Control);
    network::SocketOpenOptions options;
    options.endpoint = "missing-port";
    assert(!socket.open(options));
    assert(socket.state() == network::SocketState::Failed);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    roundTripsPacketBytesThroughQtTcpTransport();
    routesPacketsThroughNetworkManagersAndQtPacketChannels();
    packetChannelDoesNotRedispatchCurrentFrameDuringReentrantRead();
    connectsProfilesThroughRuntimeQtConnector();
    runtimeTransportManagerAppliesProfileToCurrentSession();
    serializesTcpPeerProfilePairRoundtrip();
    runtimeTransportManagerListensAndAdoptsAcceptedTcpChannel();
    runtimeTransportManagerAppliesMultiEndpointCoordinatorProfiles();
    runtimeTransportManagerPreparesReconnectReplacementsFromQtTcpProfiles();
    runtimeTransportManagerReconnectsQtTcpProfilesThroughSession();
    runtimeTransportManagerReconnectsProfilesFromJsonAndAdoptsListenerReconnect();
    runtimeTransportManagerAppliesReconnectOrchestrationPlan();
    runtimeSessionsRenderDisplayOverQtTransport();
    rejectsInvalidEndpoint();
    return 0;
}
