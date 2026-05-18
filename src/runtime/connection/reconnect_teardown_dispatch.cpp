#include "fusiondesk/runtime/connection/reconnect_teardown_dispatch.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(ReconnectTeardownDispatchResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

} // namespace

ReconnectTeardownDispatcher::ReconnectTeardownDispatcher(network::INetworkRouter& router,
                                                         network::IRequestTracker& requestTracker)
    : router_(router),
      requestTracker_(requestTracker)
{
}

ReconnectTeardownDispatchResult ReconnectTeardownDispatcher::dispatch(
    const ReconnectTeardownPlan& plan,
    const ReconnectTeardownDispatchOptions& options)
{
    ReconnectTeardownDispatchResult result;
    activePlan_ = plan;
    terminalResponses_.clear();
    interimResponses_.clear();

    if (activePlan_.commands.empty()) {
        appendFailure(result, "reconnect teardown dispatch requires commands");
        result.summary = summary();
        return result;
    }

    for (std::size_t index = 0; index < activePlan_.commands.size(); ++index) {
        ReconnectTeardownCommand& command = activePlan_.commands[index];
        if (options.assignMissingMessageIds && command.messageId == 0) {
            command.messageId = requestTracker_.nextMessageId();
            command.correlationId = command.messageId;
        } else if (command.correlationId == 0) {
            command.correlationId = command.messageId;
        }

        ReconnectTeardownWireOptions wire = options.wire;
        wire.sequence += static_cast<std::uint64_t>(index);
        protocol::PacketEnvelope request = makeReconnectTeardownRequestPacket(command, wire);
        const network::TrackResult tracked = requestTracker_.track(
            request,
            [this, command](const protocol::PacketEnvelope& response) {
                recordTrackedResponse(command, response);
            });

        if (!tracked.tracked) {
            appendFailure(result, tracked.message.empty()
                                      ? "reconnect teardown request tracking failed"
                                      : tracked.message);
            continue;
        }

        const network::SendResult sent = router_.send(request);
        if (sent.status != network::SendStatus::Sent) {
            const std::string message = sent.message.empty()
                ? "reconnect teardown request send failed"
                : sent.message;
            failTrackedRequest(request, sendStatusToResponseStatus(sent.status), message);
            appendFailure(result, message);
            continue;
        }

        result.sentRequests.push_back(request);
    }

    result.summary = summary();
    result.ok = result.messages.empty();
    return result;
}

bool ReconnectTeardownDispatcher::complete(const protocol::PacketEnvelope& response)
{
    return requestTracker_.complete(response);
}

ReconnectTeardownResponseSummary ReconnectTeardownDispatcher::summary() const
{
    return summarizeReconnectTeardownResponses(activePlan_, terminalResponses_);
}

const std::vector<ReconnectTeardownResponse>& ReconnectTeardownDispatcher::terminalResponses() const
{
    return terminalResponses_;
}

const std::vector<ReconnectTeardownResponse>& ReconnectTeardownDispatcher::interimResponses() const
{
    return interimResponses_;
}

const ReconnectTeardownPlan& ReconnectTeardownDispatcher::activePlan() const
{
    return activePlan_;
}

bool ReconnectTeardownDispatcher::terminalMessageKind(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error ||
           messageKind == protocol::MessageKind::StreamEnd;
}

protocol::ResponseStatus ReconnectTeardownDispatcher::sendStatusToResponseStatus(network::SendStatus status)
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

void ReconnectTeardownDispatcher::recordTrackedResponse(const ReconnectTeardownCommand& command,
                                                        const protocol::PacketEnvelope& response)
{
    ReconnectTeardownResponse teardownResponse =
        reconnectTeardownResponseFromPacket(command, response);
    if (terminalMessageKind(response.messageKind))
        terminalResponses_.push_back(std::move(teardownResponse));
    else
        interimResponses_.push_back(std::move(teardownResponse));
}

void ReconnectTeardownDispatcher::failTrackedRequest(const protocol::PacketEnvelope& request,
                                                     protocol::ResponseStatus status,
                                                     const std::string& message)
{
    ReconnectTeardownWireResponseOptions responseOptions;
    responseOptions.status = status == protocol::ResponseStatus::Ok
        ? protocol::ResponseStatus::Failed
        : status;
    responseOptions.message = message;
    const protocol::PacketEnvelope response =
        makeReconnectTeardownResponsePacket(request, responseOptions);
    requestTracker_.complete(response);
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
