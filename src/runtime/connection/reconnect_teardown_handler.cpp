#include "fusiondesk/runtime/connection/reconnect_teardown_handler.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

network::ChannelKey reconnectTeardownDefaultControlChannel()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey normalizedReconnectTeardownControlRoute(network::ChannelKey requested)
{
    const network::ChannelKey defaultRoute = reconnectTeardownDefaultControlChannel();
    return requested == defaultRoute ? requested : defaultRoute;
}

void appendMessage(ReconnectTeardownHandlerSnapshot& snapshot, std::string message)
{
    snapshot.messages.push_back(std::move(message));
}

} // namespace

ReconnectTeardownHandler::ReconnectTeardownHandler(network::INetworkRouter& router,
                                                   IReconnectTeardownCloseTarget& closeTarget)
    : router_(router),
      closeTarget_(closeTarget)
{
}

ReconnectTeardownHandler::~ReconnectTeardownHandler()
{
    stop();
}

ReconnectTeardownHandlerStartResult ReconnectTeardownHandler::start(
    const ReconnectTeardownHandlerOptions& options)
{
    ReconnectTeardownHandlerStartResult result;
    if (token_ != 0) {
        result.messages.push_back("reconnect teardown handler is already active");
        return result;
    }

    nextResponseMessageId_ = normalizeFirstResponseMessageId(options.firstResponseMessageId);
    nextSequence_ = options.sequence;
    monotonicTimestampUsec_ = options.monotonicTimestampUsec;
    const network::ChannelKey controlRoute =
        normalizedReconnectTeardownControlRoute(options.controlChannel);

    network::RouteMatch route;
    route.channelId = controlRoute.channelId;
    route.channelType = controlRoute.channelType;
    route.messageKind = protocol::MessageKind::Request;
    route.packetType = protocol::PacketType::Control;
    token_ = router_.subscribe(route, [this](const protocol::PacketEnvelope& packet) {
        handleIncoming(packet);
    });

    if (token_ == 0) {
        result.messages.push_back("reconnect teardown handler subscription failed");
        return result;
    }

    snapshot_.active = true;
    snapshot_.token = token_;
    result.ok = true;
    result.token = token_;
    return result;
}

void ReconnectTeardownHandler::stop()
{
    if (token_ == 0)
        return;

    router_.unsubscribe(token_);
    token_ = 0;
    snapshot_.active = false;
    snapshot_.token = 0;
}

bool ReconnectTeardownHandler::handleIncoming(const protocol::PacketEnvelope& packet)
{
    if (!looksLikeReconnectTeardownPayload(packet.payload)) {
        ++snapshot_.ignoredPackets;
        return false;
    }

    const ReconnectTeardownWireDecodeResult decoded =
        decodeReconnectTeardownCommandPacket(packet);
    if (!decoded.ok) {
        ++snapshot_.malformedRequests;
        sendMalformedRequestError(packet, decoded.message);
        return true;
    }

    ReconnectTeardownCloseRequest closeRequest;
    closeRequest.sessionId = decoded.command.sessionId;
    closeRequest.targetChannel = decoded.command.targetChannel;
    closeRequest.reason = decoded.command.reason;

    ++snapshot_.handledRequests;
    const ReconnectTeardownCloseResult closeResult =
        closeTarget_.closeOldTransport(closeRequest);
    sendCloseResult(packet, closeResult);
    return true;
}

ReconnectTeardownHandlerSnapshot ReconnectTeardownHandler::snapshot() const
{
    ReconnectTeardownHandlerSnapshot result = snapshot_;
    result.active = token_ != 0;
    result.token = token_;
    return result;
}

bool ReconnectTeardownHandler::looksLikeReconnectTeardownPayload(const protocol::ByteBuffer& payload)
{
    if (payload.size() < 4)
        return false;

    return payload[0] == static_cast<std::uint8_t>('F') &&
           payload[1] == static_cast<std::uint8_t>('D') &&
           payload[2] == static_cast<std::uint8_t>('R') &&
           payload[3] == static_cast<std::uint8_t>('T');
}

protocol::MessageId ReconnectTeardownHandler::normalizeFirstResponseMessageId(
    protocol::MessageId messageId)
{
    return messageId == 0 ? 1 : messageId;
}

protocol::MessageId ReconnectTeardownHandler::nextResponseMessageId()
{
    if (nextResponseMessageId_ == 0)
        nextResponseMessageId_ = 1;
    return nextResponseMessageId_++;
}

bool ReconnectTeardownHandler::sendMalformedRequestError(const protocol::PacketEnvelope& request,
                                                         const std::string& message)
{
    const std::string error = message.empty()
        ? "reconnect teardown request is malformed"
        : message;
    return sendResponse(request, protocol::ResponseStatus::ProtocolError, error);
}

bool ReconnectTeardownHandler::sendCloseResult(
    const protocol::PacketEnvelope& request,
    const ReconnectTeardownCloseResult& closeResult)
{
    const protocol::ResponseStatus status = closeResult.ok
        ? protocol::ResponseStatus::Ok
        : (closeResult.status == protocol::ResponseStatus::Ok
               ? protocol::ResponseStatus::Failed
               : closeResult.status);
    return sendResponse(request, status, closeResult.message);
}

bool ReconnectTeardownHandler::sendResponse(const protocol::PacketEnvelope& request,
                                            protocol::ResponseStatus status,
                                            const std::string& message)
{
    ReconnectTeardownWireResponseOptions responseOptions;
    responseOptions.messageId = nextResponseMessageId();
    responseOptions.sequence = nextSequence_++;
    responseOptions.monotonicTimestampUsec = monotonicTimestampUsec_;
    responseOptions.status = status;
    responseOptions.message = message;

    const protocol::PacketEnvelope response =
        makeReconnectTeardownResponsePacket(request, responseOptions);
    const network::SendResult sent = router_.send(response);
    if (sent.status == network::SendStatus::Sent)
        return true;

    ++snapshot_.failedResponseSends;
    appendMessage(snapshot_,
                  sent.message.empty()
                      ? "reconnect teardown response send failed"
                      : sent.message);
    return false;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
