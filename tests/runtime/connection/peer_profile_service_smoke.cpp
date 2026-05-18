#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/protocol/packet_codec.h"
#include "fusiondesk/core/protocol/protocol_validator.h"
#include "fusiondesk/runtime/connection/peer_profile_service.h"

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

class FakeChannel : public network::IChannel
{
public:
    explicit FakeChannel(network::ChannelKey key)
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
        return open;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    network::ChannelKey key_;
    bool open = true;
    std::vector<protocol::PacketEnvelope> sentPackets;
};

runtime::connection::PeerProfileExchangeRequest profileRequest()
{
    runtime::connection::PeerProfileExchangeRequest request;
    request.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            controlKey(),
            "127.0.0.1:50101",
            "client-control-ready",
            "agent-control-ready"},
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "127.0.0.1:50102",
            "client-screen-ready",
            "agent-screen-ready"},
    };
    request.clientSessionId = 1101;
    request.agentSessionId = 2202;
    return request;
}

void buildsProfileExchangeRequestResponseEnvelope()
{
    runtime::connection::PeerProfileServiceWireOptions wire;
    wire.messageId = 3100;
    wire.traceId = 77;
    wire.sequence = 3;
    wire.monotonicTimestampUsec = 123456;
    wire.controlChannel = screenKey();

    const protocol::PacketEnvelope request =
        runtime::connection::makePeerProfileExchangeRequestPacket(profileRequest(), wire);

    assert(request.sessionId == 1101);
    assert(request.traceId == 77);
    assert(request.messageId == 3100);
    assert(request.correlationId == 3100);
    assert(request.channelId == controlKey().channelId);
    assert(request.channelType == controlKey().channelType);
    assert(request.packetType == protocol::PacketType::Control);
    assert(request.messageKind == protocol::MessageKind::Request);
    assert(request.priority == protocol::PacketPriority::Critical);
    assert(request.sequence == 3);
    assert(request.monotonicTimestampUsec == 123456);
    assert(request.timeoutMs == 1000);
    assert((request.flags & protocol::PacketFlagResponseRequired) != 0);

    protocol::ProtocolValidator validator;
    assert(validator.validate(request).valid);

    protocol::PacketCodec codec;
    const protocol::PacketDecodeResult decodedRequest =
        codec.decode(codec.encode(request));
    assert(decodedRequest.ok());

    const runtime::connection::PeerProfileServiceRequestDecodeResult decoded =
        runtime::connection::decodePeerProfileExchangeRequestPacket(decodedRequest.packet);
    assert(decoded.ok);
    assert(decoded.request.clientSessionId == 1101);
    assert(decoded.request.agentSessionId == 2202);
    assert(decoded.request.connectionPlan.channels.size() == 2);
    assert(decoded.request.connectionPlan.channels.back().key == screenKey());

    const runtime::connection::PeerProfileExchangeResult exchange =
        runtime::connection::resolvePeerProfileExchange(decoded.request);
    assert(exchange.ok);

    runtime::connection::PeerProfileServiceWireResponseOptions responseWire;
    responseWire.messageId = 4100;
    responseWire.sequence = 4;
    responseWire.monotonicTimestampUsec = 234567;
    const protocol::PacketEnvelope response =
        runtime::connection::makePeerProfileExchangeResponsePacket(request,
                                                                   exchange,
                                                                   responseWire);

    assert(response.sessionId == request.sessionId);
    assert(response.messageId == 4100);
    assert(response.correlationId == request.correlationId);
    assert(response.responseTo == request.messageId);
    assert(response.channelId == controlKey().channelId);
    assert(response.channelType == controlKey().channelType);
    assert(response.packetType == protocol::PacketType::Control);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);
    assert(response.sequence == 4);
    assert(response.monotonicTimestampUsec == 234567);
    assert(validator.validate(response).valid);

    const protocol::PacketDecodeResult decodedResponse =
        codec.decode(codec.encode(response));
    assert(decodedResponse.ok());

    const runtime::connection::PeerProfileServiceResponseDecodeResult decodedExchange =
        runtime::connection::decodePeerProfileExchangeResponsePacket(decodedResponse.packet);
    assert(decodedExchange.ok);
    assert(decodedExchange.exchange.pair.client.sessionId == 1101);
    assert(decodedExchange.exchange.pair.agent.sessionId == 2202);
    assert(decodedExchange.exchange.pair.client.tcpChannels.size() == 2);
    assert(decodedExchange.exchange.pair.agent.tcpListenChannels.size() == 2);
    assert(decodedExchange.exchange.pair.client.tcpChannels.front().endpoint ==
           "127.0.0.1:50101");
    assert(decodedExchange.exchange.pair.agent.tcpListenChannels.back().readyEndpoint ==
           "agent-screen-ready");
}

void roundTripsOpaquePeerProfileExtensions()
{
    runtime::connection::PeerProfileExchangeRequest request = profileRequest();
    runtime::connection::PeerProfileExtension requestExtension;
    requestExtension.key = "display.codec.v1";
    requestExtension.payload = {1, 2, 3, 4, 0};
    request.extensions.push_back(requestExtension);

    const protocol::PacketEnvelope requestPacket =
        runtime::connection::makePeerProfileExchangeRequestPacket(request);
    const runtime::connection::PeerProfileServiceRequestDecodeResult decodedRequest =
        runtime::connection::decodePeerProfileExchangeRequestPacket(requestPacket);
    assert(decodedRequest.ok);
    assert(decodedRequest.request.extensions.size() == 1);
    assert(decodedRequest.request.extensions.front().key == "display.codec.v1");
    assert(decodedRequest.request.extensions.front().payload == requestExtension.payload);

    runtime::connection::PeerProfileExchangeResult exchange =
        runtime::connection::resolvePeerProfileExchange(decodedRequest.request);
    assert(exchange.ok);
    runtime::connection::PeerProfileExtension responseExtension;
    responseExtension.key = "display.codec.v1.result";
    responseExtension.payload = {9, 8, 7, 6};
    exchange.extensions.push_back(responseExtension);

    const protocol::PacketEnvelope responsePacket =
        runtime::connection::makePeerProfileExchangeResponsePacket(requestPacket, exchange);
    const runtime::connection::PeerProfileServiceResponseDecodeResult decodedResponse =
        runtime::connection::decodePeerProfileExchangeResponsePacket(responsePacket);
    assert(decodedResponse.ok);
    assert(decodedResponse.exchange.extensions.size() == 1);
    assert(decodedResponse.exchange.extensions.front().key == "display.codec.v1.result");
    assert(decodedResponse.exchange.extensions.front().payload == responseExtension.payload);
}

void decodesVersionOnePeerProfilePayloadsWithoutExtensions()
{
    protocol::PacketEnvelope requestPacket =
        runtime::connection::makePeerProfileExchangeRequestPacket(profileRequest());
    assert(requestPacket.payload.size() > 10);
    requestPacket.payload[4] = 0;
    requestPacket.payload[5] = 1;
    requestPacket.payload.resize(requestPacket.payload.size() - 2);

    const runtime::connection::PeerProfileServiceRequestDecodeResult decodedRequest =
        runtime::connection::decodePeerProfileExchangeRequestPacket(requestPacket);
    assert(decodedRequest.ok);
    assert(decodedRequest.request.extensions.empty());

    runtime::connection::PeerProfileExchangeResult exchange =
        runtime::connection::resolvePeerProfileExchange(decodedRequest.request);
    assert(exchange.ok);
    protocol::PacketEnvelope responsePacket =
        runtime::connection::makePeerProfileExchangeResponsePacket(requestPacket, exchange);
    assert(responsePacket.payload.size() > 10);
    responsePacket.payload[4] = 0;
    responsePacket.payload[5] = 1;
    responsePacket.payload.resize(responsePacket.payload.size() - 2);

    const runtime::connection::PeerProfileServiceResponseDecodeResult decodedResponse =
        runtime::connection::decodePeerProfileExchangeResponsePacket(responsePacket);
    assert(decodedResponse.ok);
    assert(decodedResponse.exchange.extensions.empty());
}

void serviceHandlesProfileRequestOnControlRoute()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    runtime::connection::PeerProfileService service(router);
    runtime::connection::PeerProfileServiceStartOptions startOptions;
    startOptions.firstResponseMessageId = 5100;
    const runtime::connection::PeerProfileServiceStartResult started =
        service.start(startOptions);
    assert(started.ok);
    assert(started.requestToken != 0);
    assert(service.active());

    runtime::connection::PeerProfileServiceWireOptions wire;
    wire.messageId = 6100;
    const protocol::PacketEnvelope request =
        runtime::connection::makePeerProfileExchangeRequestPacket(profileRequest(), wire);
    router.submitIncoming(request);

    assert(control->sentPackets.size() == 1);
    const protocol::PacketEnvelope& response = control->sentPackets.front();
    assert(response.messageId == 5100);
    assert(response.responseTo == 6100);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);

    const runtime::connection::PeerProfileServiceResponseDecodeResult decoded =
        runtime::connection::decodePeerProfileExchangeResponsePacket(response);
    assert(decoded.ok);
    assert(decoded.exchange.pair.client.tcpChannels.size() == 2);
    assert(decoded.exchange.pair.agent.tcpListenChannels.size() == 2);

    const runtime::connection::PeerProfileServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.active);
    assert(snapshot.handledRequests == 1);
    assert(snapshot.ignoredPackets == 0);
    assert(snapshot.failedRequests == 0);
    assert(snapshot.sentResponses == 1);
}

void serviceIgnoresForeignControlRequestPayload()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    runtime::connection::PeerProfileService service(router);
    assert(service.start().ok);

    protocol::PacketEnvelope request;
    request.sessionId = 3303;
    request.messageId = 7100;
    request.correlationId = 7100;
    request.channelId = controlKey().channelId;
    request.channelType = controlKey().channelType;
    request.packetType = protocol::PacketType::Control;
    request.messageKind = protocol::MessageKind::Request;
    request.priority = protocol::PacketPriority::Critical;
    request.timeoutMs = 1000;
    request.flags = protocol::PacketFlagResponseRequired;
    request.payload = {
        static_cast<std::uint8_t>('F'),
        static_cast<std::uint8_t>('D'),
        static_cast<std::uint8_t>('R'),
        static_cast<std::uint8_t>('T')};

    router.submitIncoming(request);

    assert(control->sentPackets.empty());
    const runtime::connection::PeerProfileServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.handledRequests == 0);
    assert(snapshot.ignoredPackets == 1);
    assert(snapshot.failedRequests == 0);
    assert(snapshot.sentResponses == 0);
}

void serviceDropsUnsafeMalformedFdppRequest()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    runtime::connection::PeerProfileService service(router);
    assert(service.start().ok);

    protocol::PacketEnvelope request =
        runtime::connection::makePeerProfileExchangeRequestPacket(profileRequest());
    request.messageId = 0;
    request.correlationId = 0;

    router.submitIncoming(request);

    assert(control->sentPackets.empty());
    const runtime::connection::PeerProfileServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.handledRequests == 1);
    assert(snapshot.failedRequests == 1);
    assert(snapshot.sentResponses == 0);
    assert(!snapshot.messages.empty());
}

void serviceReturnsErrorForCorrelatableMalformedFdppRequest()
{
    network::NetworkRouter router;
    auto control = std::make_shared<FakeChannel>(controlKey());
    assert(router.registerChannel(control));

    runtime::connection::PeerProfileService service(router);
    assert(service.start().ok);

    protocol::PacketEnvelope request =
        runtime::connection::makePeerProfileExchangeRequestPacket(profileRequest());
    request.correlationId = 0;

    router.submitIncoming(request);

    assert(control->sentPackets.size() == 1);
    const protocol::PacketEnvelope& response = control->sentPackets.front();
    assert(response.messageKind == protocol::MessageKind::Error);
    assert(response.responseStatus == protocol::ResponseStatus::InvalidArgument);
    assert(response.responseTo == request.messageId);
    assert(response.correlationId == request.messageId);

    protocol::ProtocolValidator validator;
    assert(validator.validate(response).valid);
    const runtime::connection::PeerProfileServiceSnapshot snapshot =
        service.snapshot();
    assert(snapshot.handledRequests == 1);
    assert(snapshot.failedRequests == 1);
    assert(snapshot.sentResponses == 1);
}

} // namespace

int main()
{
    buildsProfileExchangeRequestResponseEnvelope();
    roundTripsOpaquePeerProfileExtensions();
    decodesVersionOnePeerProfilePayloadsWithoutExtensions();
    serviceHandlesProfileRequestOnControlRoute();
    serviceIgnoresForeignControlRequestPayload();
    serviceDropsUnsafeMalformedFdppRequest();
    serviceReturnsErrorForCorrelatableMalformedFdppRequest();
    return 0;
}
