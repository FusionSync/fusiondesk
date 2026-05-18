#include "fusiondesk/core/network/request_tracker.h"

#include <utility>

namespace fusiondesk {
namespace network {

RequestTracker::RequestTracker(protocol::MessageId firstMessageId)
    : nextMessageId_(firstMessageId == 0 ? 1 : firstMessageId)
{
}

protocol::MessageId RequestTracker::nextMessageId()
{
    if (nextMessageId_ == 0)
        nextMessageId_ = 1;

    return nextMessageId_++;
}

TrackResult RequestTracker::track(const protocol::PacketEnvelope& request, ResponseHandler handler)
{
    if (!handler)
        return TrackResult::error(protocol::ResponseStatus::InvalidArgument, "response handler is required");

    if (!isTrackableRequest(request.messageKind))
        return TrackResult::error(protocol::ResponseStatus::InvalidArgument, "message kind is not trackable");

    if (request.messageId == 0)
        return TrackResult::error(protocol::ResponseStatus::InvalidArgument, "message id is required");

    if (request.correlationId == 0)
        return TrackResult::error(protocol::ResponseStatus::InvalidArgument, "correlation id is required");

    if (request.timeoutMs == 0)
        return TrackResult::error(protocol::ResponseStatus::InvalidArgument, "timeout is required");

    if (pending_.find(request.messageId) != pending_.end())
        return TrackResult::error(protocol::ResponseStatus::Conflict, "message id is already pending");

    PendingRequest pending;
    pending.request = request;
    pending.handler = std::move(handler);
    pending.deadlineUsec = request.monotonicTimestampUsec + static_cast<std::uint64_t>(request.timeoutMs) * 1000U;
    pending_[request.messageId] = std::move(pending);
    return TrackResult::ok();
}

bool RequestTracker::complete(const protocol::PacketEnvelope& response)
{
    if (response.responseTo == 0)
        return false;

    auto it = pending_.find(response.responseTo);
    if (it == pending_.end())
        return false;

    ResponseHandler handler = it->second.handler;
    if (handler)
        handler(response);

    if (isTerminalResponse(response.messageKind))
        pending_.erase(it);

    return true;
}

std::size_t RequestTracker::cancelByChannel(protocol::ChannelId channelId,
                                            protocol::ChannelType channelType,
                                            protocol::ResponseStatus status)
{
    std::vector<protocol::MessageId> toCancel;
    for (const auto& item : pending_) {
        const PendingRequest& pending = item.second;
        if (pending.request.channelId == channelId && pending.request.channelType == channelType)
            toCancel.push_back(item.first);
    }

    for (protocol::MessageId messageId : toCancel) {
        auto it = pending_.find(messageId);
        if (it == pending_.end())
            continue;

        protocol::PacketEnvelope response = makeSyntheticResponse(it->second, status, protocol::MessageKind::Error);
        ResponseHandler handler = it->second.handler;
        if (handler)
            handler(response);

        pending_.erase(it);
    }

    return toCancel.size();
}

std::size_t RequestTracker::expire(std::uint64_t nowUsec)
{
    std::vector<protocol::MessageId> expired;
    for (const auto& item : pending_) {
        if (item.second.deadlineUsec <= nowUsec)
            expired.push_back(item.first);
    }

    for (protocol::MessageId messageId : expired) {
        auto it = pending_.find(messageId);
        if (it == pending_.end())
            continue;

        protocol::PacketEnvelope response =
            makeSyntheticResponse(it->second, protocol::ResponseStatus::Timeout, protocol::MessageKind::Error);
        ResponseHandler handler = it->second.handler;
        if (handler)
            handler(response);

        pending_.erase(it);
    }

    return expired.size();
}

bool RequestTracker::hasPending(protocol::MessageId messageId) const
{
    return pending_.find(messageId) != pending_.end();
}

std::size_t RequestTracker::pendingCount() const
{
    return pending_.size();
}

std::vector<PendingRequestSnapshot> RequestTracker::snapshots() const
{
    std::vector<PendingRequestSnapshot> result;
    result.reserve(pending_.size());

    for (const auto& item : pending_) {
        const PendingRequest& pending = item.second;
        PendingRequestSnapshot snapshot;
        snapshot.messageId = pending.request.messageId;
        snapshot.correlationId = pending.request.correlationId;
        snapshot.channelId = pending.request.channelId;
        snapshot.channelType = pending.request.channelType;
        snapshot.packetType = pending.request.packetType;
        snapshot.deadlineUsec = pending.deadlineUsec;
        result.push_back(snapshot);
    }

    return result;
}

bool RequestTracker::isTrackableRequest(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Request ||
           messageKind == protocol::MessageKind::StreamStart ||
           messageKind == protocol::MessageKind::Cancel;
}

bool RequestTracker::isTerminalResponse(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error ||
           messageKind == protocol::MessageKind::StreamEnd;
}

protocol::PacketEnvelope RequestTracker::makeSyntheticResponse(const PendingRequest& pending,
                                                               protocol::ResponseStatus status,
                                                               protocol::MessageKind messageKind)
{
    protocol::PacketEnvelope response;
    response.protocolMajor = pending.request.protocolMajor;
    response.protocolMinor = pending.request.protocolMinor;
    response.sessionId = pending.request.sessionId;
    response.traceId = pending.request.traceId;
    response.correlationId = pending.request.correlationId;
    response.responseTo = pending.request.messageId;
    response.channelId = pending.request.channelId;
    response.channelType = pending.request.channelType;
    response.packetType = pending.request.packetType;
    response.messageKind = messageKind;
    response.priority = pending.request.priority;
    response.responseStatus = status;
    return response;
}

} // namespace network
} // namespace fusiondesk

