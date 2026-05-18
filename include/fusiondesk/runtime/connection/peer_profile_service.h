#ifndef FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_SERVICE_H
#define FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_SERVICE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/runtime/connection/peer_profile_exchange.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct PeerProfileServiceWireOptions
{
    protocol::MessageId messageId = 1;
    protocol::MessageId correlationId = 0;
    std::uint32_t timeoutMs = 1000;
    protocol::TraceId traceId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::PacketPriority priority = protocol::PacketPriority::Critical;
};

struct PeerProfileServiceWireResponseOptions
{
    protocol::MessageId messageId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
    std::string message;
};

struct PeerProfileServiceRequestDecodeResult
{
    bool ok = false;
    PeerProfileExchangeRequest request;
    std::string message;
};

struct PeerProfileServiceResponseDecodeResult
{
    bool ok = false;
    PeerProfileExchangeResult exchange;
    std::string message;
};

struct PeerProfileServiceStartOptions
{
    using ExchangeHandler = std::function<PeerProfileExchangeResult(
        const PeerProfileExchangeRequest&,
        const PeerProfileExchangeResult&)>;

    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::MessageId firstResponseMessageId = 1;
    ExchangeHandler exchangeHandler;
};

struct PeerProfileServiceStartResult
{
    bool ok = false;
    network::SubscriptionToken requestToken = 0;
    std::vector<std::string> messages;
};

struct PeerProfileServiceSnapshot
{
    bool active = false;
    std::size_t handledRequests = 0;
    std::size_t ignoredPackets = 0;
    std::size_t failedRequests = 0;
    std::size_t sentResponses = 0;
    std::vector<std::string> messages;
};

protocol::ByteBuffer encodePeerProfileExchangeRequestPayload(
    const PeerProfileExchangeRequest& request);

PeerProfileServiceRequestDecodeResult decodePeerProfileExchangeRequestPacket(
    const protocol::PacketEnvelope& packet);

protocol::ByteBuffer encodePeerProfileExchangeResultPayload(
    const PeerProfileExchangeResult& exchange);

PeerProfileServiceResponseDecodeResult decodePeerProfileExchangeResponsePacket(
    const protocol::PacketEnvelope& packet);

protocol::PacketEnvelope makePeerProfileExchangeRequestPacket(
    const PeerProfileExchangeRequest& request,
    const PeerProfileServiceWireOptions& options = {});

protocol::PacketEnvelope makePeerProfileExchangeResponsePacket(
    const protocol::PacketEnvelope& request,
    const PeerProfileExchangeResult& exchange,
    const PeerProfileServiceWireResponseOptions& options = {});

class PeerProfileService
{
public:
    explicit PeerProfileService(network::INetworkRouter& router);
    ~PeerProfileService();

    PeerProfileService(const PeerProfileService&) = delete;
    PeerProfileService& operator=(const PeerProfileService&) = delete;

    PeerProfileServiceStartResult start(
        const PeerProfileServiceStartOptions& options = {});
    void stop();
    bool active() const;

    bool handleRequest(const protocol::PacketEnvelope& packet);
    PeerProfileServiceSnapshot snapshot() const;

private:
    static bool looksLikePeerProfilePayload(const protocol::ByteBuffer& payload);
    static network::RouteMatch requestRoute(network::ChannelKey controlChannel);
    protocol::MessageId nextResponseMessageId();
    void remember(const std::vector<std::string>& messages);
    void remember(std::string message);

private:
    network::INetworkRouter& router_;
    network::SubscriptionToken requestToken_ = 0;
    network::ChannelKey controlChannel_{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::MessageId nextResponseMessageId_ = 1;
    PeerProfileServiceStartOptions::ExchangeHandler exchangeHandler_;
    bool active_ = false;
    std::size_t handledRequests_ = 0;
    std::size_t ignoredPackets_ = 0;
    std::size_t failedRequests_ = 0;
    std::size_t sentResponses_ = 0;
    std::vector<std::string> messages_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_PEER_PROFILE_SERVICE_H
