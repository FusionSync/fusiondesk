#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_ACK_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_ACK_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ReconnectTeardownPlanRequest
{
    protocol::SessionId sessionId = 0;
    std::vector<network::ChannelKey> channels;
    protocol::MessageId firstMessageId = 1;
    std::uint32_t timeoutMs = 1000;
    std::string reason = "reconnect old transport teardown";
};

struct ReconnectTeardownCommand
{
    protocol::SessionId sessionId = 0;
    network::ChannelKey targetChannel;
    protocol::MessageId messageId = 0;
    protocol::MessageId correlationId = 0;
    std::uint32_t timeoutMs = 0;
    std::string reason;
};

struct ReconnectTeardownPlan
{
    std::vector<ReconnectTeardownCommand> commands;
};

struct ReconnectTeardownPlanResult
{
    bool ok = false;
    ReconnectTeardownPlan plan;
    std::vector<std::string> messages;
};

struct ReconnectTeardownResponse
{
    protocol::SessionId sessionId = 0;
    network::ChannelKey targetChannel;
    protocol::MessageId responseTo = 0;
    protocol::MessageId correlationId = 0;
    protocol::MessageKind messageKind = protocol::MessageKind::Response;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Ok;
    std::string message;
};

struct ReconnectTeardownResponseSummary
{
    bool complete = false;
    bool ok = false;
    std::vector<network::ChannelKey> completedTargetChannels;
    std::vector<std::string> messages;
};

struct ReconnectTeardownWireOptions
{
    protocol::TraceId traceId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::PacketPriority priority = protocol::PacketPriority::Critical;
};

struct ReconnectTeardownWireResponseOptions
{
    protocol::MessageId messageId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
    std::string message;
};

struct ReconnectTeardownWireDecodeResult
{
    bool ok = false;
    ReconnectTeardownCommand command;
    std::string message;
};

ReconnectTeardownPlanResult buildReconnectTeardownPlan(
    const ReconnectTeardownPlanRequest& request);

ReconnectTeardownPlanResult buildReconnectTeardownPlan(
    const ReconnectOrchestrationSidePlan& sidePlan,
    protocol::MessageId firstMessageId = 1,
    std::uint32_t timeoutMs = 1000);

ReconnectTeardownResponseSummary summarizeReconnectTeardownResponses(
    const ReconnectTeardownPlan& plan,
    const std::vector<ReconnectTeardownResponse>& responses);

protocol::ByteBuffer encodeReconnectTeardownCommandPayload(
    const ReconnectTeardownCommand& command);

ReconnectTeardownWireDecodeResult decodeReconnectTeardownCommandPacket(
    const protocol::PacketEnvelope& packet);

protocol::PacketEnvelope makeReconnectTeardownRequestPacket(
    const ReconnectTeardownCommand& command,
    const ReconnectTeardownWireOptions& options = {});

protocol::PacketEnvelope makeReconnectTeardownResponsePacket(
    const protocol::PacketEnvelope& request,
    const ReconnectTeardownWireResponseOptions& options = {});

ReconnectTeardownResponse reconnectTeardownResponseFromPacket(
    const ReconnectTeardownCommand& command,
    const protocol::PacketEnvelope& packet);

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_TEARDOWN_ACK_H
