#ifndef FUSIONDESK_NETWORK_REQUEST_TRACKER_H
#define FUSIONDESK_NETWORK_REQUEST_TRACKER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace network {

using ResponseHandler = std::function<void(const protocol::PacketEnvelope&)>;

struct TrackResult
{
    bool tracked = false;
    protocol::ResponseStatus status = protocol::ResponseStatus::Failed;
    std::string message;

    static TrackResult ok()
    {
        return {true, protocol::ResponseStatus::Ok, {}};
    }

    static TrackResult error(protocol::ResponseStatus status, std::string message)
    {
        return {false, status, std::move(message)};
    }
};

struct PendingRequestSnapshot
{
    protocol::MessageId messageId = 0;
    protocol::MessageId correlationId = 0;
    protocol::ChannelId channelId = 0;
    protocol::ChannelType channelType = protocol::ChannelType::Standard;
    protocol::PacketType packetType = protocol::PacketType::Heartbeat;
    std::uint64_t deadlineUsec = 0;
};

class IRequestTracker
{
public:
    virtual ~IRequestTracker() = default;

    virtual protocol::MessageId nextMessageId() = 0;
    virtual TrackResult track(const protocol::PacketEnvelope& request, ResponseHandler handler) = 0;
    virtual bool complete(const protocol::PacketEnvelope& response) = 0;
    virtual std::size_t cancelByChannel(protocol::ChannelId channelId,
                                        protocol::ChannelType channelType,
                                        protocol::ResponseStatus status) = 0;
    virtual std::size_t expire(std::uint64_t nowUsec) = 0;
    virtual bool hasPending(protocol::MessageId messageId) const = 0;
    virtual std::size_t pendingCount() const = 0;
    virtual std::vector<PendingRequestSnapshot> snapshots() const = 0;
};

class RequestTracker : public IRequestTracker
{
public:
    explicit RequestTracker(protocol::MessageId firstMessageId = 1);

    protocol::MessageId nextMessageId() override;
    TrackResult track(const protocol::PacketEnvelope& request, ResponseHandler handler) override;
    bool complete(const protocol::PacketEnvelope& response) override;
    std::size_t cancelByChannel(protocol::ChannelId channelId,
                                protocol::ChannelType channelType,
                                protocol::ResponseStatus status) override;
    std::size_t expire(std::uint64_t nowUsec) override;
    bool hasPending(protocol::MessageId messageId) const override;
    std::size_t pendingCount() const override;
    std::vector<PendingRequestSnapshot> snapshots() const override;

private:
    struct PendingRequest
    {
        protocol::PacketEnvelope request;
        ResponseHandler handler;
        std::uint64_t deadlineUsec = 0;
    };

    static bool isTrackableRequest(protocol::MessageKind messageKind);
    static bool isTerminalResponse(protocol::MessageKind messageKind);
    static protocol::PacketEnvelope makeSyntheticResponse(const PendingRequest& pending,
                                                          protocol::ResponseStatus status,
                                                          protocol::MessageKind messageKind);

private:
    protocol::MessageId nextMessageId_ = 1;
    std::map<protocol::MessageId, PendingRequest> pending_;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_REQUEST_TRACKER_H
