#include "fusiondesk/runtime/connection/reconnect_teardown_service.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

network::ChannelKey defaultControlChannel()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

void appendFailure(ReconnectTeardownDispatchResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(ReconnectTeardownServiceStartResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

} // namespace

ReconnectTeardownService::ReconnectTeardownService(
    network::INetworkRouter& router,
    network::IRequestTracker& requestTracker,
    IReconnectTeardownCloseTarget& closeTarget)
    : router_(router),
      requestTracker_(requestTracker),
      dispatcher_(router, requestTracker),
      handler_(router, closeTarget)
{
}

ReconnectTeardownService::~ReconnectTeardownService()
{
    stop();
}

ReconnectTeardownServiceStartResult ReconnectTeardownService::start(
    const ReconnectTeardownServiceOptions& options)
{
    ReconnectTeardownServiceStartResult result;
    if (active_) {
        appendFailure(result, "reconnect teardown service is already active");
        messages_.insert(messages_.end(), result.messages.begin(), result.messages.end());
        return result;
    }

    if (!options.startPeerHandler && !options.subscribeResponses) {
        appendFailure(result, "reconnect teardown service has no work to start");
        messages_.insert(messages_.end(), result.messages.begin(), result.messages.end());
        return result;
    }

    const network::ChannelKey controlChannel =
        normalizedControlChannel(options.handler.controlChannel);

    if (options.subscribeResponses) {
        subscribeResponse(protocol::MessageKind::Ack, controlChannel, result);
        subscribeResponse(protocol::MessageKind::Progress, controlChannel, result);
        subscribeResponse(protocol::MessageKind::Response, controlChannel, result);
        subscribeResponse(protocol::MessageKind::Error, controlChannel, result);
        subscribeResponse(protocol::MessageKind::StreamEnd, controlChannel, result);
    }

    if (options.startPeerHandler) {
        result.handler = handler_.start(options.handler);
        if (!result.handler.ok) {
            appendFailure(result,
                          result.handler.messages.empty()
                              ? "reconnect teardown handler start failed"
                              : result.handler.messages.front());
        }
    }

    if (!result.messages.empty()) {
        clearResponseSubscriptions();
        handler_.stop();
        messages_.insert(messages_.end(), result.messages.begin(), result.messages.end());
        return result;
    }

    active_ = true;
    result.ok = true;
    result.responseTokens = responseTokens_;
    return result;
}

void ReconnectTeardownService::stop()
{
    clearResponseSubscriptions();
    handler_.stop();
    active_ = false;
}

bool ReconnectTeardownService::active() const
{
    return active_;
}

ReconnectTeardownDispatchResult ReconnectTeardownService::dispatch(
    const ReconnectTeardownPlan& plan,
    const ReconnectTeardownDispatchOptions& options)
{
    ReconnectTeardownDispatchResult result;
    if (!active_) {
        appendFailure(result, "reconnect teardown service is not active");
        return result;
    }

    return dispatcher_.dispatch(plan, options);
}

ReconnectTeardownDispatchResult ReconnectTeardownService::dispatch(
    const ReconnectOrchestrationSidePlan& sidePlan,
    std::uint32_t timeoutMs,
    const ReconnectTeardownDispatchOptions& options)
{
    ReconnectTeardownDispatchResult result;
    if (!active_) {
        appendFailure(result, "reconnect teardown service is not active");
        return result;
    }

    ReconnectTeardownPlanResult planned =
        buildReconnectTeardownPlan(sidePlan, 1, timeoutMs);
    if (!planned.ok) {
        result.messages.insert(result.messages.end(),
                               planned.messages.begin(),
                               planned.messages.end());
        result.ok = false;
        return result;
    }

    for (ReconnectTeardownCommand& command : planned.plan.commands) {
        command.messageId = 0;
        command.correlationId = 0;
    }

    return dispatcher_.dispatch(planned.plan, options);
}

bool ReconnectTeardownService::complete(const protocol::PacketEnvelope& response)
{
    if (!responseMessageKind(response.messageKind))
        return false;

    return dispatcher_.complete(response);
}

std::size_t ReconnectTeardownService::expire(std::uint64_t nowUsec)
{
    return requestTracker_.expire(nowUsec);
}

ReconnectTeardownServiceSnapshot ReconnectTeardownService::snapshot() const
{
    ReconnectTeardownServiceSnapshot result;
    result.active = active_;
    result.pendingRequests = requestTracker_.pendingCount();
    result.terminalResponses = dispatcher_.terminalResponses().size();
    result.interimResponses = dispatcher_.interimResponses().size();
    result.summary = dispatcher_.summary();
    result.handler = handler_.snapshot();
    result.pending = requestTracker_.snapshots();
    result.messages = messages_;
    return result;
}

network::ChannelKey ReconnectTeardownService::normalizedControlChannel(
    network::ChannelKey requested)
{
    if (requested.channelId ==
            static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain) &&
        requested.channelType == protocol::ChannelType::Control) {
        return requested;
    }

    return defaultControlChannel();
}

network::RouteMatch ReconnectTeardownService::responseRoute(
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

bool ReconnectTeardownService::responseMessageKind(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Ack ||
           messageKind == protocol::MessageKind::Progress ||
           messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error ||
           messageKind == protocol::MessageKind::StreamEnd;
}

bool ReconnectTeardownService::subscribeResponse(
    protocol::MessageKind messageKind,
    network::ChannelKey controlChannel,
    ReconnectTeardownServiceStartResult& result)
{
    const network::SubscriptionToken token =
        router_.subscribe(responseRoute(controlChannel, messageKind),
                          [this](const protocol::PacketEnvelope& response) {
                              complete(response);
                          });
    if (token == 0) {
        appendFailure(result, "reconnect teardown response subscription failed");
        return false;
    }

    responseTokens_.push_back(token);
    return true;
}

void ReconnectTeardownService::clearResponseSubscriptions()
{
    for (network::SubscriptionToken token : responseTokens_)
        router_.unsubscribe(token);
    responseTokens_.clear();
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
