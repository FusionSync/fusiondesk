#include "fusiondesk/runtime/connection/peer_profile_service.h"

#include <cstddef>
#include <limits>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

constexpr std::uint32_t PeerProfilePayloadMagic = 0x46445050; // "FDPP"
constexpr std::uint16_t PeerProfilePayloadMinVersion = 1;
constexpr std::uint16_t PeerProfilePayloadCurrentVersion = 2;
constexpr std::uint16_t PeerProfilePayloadKindRequest = 1;
constexpr std::uint16_t PeerProfilePayloadKindResponse = 2;

void appendFailure(PeerProfileServiceStartResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendU8(protocol::ByteBuffer& output, std::uint8_t value)
{
    output.push_back(value);
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

void appendU64(protocol::ByteBuffer& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}

bool appendString(protocol::ByteBuffer& output, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    appendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool appendBytes(protocol::ByteBuffer& output, const protocol::ByteBuffer& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    appendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool appendCount(protocol::ByteBuffer& output, std::size_t value)
{
    if (value > std::numeric_limits<std::uint16_t>::max())
        return false;
    appendU16(output, static_cast<std::uint16_t>(value));
    return true;
}

bool readU8(const protocol::ByteBuffer& input, std::size_t& offset, std::uint8_t& value)
{
    if (offset + 1 > input.size())
        return false;
    value = input[offset++];
    return true;
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

bool readU64(const protocol::ByteBuffer& input, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8 > input.size())
        return false;
    value = 0;
    for (int index = 0; index < 8; ++index)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + index]);
    offset += 8;
    return true;
}

bool readString(const protocol::ByteBuffer& input, std::size_t& offset, std::string& value)
{
    std::uint32_t size = 0;
    if (!readU32(input, offset, size))
        return false;
    if (offset + size > input.size())
        return false;
    value.assign(input.begin() + static_cast<std::ptrdiff_t>(offset),
                 input.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return true;
}

bool readBytes(const protocol::ByteBuffer& input,
               std::size_t& offset,
               protocol::ByteBuffer& value)
{
    std::uint32_t size = 0;
    if (!readU32(input, offset, size))
        return false;
    if (offset + size > input.size())
        return false;
    value.assign(input.begin() + static_cast<std::ptrdiff_t>(offset),
                 input.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
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

bool knownPacketPriority(protocol::PacketPriority priority)
{
    switch (priority) {
    case protocol::PacketPriority::Critical:
    case protocol::PacketPriority::Realtime:
    case protocol::PacketPriority::Interactive:
    case protocol::PacketPriority::Normal:
    case protocol::PacketPriority::Bulk:
    case protocol::PacketPriority::Background:
        return true;
    }
    return false;
}

bool knownSocketClass(network::SocketClass socketClass)
{
    switch (socketClass) {
    case network::SocketClass::Control:
    case network::SocketClass::Realtime:
    case network::SocketClass::Bulk:
    case network::SocketClass::Auxiliary:
        return true;
    }
    return false;
}

bool knownReliabilityMode(network::ReliabilityMode reliability)
{
    switch (reliability) {
    case network::ReliabilityMode::Reliable:
    case network::ReliabilityMode::BestEffort:
        return true;
    }
    return false;
}

bool knownOrderingMode(network::OrderingMode ordering)
{
    switch (ordering) {
    case network::OrderingMode::Ordered:
    case network::OrderingMode::LatestOnly:
    case network::OrderingMode::Unordered:
        return true;
    }
    return false;
}

bool knownFlowControlMode(network::FlowControlMode flowControl)
{
    switch (flowControl) {
    case network::FlowControlMode::None:
    case network::FlowControlMode::Credit:
    case network::FlowControlMode::Pressure:
        return true;
    }
    return false;
}

bool knownQueuePolicy(network::QueuePolicy queuePolicy)
{
    switch (queuePolicy) {
    case network::QueuePolicy::Bounded:
    case network::QueuePolicy::DropNewest:
    case network::QueuePolicy::DropOldest:
    case network::QueuePolicy::KeepLatest:
        return true;
    }
    return false;
}

bool knownPacketType(protocol::PacketType packetType)
{
    switch (packetType) {
    case protocol::PacketType::ChannelInit:
    case protocol::PacketType::Heartbeat:
    case protocol::PacketType::Login:
    case protocol::PacketType::Mouse:
    case protocol::PacketType::Keyboard:
    case protocol::PacketType::Audio:
    case protocol::PacketType::Video:
    case protocol::PacketType::ClientDisconnect:
    case protocol::PacketType::ServerDisconnect:
    case protocol::PacketType::Control:
    case protocol::PacketType::PayloadAck:
    case protocol::PacketType::CursorChange:
    case protocol::PacketType::Exchange:
    case protocol::PacketType::Clipboard:
    case protocol::PacketType::Microphone:
    case protocol::PacketType::Filesystem:
    case protocol::PacketType::Printer:
    case protocol::PacketType::FilesystemControl:
    case protocol::PacketType::FilesystemIrp:
    case protocol::PacketType::UdpInit:
    case protocol::PacketType::CheckLicense:
    case protocol::PacketType::Touchscreen:
    case protocol::PacketType::Gamepad:
    case protocol::PacketType::Watermark:
        return true;
    }
    return false;
}

network::ChannelKey defaultControlChannel()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey normalizedControlChannel(network::ChannelKey requested)
{
    if (requested == defaultControlChannel())
        return requested;
    return defaultControlChannel();
}

bool appendSpec(protocol::ByteBuffer& output, const network::ChannelSpec& spec)
{
    appendU16(output, spec.key.channelId);
    appendU16(output, static_cast<std::uint16_t>(spec.key.channelType));
    if (!appendString(output, spec.name))
        return false;
    appendU8(output, static_cast<std::uint8_t>(spec.socketClass));
    appendU8(output, static_cast<std::uint8_t>(spec.defaultPriority));
    appendU8(output, static_cast<std::uint8_t>(spec.reliability));
    appendU8(output, static_cast<std::uint8_t>(spec.ordering));
    appendU8(output, static_cast<std::uint8_t>(spec.flowControl));
    appendU8(output, static_cast<std::uint8_t>(spec.queuePolicy));
    appendU8(output, spec.required ? 1U : 0U);
    if (!appendCount(output, spec.allowlist.size()))
        return false;
    for (protocol::PacketType packetType : spec.allowlist)
        appendU16(output, static_cast<std::uint16_t>(packetType));
    return appendString(output, spec.ownerModuleId);
}

bool readSpec(const protocol::ByteBuffer& input, std::size_t& offset, network::ChannelSpec& spec)
{
    std::uint16_t channelId = 0;
    std::uint16_t channelType = 0;
    std::uint8_t socketClass = 0;
    std::uint8_t priority = 0;
    std::uint8_t reliability = 0;
    std::uint8_t ordering = 0;
    std::uint8_t flowControl = 0;
    std::uint8_t queuePolicy = 0;
    std::uint8_t required = 0;
    std::uint16_t allowlistCount = 0;
    if (!readU16(input, offset, channelId) ||
        !readU16(input, offset, channelType) ||
        !readString(input, offset, spec.name) ||
        !readU8(input, offset, socketClass) ||
        !readU8(input, offset, priority) ||
        !readU8(input, offset, reliability) ||
        !readU8(input, offset, ordering) ||
        !readU8(input, offset, flowControl) ||
        !readU8(input, offset, queuePolicy) ||
        !readU8(input, offset, required) ||
        !readU16(input, offset, allowlistCount)) {
        return false;
    }

    const protocol::ChannelType type = static_cast<protocol::ChannelType>(channelType);
    const protocol::PacketPriority packetPriority =
        static_cast<protocol::PacketPriority>(priority);
    const network::SocketClass specSocketClass =
        static_cast<network::SocketClass>(socketClass);
    const network::ReliabilityMode specReliability =
        static_cast<network::ReliabilityMode>(reliability);
    const network::OrderingMode specOrdering =
        static_cast<network::OrderingMode>(ordering);
    const network::FlowControlMode specFlowControl =
        static_cast<network::FlowControlMode>(flowControl);
    const network::QueuePolicy specQueuePolicy =
        static_cast<network::QueuePolicy>(queuePolicy);
    if (channelId == 0 ||
        !knownChannelType(type) ||
        !knownPacketPriority(packetPriority) ||
        !knownSocketClass(specSocketClass) ||
        !knownReliabilityMode(specReliability) ||
        !knownOrderingMode(specOrdering) ||
        !knownFlowControlMode(specFlowControl) ||
        !knownQueuePolicy(specQueuePolicy)) {
        return false;
    }

    spec.key = network::ChannelKey{channelId, type};
    spec.socketClass = specSocketClass;
    spec.defaultPriority = packetPriority;
    spec.reliability = specReliability;
    spec.ordering = specOrdering;
    spec.flowControl = specFlowControl;
    spec.queuePolicy = specQueuePolicy;
    spec.required = required != 0;
    spec.allowlist.clear();
    for (std::uint16_t index = 0; index < allowlistCount; ++index) {
        std::uint16_t packetType = 0;
        if (!readU16(input, offset, packetType))
            return false;
        const protocol::PacketType specPacketType =
            static_cast<protocol::PacketType>(packetType);
        if (!knownPacketType(specPacketType))
            return false;
        spec.allowlist.push_back(specPacketType);
    }
    return readString(input, offset, spec.ownerModuleId);
}

bool appendConnectChannel(protocol::ByteBuffer& output,
                          const PeerProfileConnectChannel& channel)
{
    return appendSpec(output, channel.spec) &&
           appendString(output, channel.endpoint) &&
           appendString(output, channel.readyEndpoint);
}

bool appendListenChannel(protocol::ByteBuffer& output,
                         const PeerProfileListenChannel& channel)
{
    return appendSpec(output, channel.spec) &&
           appendString(output, channel.endpoint) &&
           appendString(output, channel.readyEndpoint);
}

bool readConnectChannel(const protocol::ByteBuffer& input,
                        std::size_t& offset,
                        PeerProfileConnectChannel& channel)
{
    return readSpec(input, offset, channel.spec) &&
           readString(input, offset, channel.endpoint) &&
           readString(input, offset, channel.readyEndpoint);
}

bool readListenChannel(const protocol::ByteBuffer& input,
                       std::size_t& offset,
                       PeerProfileListenChannel& channel)
{
    return readSpec(input, offset, channel.spec) &&
           readString(input, offset, channel.endpoint) &&
           readString(input, offset, channel.readyEndpoint);
}

bool appendMessages(protocol::ByteBuffer& output, const std::vector<std::string>& messages)
{
    if (!appendCount(output, messages.size()))
        return false;
    for (const std::string& message : messages) {
        if (!appendString(output, message))
            return false;
    }
    return true;
}

bool readMessages(const protocol::ByteBuffer& input,
                  std::size_t& offset,
                  std::vector<std::string>& messages)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    messages.clear();
    for (std::uint16_t index = 0; index < count; ++index) {
        std::string message;
        if (!readString(input, offset, message))
            return false;
        messages.push_back(std::move(message));
    }
    return true;
}

bool appendExtensions(protocol::ByteBuffer& output,
                      const std::vector<PeerProfileExtension>& extensions)
{
    if (!appendCount(output, extensions.size()))
        return false;
    for (const PeerProfileExtension& extension : extensions) {
        if (!appendString(output, extension.key) ||
            !appendBytes(output, extension.payload)) {
            return false;
        }
    }
    return true;
}

bool readExtensions(const protocol::ByteBuffer& input,
                    std::size_t& offset,
                    std::vector<PeerProfileExtension>& extensions)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    extensions.clear();
    for (std::uint16_t index = 0; index < count; ++index) {
        PeerProfileExtension extension;
        if (!readString(input, offset, extension.key) ||
            !readBytes(input, offset, extension.payload) ||
            extension.key.empty()) {
            return false;
        }
        extensions.push_back(std::move(extension));
    }
    return true;
}

bool appendSide(protocol::ByteBuffer& output, const PeerProfileSide& side)
{
    appendU64(output, side.sessionId);
    if (!appendCount(output, side.tcpChannels.size()))
        return false;
    for (const PeerProfileConnectChannel& channel : side.tcpChannels) {
        if (!appendConnectChannel(output, channel))
            return false;
    }
    if (!appendCount(output, side.tcpListenChannels.size()))
        return false;
    for (const PeerProfileListenChannel& channel : side.tcpListenChannels) {
        if (!appendListenChannel(output, channel))
            return false;
    }
    return true;
}

bool readSide(const protocol::ByteBuffer& input, std::size_t& offset, PeerProfileSide& side)
{
    std::uint16_t connectCount = 0;
    std::uint16_t listenCount = 0;
    if (!readU64(input, offset, side.sessionId) ||
        !readU16(input, offset, connectCount)) {
        return false;
    }
    side.tcpChannels.clear();
    for (std::uint16_t index = 0; index < connectCount; ++index) {
        PeerProfileConnectChannel channel;
        if (!readConnectChannel(input, offset, channel))
            return false;
        side.tcpChannels.push_back(std::move(channel));
    }
    if (!readU16(input, offset, listenCount))
        return false;
    side.tcpListenChannels.clear();
    for (std::uint16_t index = 0; index < listenCount; ++index) {
        PeerProfileListenChannel channel;
        if (!readListenChannel(input, offset, channel))
            return false;
        side.tcpListenChannels.push_back(std::move(channel));
    }
    return true;
}

bool appendHeader(protocol::ByteBuffer& output, std::uint16_t kind)
{
    appendU32(output, PeerProfilePayloadMagic);
    appendU16(output, PeerProfilePayloadCurrentVersion);
    appendU16(output, kind);
    return true;
}

bool readHeader(const protocol::ByteBuffer& input,
                std::size_t& offset,
                std::uint16_t expectedKind,
                std::uint16_t& version,
                std::string& message)
{
    std::uint32_t magic = 0;
    std::uint16_t kind = 0;
    if (!readU32(input, offset, magic) ||
        !readU16(input, offset, version) ||
        !readU16(input, offset, kind)) {
        message = "peer profile payload is truncated";
        return false;
    }
    if (magic != PeerProfilePayloadMagic) {
        message = "peer profile payload magic is invalid";
        return false;
    }
    if (version < PeerProfilePayloadMinVersion ||
        version > PeerProfilePayloadCurrentVersion) {
        message = "peer profile payload version is unsupported";
        return false;
    }
    if (kind != expectedKind) {
        message = "peer profile payload kind is invalid";
        return false;
    }
    return true;
}

std::string payloadToString(const protocol::ByteBuffer& payload)
{
    return std::string(payload.begin(), payload.end());
}

protocol::ByteBuffer stringToPayload(const std::string& message)
{
    return protocol::ByteBuffer(message.begin(), message.end());
}

protocol::MessageKind responseKindForStatus(protocol::ResponseStatus status)
{
    return status == protocol::ResponseStatus::Ok
        ? protocol::MessageKind::Response
        : protocol::MessageKind::Error;
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

bool canSendPeerProfileTerminalResponse(const protocol::PacketEnvelope& request)
{
    return request.sessionId != 0 &&
           request.messageId != 0 &&
           network::ChannelKey{request.channelId, request.channelType} == defaultControlChannel();
}

} // namespace

protocol::ByteBuffer encodePeerProfileExchangeRequestPayload(
    const PeerProfileExchangeRequest& request)
{
    protocol::ByteBuffer output;
    appendHeader(output, PeerProfilePayloadKindRequest);
    appendU64(output, request.clientSessionId);
    appendU64(output, request.agentSessionId);

    if (!appendCount(output, request.connectionPlan.knownSpecs.size()))
        return {};
    for (const network::ChannelSpec& spec : request.connectionPlan.knownSpecs) {
        if (!appendSpec(output, spec))
            return {};
    }

    if (!appendCount(output, request.connectionPlan.channels.size()))
        return {};
    for (const PeerConnectionChannelRequest& channel : request.connectionPlan.channels) {
        appendU16(output, channel.key.channelId);
        appendU16(output, static_cast<std::uint16_t>(channel.key.channelType));
        if (!appendString(output, channel.endpoint) ||
            !appendString(output, channel.clientReadyEndpoint) ||
            !appendString(output, channel.agentReadyEndpoint)) {
            return {};
        }
    }

    if (!appendExtensions(output, request.extensions))
        return {};

    return output;
}

PeerProfileServiceRequestDecodeResult decodePeerProfileExchangeRequestPacket(
    const protocol::PacketEnvelope& packet)
{
    PeerProfileServiceRequestDecodeResult result;
    if (packet.packetType != protocol::PacketType::Control) {
        result.message = "peer profile request must use control packet type";
        return result;
    }
    if (packet.messageKind != protocol::MessageKind::Request) {
        result.message = "peer profile request must use request message kind";
        return result;
    }
    if (!(network::ChannelKey{packet.channelId, packet.channelType} == defaultControlChannel())) {
        result.message = "peer profile request must use the control route";
        return result;
    }
    if ((packet.flags & protocol::PacketFlagResponseRequired) == 0 ||
        packet.sessionId == 0 ||
        packet.messageId == 0 ||
        packet.correlationId == 0 ||
        packet.timeoutMs == 0) {
        result.message = "peer profile request is missing correlation fields";
        return result;
    }

    std::size_t offset = 0;
    std::uint16_t version = 0;
    if (!readHeader(packet.payload,
                    offset,
                    PeerProfilePayloadKindRequest,
                    version,
                    result.message))
        return result;

    std::uint16_t specCount = 0;
    std::uint16_t channelCount = 0;
    if (!readU64(packet.payload, offset, result.request.clientSessionId) ||
        !readU64(packet.payload, offset, result.request.agentSessionId) ||
        !readU16(packet.payload, offset, specCount)) {
        result.message = "peer profile request payload is truncated";
        return result;
    }

    result.request.connectionPlan.knownSpecs.clear();
    for (std::uint16_t index = 0; index < specCount; ++index) {
        network::ChannelSpec spec;
        if (!readSpec(packet.payload, offset, spec)) {
            result.message = "peer profile request channel spec is invalid";
            return result;
        }
        result.request.connectionPlan.knownSpecs.push_back(std::move(spec));
    }

    if (!readU16(packet.payload, offset, channelCount)) {
        result.message = "peer profile request channel count is missing";
        return result;
    }
    for (std::uint16_t index = 0; index < channelCount; ++index) {
        std::uint16_t channelId = 0;
        std::uint16_t channelType = 0;
        PeerConnectionChannelRequest channel;
        if (!readU16(packet.payload, offset, channelId) ||
            !readU16(packet.payload, offset, channelType) ||
            !readString(packet.payload, offset, channel.endpoint) ||
            !readString(packet.payload, offset, channel.clientReadyEndpoint) ||
            !readString(packet.payload, offset, channel.agentReadyEndpoint)) {
            result.message = "peer profile request channel entry is invalid";
            return result;
        }
        const protocol::ChannelType type = static_cast<protocol::ChannelType>(channelType);
        if (channelId == 0 || !knownChannelType(type)) {
            result.message = "peer profile request channel key is invalid";
            return result;
        }
        channel.key = network::ChannelKey{channelId, type};
        result.request.connectionPlan.channels.push_back(std::move(channel));
    }

    if (version >= 2 &&
        !readExtensions(packet.payload, offset, result.request.extensions)) {
        result.message = "peer profile request extensions are invalid";
        return result;
    }

    if (offset != packet.payload.size()) {
        result.message = "peer profile request payload has trailing bytes";
        return result;
    }

    result.ok = true;
    return result;
}

protocol::ByteBuffer encodePeerProfileExchangeResultPayload(
    const PeerProfileExchangeResult& exchange)
{
    protocol::ByteBuffer output;
    appendHeader(output, PeerProfilePayloadKindResponse);
    appendU8(output, exchange.ok ? 1U : 0U);
    if (!appendMessages(output, exchange.messages) ||
        !appendSide(output, exchange.pair.client) ||
        !appendSide(output, exchange.pair.agent) ||
        !appendExtensions(output, exchange.extensions)) {
        return {};
    }
    return output;
}

PeerProfileServiceResponseDecodeResult decodePeerProfileExchangeResponsePacket(
    const protocol::PacketEnvelope& packet)
{
    PeerProfileServiceResponseDecodeResult result;
    if (packet.packetType != protocol::PacketType::Control) {
        result.message = "peer profile response must use control packet type";
        return result;
    }
    if (!(network::ChannelKey{packet.channelId, packet.channelType} == defaultControlChannel())) {
        result.message = "peer profile response must use the control route";
        return result;
    }
    if (packet.responseTo == 0 || packet.correlationId == 0) {
        result.message = "peer profile response is missing correlation fields";
        return result;
    }
    if (packet.messageKind == protocol::MessageKind::Error) {
        result.exchange.ok = false;
        result.message = payloadToString(packet.payload);
        if (!result.message.empty())
            result.exchange.messages.push_back(result.message);
        return result;
    }
    if (packet.messageKind != protocol::MessageKind::Response) {
        result.message = "peer profile response must be terminal";
        return result;
    }

    std::size_t offset = 0;
    std::uint16_t version = 0;
    if (!readHeader(packet.payload,
                    offset,
                    PeerProfilePayloadKindResponse,
                    version,
                    result.message))
        return result;

    std::uint8_t ok = 0;
    if (!readU8(packet.payload, offset, ok) ||
        !readMessages(packet.payload, offset, result.exchange.messages) ||
        !readSide(packet.payload, offset, result.exchange.pair.client) ||
        !readSide(packet.payload, offset, result.exchange.pair.agent)) {
        result.message = "peer profile response payload is invalid";
        return result;
    }
    if (version >= 2 &&
        !readExtensions(packet.payload, offset, result.exchange.extensions)) {
        result.message = "peer profile response extensions are invalid";
        return result;
    }
    if (offset != packet.payload.size()) {
        result.message = "peer profile response payload has trailing bytes";
        return result;
    }

    result.exchange.ok = ok != 0;
    result.ok = result.exchange.ok;
    if (!result.ok && !result.exchange.messages.empty())
        result.message = result.exchange.messages.front();
    return result;
}

protocol::PacketEnvelope makePeerProfileExchangeRequestPacket(
    const PeerProfileExchangeRequest& request,
    const PeerProfileServiceWireOptions& options)
{
    const network::ChannelKey controlRoute = normalizedControlChannel(options.controlChannel);

    protocol::PacketEnvelope packet;
    packet.sessionId = request.clientSessionId != 0
        ? request.clientSessionId
        : request.agentSessionId;
    packet.traceId = options.traceId;
    packet.messageId = options.messageId;
    packet.correlationId = options.correlationId != 0
        ? options.correlationId
        : options.messageId;
    packet.channelId = controlRoute.channelId;
    packet.channelType = controlRoute.channelType;
    packet.packetType = protocol::PacketType::Control;
    packet.messageKind = protocol::MessageKind::Request;
    packet.priority = options.priority;
    packet.sequence = options.sequence;
    packet.monotonicTimestampUsec = options.monotonicTimestampUsec;
    packet.timeoutMs = options.timeoutMs;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.payload = encodePeerProfileExchangeRequestPayload(request);
    return packet;
}

protocol::PacketEnvelope makePeerProfileExchangeResponsePacket(
    const protocol::PacketEnvelope& request,
    const PeerProfileExchangeResult& exchange,
    const PeerProfileServiceWireResponseOptions& options)
{
    const protocol::ResponseStatus status = exchange.ok
        ? options.status
        : protocol::ResponseStatus::InvalidArgument;
    const std::string message = !options.message.empty()
        ? options.message
        : (exchange.messages.empty() ? std::string{} : exchange.messages.front());

    protocol::PacketEnvelope response;
    response.protocolMajor = request.protocolMajor;
    response.protocolMinor = request.protocolMinor;
    response.sessionId = request.sessionId;
    response.traceId = request.traceId;
    response.messageId = responseMessageId(request, options.messageId);
    response.correlationId = request.correlationId != 0
        ? request.correlationId
        : request.messageId;
    response.responseTo = request.messageId;
    response.channelId = request.channelId;
    response.channelType = request.channelType;
    response.packetType = protocol::PacketType::Control;
    response.messageKind = responseKindForStatus(status);
    response.priority = protocol::PacketPriority::Critical;
    response.responseStatus = status;
    response.sequence = options.sequence;
    response.monotonicTimestampUsec = options.monotonicTimestampUsec;
    response.payload = response.messageKind == protocol::MessageKind::Response
        ? encodePeerProfileExchangeResultPayload(exchange)
        : stringToPayload(message);
    return response;
}

PeerProfileService::PeerProfileService(network::INetworkRouter& router)
    : router_(router)
{
}

PeerProfileService::~PeerProfileService()
{
    stop();
}

PeerProfileServiceStartResult PeerProfileService::start(
    const PeerProfileServiceStartOptions& options)
{
    PeerProfileServiceStartResult result;
    if (active_) {
        appendFailure(result, "peer profile service is already active");
        remember(result.messages);
        return result;
    }
    if (options.firstResponseMessageId == 0) {
        appendFailure(result, "peer profile service requires first response message id");
        remember(result.messages);
        return result;
    }

    controlChannel_ = normalizedControlChannel(options.controlChannel);
    nextResponseMessageId_ = options.firstResponseMessageId;
    exchangeHandler_ = options.exchangeHandler;
    requestToken_ = router_.subscribe(requestRoute(controlChannel_),
                                      [this](const protocol::PacketEnvelope& packet) {
                                          handleRequest(packet);
                                      });
    if (requestToken_ == 0) {
        appendFailure(result, "peer profile service request subscription failed");
        remember(result.messages);
        return result;
    }

    active_ = true;
    result.ok = true;
    result.requestToken = requestToken_;
    return result;
}

void PeerProfileService::stop()
{
    if (requestToken_ != 0)
        router_.unsubscribe(requestToken_);
    requestToken_ = 0;
    exchangeHandler_ = {};
    active_ = false;
}

bool PeerProfileService::active() const
{
    return active_;
}

bool PeerProfileService::handleRequest(const protocol::PacketEnvelope& packet)
{
    if (!active_)
        return false;

    if (!looksLikePeerProfilePayload(packet.payload)) {
        ++ignoredPackets_;
        return false;
    }

    ++handledRequests_;
    PeerProfileExchangeResult exchange;

    const PeerProfileServiceRequestDecodeResult decoded =
        decodePeerProfileExchangeRequestPacket(packet);
    if (!decoded.ok) {
        ++failedRequests_;
        exchange.ok = false;
        exchange.messages.push_back(decoded.message);
        remember(decoded.message);
        if (!canSendPeerProfileTerminalResponse(packet))
            return false;
    } else {
        exchange = resolvePeerProfileExchange(decoded.request);
        if (exchange.ok && exchangeHandler_)
            exchange = exchangeHandler_(decoded.request, exchange);
        if (!exchange.ok) {
            ++failedRequests_;
            remember(exchange.messages);
        }
    }

    PeerProfileServiceWireResponseOptions responseOptions;
    responseOptions.messageId = nextResponseMessageId();
    const protocol::PacketEnvelope response =
        makePeerProfileExchangeResponsePacket(packet, exchange, responseOptions);
    const network::SendResult sent = router_.send(response);
    if (sent.status != network::SendStatus::Sent) {
        ++failedRequests_;
        remember(sent.message.empty()
                     ? std::string("peer profile service response send failed")
                     : sent.message);
        return false;
    }

    ++sentResponses_;
    return exchange.ok;
}

PeerProfileServiceSnapshot PeerProfileService::snapshot() const
{
    PeerProfileServiceSnapshot result;
    result.active = active_;
    result.handledRequests = handledRequests_;
    result.ignoredPackets = ignoredPackets_;
    result.failedRequests = failedRequests_;
    result.sentResponses = sentResponses_;
    result.messages = messages_;
    return result;
}

bool PeerProfileService::looksLikePeerProfilePayload(const protocol::ByteBuffer& payload)
{
    if (payload.size() < 4)
        return false;

    return payload[0] == static_cast<std::uint8_t>('F') &&
           payload[1] == static_cast<std::uint8_t>('D') &&
           payload[2] == static_cast<std::uint8_t>('P') &&
           payload[3] == static_cast<std::uint8_t>('P');
}

network::RouteMatch PeerProfileService::requestRoute(network::ChannelKey controlChannel)
{
    network::RouteMatch route;
    route.channelId = controlChannel.channelId;
    route.channelType = controlChannel.channelType;
    route.packetType = protocol::PacketType::Control;
    route.messageKind = protocol::MessageKind::Request;
    return route;
}

protocol::MessageId PeerProfileService::nextResponseMessageId()
{
    if (nextResponseMessageId_ == 0)
        nextResponseMessageId_ = 1;
    return nextResponseMessageId_++;
}

void PeerProfileService::remember(const std::vector<std::string>& messages)
{
    messages_.insert(messages_.end(), messages.begin(), messages.end());
}

void PeerProfileService::remember(std::string message)
{
    if (!message.empty())
        messages_.push_back(std::move(message));
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
