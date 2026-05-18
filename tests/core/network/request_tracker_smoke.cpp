#include <cassert>
#include <vector>

#include "fusiondesk/core/network/request_tracker.h"

using namespace fusiondesk;

namespace {

protocol::PacketEnvelope makeRequest(protocol::MessageId messageId)
{
    protocol::PacketEnvelope request;
    request.sessionId = 7;
    request.traceId = 9;
    request.messageId = messageId;
    request.correlationId = messageId;
    request.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    request.channelType = protocol::ChannelType::Video;
    request.packetType = protocol::PacketType::PayloadAck;
    request.messageKind = protocol::MessageKind::Request;
    request.priority = protocol::PacketPriority::Interactive;
    request.monotonicTimestampUsec = 1000;
    request.timeoutMs = 10;
    return request;
}

protocol::PacketEnvelope makeResponse(protocol::MessageId responseTo,
                                      protocol::MessageKind kind,
                                      protocol::ResponseStatus status)
{
    protocol::PacketEnvelope response;
    response.messageId = responseTo + 1000;
    response.correlationId = responseTo;
    response.responseTo = responseTo;
    response.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    response.channelType = protocol::ChannelType::Video;
    response.packetType = protocol::PacketType::PayloadAck;
    response.messageKind = kind;
    response.responseStatus = status;
    return response;
}

void tracksAckAndFinalResponse()
{
    network::RequestTracker tracker;
    std::vector<protocol::PacketEnvelope> callbacks;

    protocol::PacketEnvelope request = makeRequest(tracker.nextMessageId());
    network::TrackResult result = tracker.track(
        request,
        [&callbacks](const protocol::PacketEnvelope& response) {
            callbacks.push_back(response);
        });

    assert(result.tracked);
    assert(tracker.pendingCount() == 1);

    assert(tracker.complete(makeResponse(request.messageId, protocol::MessageKind::Ack, protocol::ResponseStatus::Accepted)));
    assert(tracker.pendingCount() == 1);
    assert(callbacks.size() == 1);
    assert(callbacks.back().messageKind == protocol::MessageKind::Ack);

    assert(tracker.complete(makeResponse(request.messageId, protocol::MessageKind::Response, protocol::ResponseStatus::Ok)));
    assert(tracker.pendingCount() == 0);
    assert(callbacks.size() == 2);
    assert(callbacks.back().messageKind == protocol::MessageKind::Response);
    assert(callbacks.back().responseStatus == protocol::ResponseStatus::Ok);
}

void expiresTimedOutRequest()
{
    network::RequestTracker tracker;
    std::vector<protocol::PacketEnvelope> callbacks;

    protocol::PacketEnvelope request = makeRequest(tracker.nextMessageId());
    network::TrackResult result = tracker.track(
        request,
        [&callbacks](const protocol::PacketEnvelope& response) {
            callbacks.push_back(response);
        });

    assert(result.tracked);
    assert(tracker.expire(11000) == 1);
    assert(tracker.pendingCount() == 0);
    assert(callbacks.size() == 1);
    assert(callbacks.back().messageKind == protocol::MessageKind::Error);
    assert(callbacks.back().responseTo == request.messageId);
    assert(callbacks.back().responseStatus == protocol::ResponseStatus::Timeout);
}

} // namespace

int main()
{
    tracksAckAndFinalResponse();
    expiresTimedOutRequest();
    return 0;
}

