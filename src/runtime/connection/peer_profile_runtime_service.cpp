#include "fusiondesk/runtime/connection/peer_profile_runtime_service.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(PeerProfileRuntimeServiceStartResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(PeerProfileRuntimeDispatchResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

protocol::ByteBuffer stringToPayload(const std::string& message)
{
    return protocol::ByteBuffer(message.begin(), message.end());
}

std::string payloadToString(const protocol::ByteBuffer& payload)
{
    return std::string(payload.begin(), payload.end());
}

std::string responseStatusName(protocol::ResponseStatus status)
{
    switch (status) {
    case protocol::ResponseStatus::Ok:
        return "ok";
    case protocol::ResponseStatus::Accepted:
        return "accepted";
    case protocol::ResponseStatus::Progress:
        return "progress";
    case protocol::ResponseStatus::InvalidArgument:
        return "invalid argument";
    case protocol::ResponseStatus::Unauthorized:
        return "unauthorized";
    case protocol::ResponseStatus::DeniedByPolicy:
        return "denied by policy";
    case protocol::ResponseStatus::Unsupported:
        return "unsupported";
    case protocol::ResponseStatus::NotFound:
        return "not found";
    case protocol::ResponseStatus::Conflict:
        return "conflict";
    case protocol::ResponseStatus::Busy:
        return "busy";
    case protocol::ResponseStatus::Timeout:
        return "timeout";
    case protocol::ResponseStatus::Cancelled:
        return "cancelled";
    case protocol::ResponseStatus::TooLarge:
        return "too large";
    case protocol::ResponseStatus::BackPressure:
        return "back pressure";
    case protocol::ResponseStatus::ChannelUnavailable:
        return "channel unavailable";
    case protocol::ResponseStatus::Failed:
        return "failed";
    case protocol::ResponseStatus::InternalError:
        return "internal error";
    case protocol::ResponseStatus::ProtocolError:
        return "protocol error";
    }
    return "failed";
}

} // namespace

PeerProfileRuntimeService::PeerProfileRuntimeService(network::INetworkRouter& router,
                                                     protocol::MessageId firstMessageId)
    : router_(router),
      requestTracker_(firstMessageId),
      responder_(router)
{
}

PeerProfileRuntimeService::~PeerProfileRuntimeService()
{
    stop();
}

PeerProfileRuntimeServiceStartResult PeerProfileRuntimeService::start(
    const PeerProfileRuntimeServiceStartOptions& options)
{
    PeerProfileRuntimeServiceStartResult result;
    if (active_) {
        appendFailure(result, "peer profile runtime service is already active");
        remember(result.messages);
        return result;
    }

    if (!options.startResponder && !options.subscribeResponses) {
        appendFailure(result, "peer profile runtime service has no work to start");
        remember(result.messages);
        return result;
    }

    controlChannel_ = normalizedControlChannel(options.controlChannel);

    if (options.subscribeResponses) {
        subscribeResponse(protocol::MessageKind::Ack, result);
        subscribeResponse(protocol::MessageKind::Progress, result);
        subscribeResponse(protocol::MessageKind::Response, result);
        subscribeResponse(protocol::MessageKind::Error, result);
        subscribeResponse(protocol::MessageKind::StreamEnd, result);
    }

    if (options.startResponder) {
        PeerProfileServiceStartOptions responderOptions = options.responder;
        responderOptions.controlChannel = controlChannel_;
        result.responder = responder_.start(responderOptions);
        if (!result.responder.ok) {
            const std::string message = result.responder.messages.empty()
                ? "peer profile responder start failed"
                : result.responder.messages.front();
            appendFailure(result, message);
        }
    }

    if (!result.messages.empty()) {
        clearResponseSubscriptions();
        responder_.stop();
        remember(result.messages);
        return result;
    }

    active_ = true;
    result.ok = true;
    result.responseTokens = responseTokens_;
    return result;
}

void PeerProfileRuntimeService::stop()
{
    requestTracker_.cancelByChannel(controlChannel_.channelId,
                                    controlChannel_.channelType,
                                    protocol::ResponseStatus::Cancelled);
    clearResponseSubscriptions();
    responder_.stop();
    active_ = false;
}

bool PeerProfileRuntimeService::active() const
{
    return active_;
}

PeerProfileRuntimeDispatchResult PeerProfileRuntimeService::requestPeerProfile(
    const PeerProfileExchangeRequest& request,
    const PeerProfileRuntimeExchangeOptions& options)
{
    PeerProfileRuntimeDispatchResult result;
    if (!active_) {
        appendFailure(result, "peer profile runtime service is not active");
        return result;
    }

    PeerProfileServiceWireOptions wire = options.wire;
    wire.controlChannel = controlChannel_;
    if (options.assignMissingMessageId && wire.messageId == 0)
        wire.messageId = requestTracker_.nextMessageId();
    if (wire.correlationId == 0)
        wire.correlationId = wire.messageId;

    result.request = makePeerProfileExchangeRequestPacket(request, wire);
    if (result.request.payload.empty()) {
        appendFailure(result, "peer profile runtime request payload encode failed");
        remember(result.messages);
        return result;
    }

    const network::TrackResult tracked =
        requestTracker_.track(result.request,
                              [this](const protocol::PacketEnvelope& response) {
                                  recordResponse(response);
                              });
    if (!tracked.tracked) {
        appendFailure(result, tracked.message.empty()
                                  ? "peer profile runtime request tracking failed"
                                  : tracked.message);
        remember(result.messages);
        return result;
    }

    const network::SendResult sent = router_.send(result.request);
    if (sent.status != network::SendStatus::Sent) {
        const std::string message = sent.message.empty()
            ? "peer profile runtime request send failed"
            : sent.message;
        failTrackedRequest(result.request, sendStatusToResponseStatus(sent.status), message);
        appendFailure(result, message);
        remember(result.messages);
        return result;
    }

    result.ok = true;
    return result;
}

bool PeerProfileRuntimeService::complete(const protocol::PacketEnvelope& response)
{
    if (!responseMessageKind(response.messageKind))
        return false;

    return requestTracker_.complete(response);
}

std::size_t PeerProfileRuntimeService::expire(std::uint64_t nowUsec)
{
    const std::size_t expired = requestTracker_.expire(nowUsec);
    expiredRequests_ += expired;
    return expired;
}

PeerProfileRuntimeServiceSnapshot PeerProfileRuntimeService::snapshot() const
{
    PeerProfileRuntimeServiceSnapshot result;
    result.active = active_;
    result.responder = responder_.snapshot();
    result.pendingRequests = requestTracker_.pendingCount();
    result.interimResponses = interimResponses_.size();
    result.completedResponses = completions_.size();
    result.expiredRequests = expiredRequests_;
    result.pending = requestTracker_.snapshots();
    result.interimPackets = interimResponses_;
    result.completions = completions_;
    result.messages = messages_;
    return result;
}

network::ChannelKey PeerProfileRuntimeService::defaultControlChannel()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey PeerProfileRuntimeService::normalizedControlChannel(
    network::ChannelKey requested)
{
    if (requested == defaultControlChannel())
        return requested;
    return defaultControlChannel();
}

network::RouteMatch PeerProfileRuntimeService::responseRoute(
    network::ChannelKey controlChannel,
    protocol::MessageKind messageKind)
{
    network::RouteMatch route;
    route.channelId = controlChannel.channelId;
    route.channelType = controlChannel.channelType;
    route.packetType = protocol::PacketType::Control;
    route.messageKind = messageKind;
    return route;
}

bool PeerProfileRuntimeService::responseMessageKind(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Ack ||
           messageKind == protocol::MessageKind::Progress ||
           messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error ||
           messageKind == protocol::MessageKind::StreamEnd;
}

bool PeerProfileRuntimeService::terminalMessageKind(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error ||
           messageKind == protocol::MessageKind::StreamEnd;
}

protocol::ResponseStatus PeerProfileRuntimeService::sendStatusToResponseStatus(
    network::SendStatus status)
{
    switch (status) {
    case network::SendStatus::Sent:
        return protocol::ResponseStatus::Ok;
    case network::SendStatus::ChannelClosed:
    case network::SendStatus::ChannelNotFound:
        return protocol::ResponseStatus::ChannelUnavailable;
    case network::SendStatus::BackPressure:
        return protocol::ResponseStatus::BackPressure;
    case network::SendStatus::Failed:
        return protocol::ResponseStatus::Failed;
    }
    return protocol::ResponseStatus::Failed;
}

bool PeerProfileRuntimeService::subscribeResponse(
    protocol::MessageKind messageKind,
    PeerProfileRuntimeServiceStartResult& result)
{
    const network::SubscriptionToken token =
        router_.subscribe(responseRoute(controlChannel_, messageKind),
                          [this](const protocol::PacketEnvelope& response) {
                              complete(response);
                          });
    if (token == 0) {
        appendFailure(result, "peer profile runtime response subscription failed");
        return false;
    }

    responseTokens_.push_back(token);
    return true;
}

void PeerProfileRuntimeService::clearResponseSubscriptions()
{
    for (network::SubscriptionToken token : responseTokens_)
        router_.unsubscribe(token);
    responseTokens_.clear();
}

void PeerProfileRuntimeService::recordResponse(const protocol::PacketEnvelope& response)
{
    if (!terminalMessageKind(response.messageKind)) {
        interimResponses_.push_back(response);
        return;
    }

    PeerProfileRuntimeCompletion completion;
    completion.terminal = true;
    completion.response = response;

    const PeerProfileServiceResponseDecodeResult decoded =
        decodePeerProfileExchangeResponsePacket(response);
    if (decoded.ok) {
        completion.ok = decoded.exchange.ok;
        completion.exchange = decoded.exchange;
    } else if (response.messageKind == protocol::MessageKind::Error) {
        completion.ok = false;
        completion.exchange.ok = false;
        const std::string message = !decoded.message.empty()
            ? decoded.message
            : responseStatusName(response.responseStatus);
        if (!message.empty()) {
            completion.exchange.messages.push_back(message);
            completion.messages.push_back(message);
        }
    } else {
        completion.ok = false;
        completion.exchange.ok = false;
        const std::string message = decoded.message.empty()
            ? "peer profile runtime response decode failed"
            : decoded.message;
        completion.exchange.messages.push_back(message);
        completion.messages.push_back(message);
    }

    completions_.push_back(std::move(completion));
}

void PeerProfileRuntimeService::failTrackedRequest(
    const protocol::PacketEnvelope& request,
    protocol::ResponseStatus status,
    const std::string& message)
{
    protocol::PacketEnvelope response;
    response.protocolMajor = request.protocolMajor;
    response.protocolMinor = request.protocolMinor;
    response.sessionId = request.sessionId;
    response.traceId = request.traceId;
    response.correlationId = request.correlationId;
    response.responseTo = request.messageId;
    response.channelId = request.channelId;
    response.channelType = request.channelType;
    response.packetType = request.packetType;
    response.messageKind = protocol::MessageKind::Error;
    response.priority = request.priority;
    response.responseStatus = status == protocol::ResponseStatus::Ok
        ? protocol::ResponseStatus::Failed
        : status;
    response.payload = stringToPayload(message);
    requestTracker_.complete(response);
}

void PeerProfileRuntimeService::remember(const std::vector<std::string>& messages)
{
    messages_.insert(messages_.end(), messages.begin(), messages.end());
}

void PeerProfileRuntimeService::remember(std::string message)
{
    if (!message.empty())
        messages_.push_back(std::move(message));
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
