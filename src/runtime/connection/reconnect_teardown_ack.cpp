#include "fusiondesk/runtime/connection/reconnect_teardown_ack.h"

#include <limits>
#include <set>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

constexpr std::uint32_t ReconnectTeardownPayloadMagic = 0x46445254; // "FDRT"
constexpr std::uint16_t ReconnectTeardownPayloadVersion = 1;
constexpr std::uint16_t ReconnectTeardownPayloadHeaderSize = 16;

void appendFailure(ReconnectTeardownPlanResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(ReconnectTeardownResponseSummary& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

bool sameChannel(network::ChannelKey lhs, network::ChannelKey rhs)
{
    return lhs.channelId == rhs.channelId && lhs.channelType == rhs.channelType;
}

const ReconnectTeardownCommand* findCommand(
    const ReconnectTeardownPlan& plan,
    const ReconnectTeardownResponse& response)
{
    for (const ReconnectTeardownCommand& command : plan.commands) {
        if (command.sessionId == response.sessionId &&
            sameChannel(command.targetChannel, response.targetChannel) &&
            command.messageId == response.responseTo &&
            command.correlationId == response.correlationId) {
            return &command;
        }
    }
    return nullptr;
}

bool terminalResponse(protocol::MessageKind messageKind)
{
    return messageKind == protocol::MessageKind::Response ||
           messageKind == protocol::MessageKind::Error ||
           messageKind == protocol::MessageKind::StreamEnd;
}

void appendU16(protocol::ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU32(protocol::ByteBuffer& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

bool readU16(const protocol::ByteBuffer& input, std::size_t& offset, std::uint16_t& value)
{
    if (offset + 2 > input.size())
        return false;

    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[offset]) << 8) |
                                       static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

bool readU32(const protocol::ByteBuffer& input, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4 > input.size())
        return false;

    value = (static_cast<std::uint32_t>(input[offset]) << 24) |
            (static_cast<std::uint32_t>(input[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 2]) << 8) |
            static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool knownChannelType(protocol::ChannelType channelType)
{
    switch (channelType) {
    case protocol::ChannelType::Standard:
    case protocol::ChannelType::Video:
    case protocol::ChannelType::Audio:
    case protocol::ChannelType::Bulk:
    case protocol::ChannelType::Control:
    case protocol::ChannelType::Input:
        return true;
    }
    return false;
}

network::ChannelKey reconnectTeardownDefaultControlChannel()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

bool isReconnectTeardownControlRoute(network::ChannelKey route)
{
    return route == reconnectTeardownDefaultControlChannel();
}

network::ChannelKey normalizedReconnectTeardownControlRoute(network::ChannelKey requested)
{
    if (isReconnectTeardownControlRoute(requested))
        return requested;
    return reconnectTeardownDefaultControlChannel();
}

protocol::MessageId responseCorrelationId(const protocol::PacketEnvelope& request)
{
    return request.correlationId != 0 ? request.correlationId : request.messageId;
}

protocol::MessageId responseMessageId(const protocol::PacketEnvelope& request,
                                      protocol::MessageId configuredMessageId)
{
    if (configuredMessageId != 0)
        return configuredMessageId;
    if (request.messageId != std::numeric_limits<protocol::MessageId>::max())
        return request.messageId + 1;
    return 1;
}

protocol::MessageKind responseKindForStatus(protocol::ResponseStatus status)
{
    switch (status) {
    case protocol::ResponseStatus::Ok:
        return protocol::MessageKind::Response;
    case protocol::ResponseStatus::Accepted:
        return protocol::MessageKind::Ack;
    case protocol::ResponseStatus::Progress:
        return protocol::MessageKind::Progress;
    case protocol::ResponseStatus::InvalidArgument:
    case protocol::ResponseStatus::Unauthorized:
    case protocol::ResponseStatus::DeniedByPolicy:
    case protocol::ResponseStatus::Unsupported:
    case protocol::ResponseStatus::NotFound:
    case protocol::ResponseStatus::Conflict:
    case protocol::ResponseStatus::Busy:
    case protocol::ResponseStatus::Timeout:
    case protocol::ResponseStatus::Cancelled:
    case protocol::ResponseStatus::TooLarge:
    case protocol::ResponseStatus::BackPressure:
    case protocol::ResponseStatus::ChannelUnavailable:
    case protocol::ResponseStatus::Failed:
    case protocol::ResponseStatus::InternalError:
    case protocol::ResponseStatus::ProtocolError:
        return protocol::MessageKind::Error;
    }
    return protocol::MessageKind::Error;
}

protocol::ByteBuffer stringToPayload(const std::string& message)
{
    return protocol::ByteBuffer(message.begin(), message.end());
}

std::string payloadToString(const protocol::ByteBuffer& payload)
{
    return std::string(payload.begin(), payload.end());
}

} // namespace

ReconnectTeardownPlanResult buildReconnectTeardownPlan(
    const ReconnectTeardownPlanRequest& request)
{
    ReconnectTeardownPlanResult result;

    if (request.sessionId == 0)
        appendFailure(result, "reconnect teardown plan requires session id");
    if (request.channels.empty())
        appendFailure(result, "reconnect teardown plan requires channels");
    if (request.firstMessageId == 0)
        appendFailure(result, "reconnect teardown plan requires first message id");
    if (request.timeoutMs == 0)
        appendFailure(result, "reconnect teardown plan requires timeout");
    if (!result.messages.empty())
        return result;

    std::set<network::ChannelKey> seen;
    protocol::MessageId nextMessageId = request.firstMessageId;
    const std::string reason = request.reason.empty()
        ? std::string("reconnect old transport teardown")
        : request.reason;
    for (const network::ChannelKey& channel : request.channels) {
        const auto inserted = seen.insert(channel);
        if (!inserted.second) {
            appendFailure(result, "reconnect teardown plan has duplicate channels");
            return result;
        }

        ReconnectTeardownCommand command;
        command.sessionId = request.sessionId;
        command.targetChannel = channel;
        command.messageId = nextMessageId++;
        command.correlationId = command.messageId;
        command.timeoutMs = request.timeoutMs;
        command.reason = reason;
        result.plan.commands.push_back(std::move(command));
    }

    result.ok = true;
    return result;
}

ReconnectTeardownPlanResult buildReconnectTeardownPlan(
    const ReconnectOrchestrationSidePlan& sidePlan,
    protocol::MessageId firstMessageId,
    std::uint32_t timeoutMs)
{
    ReconnectTeardownPlanRequest request;
    request.sessionId = sidePlan.sessionId;
    request.channels = sidePlan.teardownAfterSuccessfulRebind;
    request.firstMessageId = firstMessageId;
    request.timeoutMs = timeoutMs;
    request.reason = sidePlan.reason.empty()
        ? std::string("reconnect old transport teardown")
        : sidePlan.reason;
    return buildReconnectTeardownPlan(request);
}

ReconnectTeardownResponseSummary summarizeReconnectTeardownResponses(
    const ReconnectTeardownPlan& plan,
    const std::vector<ReconnectTeardownResponse>& responses)
{
    ReconnectTeardownResponseSummary result;
    if (plan.commands.empty()) {
        appendFailure(result, "reconnect teardown response summary requires commands");
        return result;
    }

    std::set<protocol::MessageId> completedMessages;
    for (const ReconnectTeardownResponse& response : responses) {
        const ReconnectTeardownCommand* command = findCommand(plan, response);
        if (command == nullptr) {
            appendFailure(result, "reconnect teardown response does not match a command");
            continue;
        }

        if (!terminalResponse(response.messageKind)) {
            appendFailure(result, "reconnect teardown response is not terminal");
            continue;
        }

        const auto inserted = completedMessages.insert(command->messageId);
        if (!inserted.second) {
            appendFailure(result, "reconnect teardown response is duplicated");
            continue;
        }

        if (response.messageKind == protocol::MessageKind::Error ||
            response.responseStatus != protocol::ResponseStatus::Ok) {
            appendFailure(result,
                          response.message.empty()
                              ? "reconnect teardown response failed"
                              : response.message);
            continue;
        }

        result.completedTargetChannels.push_back(command->targetChannel);
    }

    result.complete = completedMessages.size() == plan.commands.size();
    result.ok = result.complete && result.messages.empty();
    if (!result.complete)
        result.messages.push_back("reconnect teardown responses are incomplete");
    return result;
}

protocol::ByteBuffer encodeReconnectTeardownCommandPayload(
    const ReconnectTeardownCommand& command)
{
    protocol::ByteBuffer output;
    if (command.reason.size() > std::numeric_limits<std::uint32_t>::max())
        return output;

    output.reserve(ReconnectTeardownPayloadHeaderSize + command.reason.size());
    appendU32(output, ReconnectTeardownPayloadMagic);
    appendU16(output, ReconnectTeardownPayloadVersion);
    appendU16(output, ReconnectTeardownPayloadHeaderSize);
    appendU16(output, command.targetChannel.channelId);
    appendU16(output, static_cast<std::uint16_t>(command.targetChannel.channelType));
    appendU32(output, static_cast<std::uint32_t>(command.reason.size()));
    output.insert(output.end(), command.reason.begin(), command.reason.end());
    return output;
}

ReconnectTeardownWireDecodeResult decodeReconnectTeardownCommandPacket(
    const protocol::PacketEnvelope& packet)
{
    ReconnectTeardownWireDecodeResult result;

    if (packet.packetType != protocol::PacketType::Control) {
        result.message = "reconnect teardown request must use control packet type";
        return result;
    }
    if (packet.messageKind != protocol::MessageKind::Request) {
        result.message = "reconnect teardown request must use request message kind";
        return result;
    }
    if (!isReconnectTeardownControlRoute(network::ChannelKey{packet.channelId, packet.channelType})) {
        result.message = "reconnect teardown request must use the reconnect teardown control route";
        return result;
    }
    if ((packet.flags & protocol::PacketFlagResponseRequired) == 0) {
        result.message = "reconnect teardown request must require response";
        return result;
    }
    if (packet.sessionId == 0 || packet.messageId == 0 || packet.correlationId == 0 || packet.timeoutMs == 0) {
        result.message = "reconnect teardown request is missing correlation fields";
        return result;
    }
    if (packet.payload.size() < ReconnectTeardownPayloadHeaderSize) {
        result.message = "reconnect teardown payload is smaller than header";
        return result;
    }

    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t headerSize = 0;
    std::uint16_t targetChannelId = 0;
    std::uint16_t targetChannelType = 0;
    std::uint32_t reasonLength = 0;
    if (!readU32(packet.payload, offset, magic) ||
        !readU16(packet.payload, offset, version) ||
        !readU16(packet.payload, offset, headerSize) ||
        !readU16(packet.payload, offset, targetChannelId) ||
        !readU16(packet.payload, offset, targetChannelType) ||
        !readU32(packet.payload, offset, reasonLength)) {
        result.message = "reconnect teardown payload is truncated";
        return result;
    }

    if (magic != ReconnectTeardownPayloadMagic) {
        result.message = "reconnect teardown payload magic is invalid";
        return result;
    }
    if (version != ReconnectTeardownPayloadVersion) {
        result.message = "reconnect teardown payload version is unsupported";
        return result;
    }
    if (headerSize != ReconnectTeardownPayloadHeaderSize || headerSize > packet.payload.size()) {
        result.message = "reconnect teardown payload header size is invalid";
        return result;
    }

    offset = headerSize;
    if (offset + reasonLength != packet.payload.size()) {
        result.message = "reconnect teardown payload reason length is invalid";
        return result;
    }

    const protocol::ChannelType channelType = static_cast<protocol::ChannelType>(targetChannelType);
    if (targetChannelId == 0 || !knownChannelType(channelType)) {
        result.message = "reconnect teardown payload target channel is invalid";
        return result;
    }

    result.command.sessionId = packet.sessionId;
    result.command.targetChannel = network::ChannelKey{targetChannelId, channelType};
    result.command.messageId = packet.messageId;
    result.command.correlationId = packet.correlationId;
    result.command.timeoutMs = packet.timeoutMs;
    result.command.reason.assign(packet.payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                 packet.payload.end());
    result.ok = true;
    return result;
}

protocol::PacketEnvelope makeReconnectTeardownRequestPacket(
    const ReconnectTeardownCommand& command,
    const ReconnectTeardownWireOptions& options)
{
    const network::ChannelKey controlRoute = normalizedReconnectTeardownControlRoute(options.controlChannel);

    protocol::PacketEnvelope packet;
    packet.sessionId = command.sessionId;
    packet.traceId = options.traceId;
    packet.messageId = command.messageId;
    packet.correlationId = command.correlationId != 0 ? command.correlationId : command.messageId;
    packet.channelId = controlRoute.channelId;
    packet.channelType = controlRoute.channelType;
    packet.packetType = protocol::PacketType::Control;
    packet.messageKind = protocol::MessageKind::Request;
    packet.priority = options.priority;
    packet.sequence = options.sequence;
    packet.monotonicTimestampUsec = options.monotonicTimestampUsec;
    packet.timeoutMs = command.timeoutMs;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.payload = encodeReconnectTeardownCommandPayload(command);
    return packet;
}

protocol::PacketEnvelope makeReconnectTeardownResponsePacket(
    const protocol::PacketEnvelope& request,
    const ReconnectTeardownWireResponseOptions& options)
{
    protocol::PacketEnvelope response;
    response.protocolMajor = request.protocolMajor;
    response.protocolMinor = request.protocolMinor;
    response.sessionId = request.sessionId;
    response.traceId = request.traceId;
    response.messageId = responseMessageId(request, options.messageId);
    response.correlationId = responseCorrelationId(request);
    response.responseTo = request.messageId;
    response.channelId = request.channelId;
    response.channelType = request.channelType;
    response.packetType = protocol::PacketType::Control;
    response.messageKind = responseKindForStatus(options.status);
    response.priority = protocol::PacketPriority::Critical;
    response.responseStatus = options.status;
    response.sequence = options.sequence;
    response.monotonicTimestampUsec = options.monotonicTimestampUsec;
    response.payload = stringToPayload(options.message);
    return response;
}

ReconnectTeardownResponse reconnectTeardownResponseFromPacket(
    const ReconnectTeardownCommand& command,
    const protocol::PacketEnvelope& packet)
{
    ReconnectTeardownResponse response;
    response.sessionId = packet.sessionId;
    response.targetChannel = command.targetChannel;
    response.responseTo = packet.responseTo;
    response.correlationId = packet.correlationId;
    response.messageKind = packet.messageKind;
    response.responseStatus = packet.responseStatus;
    response.message = payloadToString(packet.payload);
    return response;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
