#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_HANDLER_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_HANDLER_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_ack.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ReconnectTeardownCloseRequest
{
    protocol::SessionId sessionId = 0;
    network::ChannelKey targetChannel;
    std::string reason;
};

struct ReconnectTeardownCloseResult
{
    bool ok = false;
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    std::string message;

    static ReconnectTeardownCloseResult closed(std::string message = {})
    {
        return {true, protocol::ResponseStatus::Ok, std::move(message)};
    }

    static ReconnectTeardownCloseResult failed(protocol::ResponseStatus status,
                                               std::string message)
    {
        return {false,
                status == protocol::ResponseStatus::Ok ? protocol::ResponseStatus::Failed : status,
                std::move(message)};
    }
};

class IReconnectTeardownCloseTarget
{
public:
    virtual ~IReconnectTeardownCloseTarget() = default;

    virtual ReconnectTeardownCloseResult closeOldTransport(
        const ReconnectTeardownCloseRequest& request) = 0;
};

struct ReconnectTeardownHandlerOptions
{
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::MessageId firstResponseMessageId = 1;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
};

struct ReconnectTeardownHandlerStartResult
{
    bool ok = false;
    network::SubscriptionToken token = 0;
    std::vector<std::string> messages;
};

struct ReconnectTeardownHandlerSnapshot
{
    bool active = false;
    network::SubscriptionToken token = 0;
    std::size_t handledRequests = 0;
    std::size_t ignoredPackets = 0;
    std::size_t malformedRequests = 0;
    std::size_t failedResponseSends = 0;
    std::vector<std::string> messages;
};

class ReconnectTeardownHandler
{
public:
    ReconnectTeardownHandler(network::INetworkRouter& router,
                             IReconnectTeardownCloseTarget& closeTarget);
    ~ReconnectTeardownHandler();

    ReconnectTeardownHandlerStartResult start(
        const ReconnectTeardownHandlerOptions& options = {});
    void stop();

    bool handleIncoming(const protocol::PacketEnvelope& packet);
    ReconnectTeardownHandlerSnapshot snapshot() const;

private:
    static bool looksLikeReconnectTeardownPayload(const protocol::ByteBuffer& payload);
    static protocol::MessageId normalizeFirstResponseMessageId(protocol::MessageId messageId);

    protocol::MessageId nextResponseMessageId();
    bool sendMalformedRequestError(const protocol::PacketEnvelope& request,
                                   const std::string& message);
    bool sendCloseResult(const protocol::PacketEnvelope& request,
                         const ReconnectTeardownCloseResult& closeResult);
    bool sendResponse(const protocol::PacketEnvelope& request,
                      protocol::ResponseStatus status,
                      const std::string& message);

private:
    network::INetworkRouter& router_;
    IReconnectTeardownCloseTarget& closeTarget_;
    network::SubscriptionToken token_ = 0;
    protocol::MessageId nextResponseMessageId_ = 1;
    std::uint64_t nextSequence_ = 0;
    std::uint64_t monotonicTimestampUsec_ = 0;
    ReconnectTeardownHandlerSnapshot snapshot_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_HANDLER_H
