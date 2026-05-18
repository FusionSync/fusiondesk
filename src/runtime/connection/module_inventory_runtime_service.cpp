#include "fusiondesk/runtime/connection/module_inventory_runtime_service.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(ModuleInventoryRuntimeServiceStartResult& result,
                   std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(ModuleInventoryRuntimeDispatchResult& result,
                   std::string message)
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

ModuleInventoryRuntimeService::ModuleInventoryRuntimeService(
    network::INetworkRouter& router,
    protocol::MessageId firstMessageId)
    : router_(router),
      requestTracker_(firstMessageId),
      responder_(router)
{
}

ModuleInventoryRuntimeService::~ModuleInventoryRuntimeService()
{
    stop();
}

ModuleInventoryRuntimeServiceStartResult ModuleInventoryRuntimeService::start(
    const ModuleInventoryRuntimeServiceStartOptions& options)
{
    ModuleInventoryRuntimeServiceStartResult result;
    if (active_) {
        appendFailure(result, "module inventory runtime service is already active");
        remember(result.messages);
        return result;
    }

    if (!options.startResponder && !options.subscribeResponses) {
        appendFailure(result, "module inventory runtime service has no work to start");
        remember(result.messages);
        return result;
    }

    controlChannel_ = options.controlChannel;

    if (options.subscribeResponses) {
        subscribeResponse(protocol::MessageKind::Response, result);
        subscribeResponse(protocol::MessageKind::Error, result);
    }

    if (options.startResponder) {
        ModuleInventoryServiceStartOptions responderOptions = options.responder;
        responderOptions.controlChannel = controlChannel_;
        result.responder = responder_.start(responderOptions);
        if (!result.responder.ok) {
            const std::string message = result.responder.messages.empty()
                ? "module inventory responder start failed"
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

void ModuleInventoryRuntimeService::stop()
{
    requestTracker_.cancelByChannel(controlChannel_.channelId,
                                    controlChannel_.channelType,
                                    protocol::ResponseStatus::Cancelled);
    clearResponseSubscriptions();
    responder_.stop();
    active_ = false;
}

bool ModuleInventoryRuntimeService::active() const
{
    return active_;
}

ModuleInventoryRuntimeDispatchResult ModuleInventoryRuntimeService::requestModuleInventory(
    const ModuleInventory& inventory,
    const ModuleInventoryRuntimeExchangeOptions& options)
{
    ModuleInventoryRuntimeDispatchResult result;
    if (!active_) {
        appendFailure(result, "module inventory runtime service is not active");
        return result;
    }

    ModuleInventoryWireOptions wire = options.wire;
    wire.controlChannel = controlChannel_;
    if (options.assignMissingMessageId && wire.messageId == 0)
        wire.messageId = requestTracker_.nextMessageId();
    if (wire.correlationId == 0)
        wire.correlationId = wire.messageId;

    result.request = makeModuleInventoryRequestPacket(inventory, wire);
    if (result.request.payload.empty()) {
        appendFailure(result, "module inventory runtime request payload encode failed");
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
                                  ? "module inventory runtime request tracking failed"
                                  : tracked.message);
        remember(result.messages);
        return result;
    }

    const network::SendResult sent = router_.send(result.request);
    if (sent.status != network::SendStatus::Sent) {
        const std::string message = sent.message.empty()
            ? "module inventory runtime request send failed"
            : sent.message;
        failTrackedRequest(result.request, sendStatusToResponseStatus(sent.status), message);
        appendFailure(result, message);
        remember(result.messages);
        return result;
    }

    result.ok = true;
    return result;
}

bool ModuleInventoryRuntimeService::complete(const protocol::PacketEnvelope& response)
{
    if (!responseMessageKind(response.messageKind))
        return false;

    return requestTracker_.complete(response);
}

std::size_t ModuleInventoryRuntimeService::expire(std::uint64_t nowUsec)
{
    const std::size_t expired = requestTracker_.expire(nowUsec);
    expiredRequests_ += expired;
    return expired;
}

ModuleInventoryRuntimeServiceSnapshot ModuleInventoryRuntimeService::snapshot() const
{
    ModuleInventoryRuntimeServiceSnapshot result;
    result.active = active_;
    result.responder = responder_.snapshot();
    result.pendingRequests = requestTracker_.pendingCount();
    result.completedResponses = completions_.size();
    result.expiredRequests = expiredRequests_;
    result.pending = requestTracker_.snapshots();
    result.completions = completions_;
    result.messages = messages_;
    return result;
}

const ModuleInventory& ModuleInventoryRuntimeService::lastRemoteInventoryFromResponder() const
{
    return responder_.lastRemoteInventory();
}

network::RouteMatch ModuleInventoryRuntimeService::responseRoute(
    network::ChannelKey controlChannel,
    protocol::MessageKind messageKind)
{
    network::RouteMatch route;
    route.channelId = controlChannel.channelId;
    route.channelType = controlChannel.channelType;
    route.packetType = protocol::PacketType::Exchange;
    route.messageKind = messageKind;
    return route;
}

bool ModuleInventoryRuntimeService::responseMessageKind(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error;
}

bool ModuleInventoryRuntimeService::terminalMessageKind(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error;
}

protocol::ResponseStatus ModuleInventoryRuntimeService::sendStatusToResponseStatus(
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

bool ModuleInventoryRuntimeService::subscribeResponse(
    protocol::MessageKind messageKind,
    ModuleInventoryRuntimeServiceStartResult& result)
{
    const network::SubscriptionToken token =
        router_.subscribe(responseRoute(controlChannel_, messageKind),
                          [this](const protocol::PacketEnvelope& response) {
                              complete(response);
                          });
    if (token == 0) {
        appendFailure(result, "module inventory runtime response subscription failed");
        return false;
    }

    responseTokens_.push_back(token);
    return true;
}

void ModuleInventoryRuntimeService::clearResponseSubscriptions()
{
    for (network::SubscriptionToken token : responseTokens_)
        router_.unsubscribe(token);
    responseTokens_.clear();
}

void ModuleInventoryRuntimeService::recordResponse(
    const protocol::PacketEnvelope& response)
{
    if (!terminalMessageKind(response.messageKind))
        return;

    ModuleInventoryRuntimeCompletion completion;
    completion.terminal = true;
    completion.response = response;

    if (response.messageKind == protocol::MessageKind::Response) {
        const ModuleInventoryDecodeResult decoded =
            decodeModuleInventoryResponsePacket(response);
        if (decoded.ok) {
            completion.ok = true;
            completion.inventory = decoded.inventory;
        } else {
            completion.ok = false;
            completion.messages.push_back(decoded.message.empty()
                                              ? "module inventory runtime response decode failed"
                                              : decoded.message);
        }
    } else {
        completion.ok = false;
        std::string message = payloadToString(response.payload);
        if (message.empty())
            message = responseStatusName(response.responseStatus);
        completion.messages.push_back(message);
    }

    completions_.push_back(std::move(completion));
}

void ModuleInventoryRuntimeService::failTrackedRequest(
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

void ModuleInventoryRuntimeService::remember(const std::vector<std::string>& messages)
{
    messages_.insert(messages_.end(), messages.begin(), messages.end());
}

void ModuleInventoryRuntimeService::remember(std::string message)
{
    if (!message.empty())
        messages_.push_back(std::move(message));
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
