#include "fusiondesk/runtime/connection/module_inventory_exchange.h"

#include <cstddef>
#include <limits>
#include <utility>

#include "fusiondesk/core/protocol/protocol_validator.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

constexpr std::uint32_t ModuleInventoryPayloadMagic = 0x46444d49; // "FDMI"
constexpr std::uint16_t ModuleInventoryPayloadVersion = 1;
constexpr std::uint16_t ModuleInventoryPayloadKindRequest = 1;
constexpr std::uint16_t ModuleInventoryPayloadKindResponse = 2;

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

bool appendCount(protocol::ByteBuffer& output, std::size_t value)
{
    if (value > std::numeric_limits<std::uint16_t>::max())
        return false;
    appendU16(output, static_cast<std::uint16_t>(value));
    return true;
}

bool appendString(protocol::ByteBuffer& output, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    appendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

void appendVersion(protocol::ByteBuffer& output, const module::ModuleVersion& version)
{
    appendU16(output, version.major);
    appendU16(output, version.minor);
    appendU16(output, version.patch);
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

bool readVersion(const protocol::ByteBuffer& input,
                 std::size_t& offset,
                 module::ModuleVersion& version)
{
    return readU16(input, offset, version.major) &&
           readU16(input, offset, version.minor) &&
           readU16(input, offset, version.patch);
}

bool appendStringVector(protocol::ByteBuffer& output, const std::vector<std::string>& values)
{
    if (!appendCount(output, values.size()))
        return false;
    for (const std::string& value : values) {
        if (!appendString(output, value))
            return false;
    }
    return true;
}

bool readStringVector(const protocol::ByteBuffer& input,
                      std::size_t& offset,
                      std::vector<std::string>& values)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    values.clear();
    values.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        std::string value;
        if (!readString(input, offset, value))
            return false;
        values.push_back(std::move(value));
    }
    return true;
}

bool appendPacketTypes(protocol::ByteBuffer& output,
                       const std::vector<protocol::PacketType>& packetTypes)
{
    if (!appendCount(output, packetTypes.size()))
        return false;
    for (protocol::PacketType packetType : packetTypes)
        appendU16(output, static_cast<std::uint16_t>(packetType));
    return true;
}

bool readPacketTypes(const protocol::ByteBuffer& input,
                     std::size_t& offset,
                     std::vector<protocol::PacketType>& packetTypes)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    packetTypes.clear();
    packetTypes.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        std::uint16_t value = 0;
        if (!readU16(input, offset, value))
            return false;
        packetTypes.push_back(static_cast<protocol::PacketType>(value));
    }
    return true;
}

bool appendPeerCompatibility(protocol::ByteBuffer& output,
                             const std::vector<module::ModulePeerCompatibility>& peers)
{
    if (!appendCount(output, peers.size()))
        return false;
    for (const module::ModulePeerCompatibility& peer : peers) {
        if (!appendString(output, peer.peerModuleId))
            return false;
        appendVersion(output, peer.minPeerVersion);
        appendVersion(output, peer.maxPeerVersion);
        if (!appendString(output, peer.compatibilityMode))
            return false;
    }
    return true;
}

bool readPeerCompatibility(const protocol::ByteBuffer& input,
                           std::size_t& offset,
                           std::vector<module::ModulePeerCompatibility>& peers)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    peers.clear();
    peers.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        module::ModulePeerCompatibility peer;
        if (!readString(input, offset, peer.peerModuleId))
            return false;
        if (!readVersion(input, offset, peer.minPeerVersion))
            return false;
        if (!readVersion(input, offset, peer.maxPeerVersion))
            return false;
        if (!readString(input, offset, peer.compatibilityMode))
            return false;
        peers.push_back(std::move(peer));
    }
    return true;
}

bool appendChannels(protocol::ByteBuffer& output,
                    const std::vector<module::ChannelBinding>& channels)
{
    if (!appendCount(output, channels.size()))
        return false;
    for (const module::ChannelBinding& channel : channels) {
        if (!appendString(output, channel.name))
            return false;
        appendU16(output, channel.channelId);
        appendU16(output, static_cast<std::uint16_t>(channel.channelType));
        appendU8(output, channel.required ? 1U : 0U);
        appendU8(output, channel.shared ? 1U : 0U);
        if (!appendPacketTypes(output, channel.consumes))
            return false;
        if (!appendPacketTypes(output, channel.produces))
            return false;
    }
    return true;
}

bool readChannels(const protocol::ByteBuffer& input,
                  std::size_t& offset,
                  std::vector<module::ChannelBinding>& channels)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    channels.clear();
    channels.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        module::ChannelBinding channel;
        std::uint16_t channelType = 0;
        std::uint8_t required = 0;
        std::uint8_t shared = 0;
        if (!readString(input, offset, channel.name))
            return false;
        if (!readU16(input, offset, channel.channelId))
            return false;
        if (!readU16(input, offset, channelType))
            return false;
        if (!readU8(input, offset, required))
            return false;
        if (!readU8(input, offset, shared))
            return false;
        channel.channelType = static_cast<protocol::ChannelType>(channelType);
        channel.required = required != 0;
        channel.shared = shared != 0;
        if (!readPacketTypes(input, offset, channel.consumes))
            return false;
        if (!readPacketTypes(input, offset, channel.produces))
            return false;
        channels.push_back(std::move(channel));
    }
    return true;
}

bool appendManifest(protocol::ByteBuffer& output, const module::ModuleManifest& manifest)
{
    if (!appendString(output, manifest.moduleId))
        return false;
    appendVersion(output, manifest.version);
    appendU64(output, manifest.feature);
    appendU32(output, manifest.roleFlags);
    appendU32(output, manifest.runModeFlags);
    if (!appendStringVector(output, manifest.requiredModules))
        return false;
    if (!appendPeerCompatibility(output, manifest.compatiblePeers))
        return false;
    if (!appendChannels(output, manifest.channels))
        return false;
    return true;
}

bool readManifest(const protocol::ByteBuffer& input,
                  std::size_t& offset,
                  module::ModuleManifest& manifest)
{
    std::uint64_t feature = 0;
    if (!readString(input, offset, manifest.moduleId))
        return false;
    if (!readVersion(input, offset, manifest.version))
        return false;
    if (!readU64(input, offset, feature))
        return false;
    manifest.feature = feature;
    if (!readU32(input, offset, manifest.roleFlags))
        return false;
    if (!readU32(input, offset, manifest.runModeFlags))
        return false;
    if (!readStringVector(input, offset, manifest.requiredModules))
        return false;
    if (!readPeerCompatibility(input, offset, manifest.compatiblePeers))
        return false;
    if (!readChannels(input, offset, manifest.channels))
        return false;
    return !manifest.moduleId.empty();
}

protocol::ByteBuffer encodeInventoryPayload(const ModuleInventory& inventory,
                                            std::uint16_t kind)
{
    protocol::ByteBuffer output;
    appendU32(output, ModuleInventoryPayloadMagic);
    appendU16(output, ModuleInventoryPayloadVersion);
    appendU16(output, kind);
    appendU64(output, inventory.sessionId);
    if (!appendCount(output, inventory.manifests.size()))
        return {};
    for (const module::ModuleManifest& manifest : inventory.manifests) {
        if (!appendManifest(output, manifest))
            return {};
    }
    return output;
}

ModuleInventoryDecodeResult decodeInventoryPayload(const protocol::ByteBuffer& payload,
                                                   std::uint16_t expectedKind)
{
    ModuleInventoryDecodeResult result;
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t kind = 0;
    std::uint64_t sessionId = 0;
    std::uint16_t manifestCount = 0;

    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, version) ||
        !readU16(payload, offset, kind)) {
        result.message = "module inventory payload header is incomplete";
        return result;
    }

    if (magic != ModuleInventoryPayloadMagic) {
        result.message = "module inventory payload magic mismatch";
        return result;
    }

    if (version != ModuleInventoryPayloadVersion) {
        result.message = "module inventory payload version is unsupported";
        return result;
    }

    if (kind != expectedKind) {
        result.message = "module inventory payload kind is unexpected";
        return result;
    }

    if (!readU64(payload, offset, sessionId) ||
        !readU16(payload, offset, manifestCount)) {
        result.message = "module inventory payload body is incomplete";
        return result;
    }

    result.inventory.sessionId = sessionId;
    result.inventory.manifests.reserve(manifestCount);
    for (std::uint16_t index = 0; index < manifestCount; ++index) {
        module::ModuleManifest manifest;
        if (!readManifest(payload, offset, manifest)) {
            result.message = "module inventory manifest is invalid";
            return result;
        }
        result.inventory.manifests.push_back(std::move(manifest));
    }

    if (offset != payload.size()) {
        result.message = "module inventory payload has trailing bytes";
        return result;
    }

    result.ok = true;
    result.message = "module inventory payload decoded";
    return result;
}

bool hasFlag(protocol::PacketFlags flags, protocol::PacketFlags flag)
{
    return (flags & flag) == flag;
}

ModuleInventoryDecodeResult validateRequestEnvelope(const protocol::PacketEnvelope& packet)
{
    if (packet.packetType != protocol::PacketType::Exchange ||
        packet.messageKind != protocol::MessageKind::Request) {
        return {false, {}, "module inventory request packet route mismatch"};
    }

    const protocol::ProtocolValidationResult validation =
        protocol::ProtocolValidator().validate(packet);
    if (!validation.valid)
        return {false, {}, validation.message};

    if (packet.responseTo != 0)
        return {false, {}, "module inventory request responseTo must be zero"};

    if (!hasFlag(packet.flags, protocol::PacketFlagResponseRequired))
        return {false, {}, "module inventory request requires response-required flag"};

    if (hasFlag(packet.flags, protocol::PacketFlagNoResponseRequired))
        return {false, {}, "module inventory request cannot disable responses"};

    if (packet.channelType != protocol::ChannelType::Control)
        return {false, {}, "module inventory request must use a control channel"};

    return {true, {}, "module inventory request envelope decoded"};
}

ModuleInventoryDecodeResult validateResponseEnvelope(const protocol::PacketEnvelope& packet)
{
    if (packet.packetType != protocol::PacketType::Exchange ||
        packet.messageKind != protocol::MessageKind::Response) {
        return {false, {}, "module inventory response packet route mismatch"};
    }

    const protocol::ProtocolValidationResult validation =
        protocol::ProtocolValidator().validate(packet);
    if (!validation.valid)
        return {false, {}, validation.message};

    if (packet.responseStatus != protocol::ResponseStatus::Ok)
        return {false, {}, "module inventory response status is not Ok"};

    if (packet.channelType != protocol::ChannelType::Control)
        return {false, {}, "module inventory response must use a control channel"};

    return {true, {}, "module inventory response envelope decoded"};
}

protocol::PacketEnvelope makeErrorResponse(const protocol::PacketEnvelope& request,
                                           protocol::MessageId messageId,
                                           protocol::ResponseStatus status)
{
    protocol::PacketEnvelope response = request;
    response.messageId = messageId;
    response.correlationId = request.correlationId == 0 ? request.messageId : request.correlationId;
    response.messageKind = protocol::MessageKind::Error;
    response.responseStatus = status;
    response.responseTo = request.messageId;
    response.timeoutMs = 0;
    response.flags = protocol::PacketFlagNone;
    response.payload.clear();
    return response;
}

} // namespace

ModuleInventory makeModuleInventory(protocol::SessionId sessionId,
                                    const std::vector<module::ModuleManifest>& manifests)
{
    ModuleInventory inventory;
    inventory.sessionId = sessionId;
    inventory.manifests = manifests;
    return inventory;
}

std::vector<module::ModulePeerVersion> peerVersionsFromModuleInventory(
    const ModuleInventory& inventory)
{
    std::vector<module::ModulePeerVersion> versions;
    versions.reserve(inventory.manifests.size());
    for (const module::ModuleManifest& manifest : inventory.manifests) {
        if (manifest.moduleId.empty())
            continue;
        versions.push_back(module::ModulePeerVersion{
            manifest.moduleId,
            manifest.version,
            false,
            {}});
    }
    return versions;
}

protocol::ByteBuffer encodeModuleInventoryRequestPayload(const ModuleInventory& inventory)
{
    return encodeInventoryPayload(inventory, ModuleInventoryPayloadKindRequest);
}

protocol::ByteBuffer encodeModuleInventoryResponsePayload(const ModuleInventory& inventory)
{
    return encodeInventoryPayload(inventory, ModuleInventoryPayloadKindResponse);
}

ModuleInventoryDecodeResult decodeModuleInventoryRequestPacket(
    const protocol::PacketEnvelope& packet)
{
    const ModuleInventoryDecodeResult envelope = validateRequestEnvelope(packet);
    if (!envelope.ok)
        return envelope;

    return decodeInventoryPayload(packet.payload, ModuleInventoryPayloadKindRequest);
}

ModuleInventoryDecodeResult decodeModuleInventoryResponsePacket(
    const protocol::PacketEnvelope& packet)
{
    const ModuleInventoryDecodeResult envelope = validateResponseEnvelope(packet);
    if (!envelope.ok)
        return envelope;

    return decodeInventoryPayload(packet.payload, ModuleInventoryPayloadKindResponse);
}

protocol::PacketEnvelope makeModuleInventoryRequestPacket(
    const ModuleInventory& inventory,
    const ModuleInventoryWireOptions& options)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = options.sessionId == 0 ? inventory.sessionId : options.sessionId;
    packet.traceId = options.traceId;
    packet.messageId = options.messageId;
    packet.correlationId = options.correlationId == 0 ? options.messageId : options.correlationId;
    packet.channelId = options.controlChannel.channelId;
    packet.channelType = options.controlChannel.channelType;
    packet.packetType = protocol::PacketType::Exchange;
    packet.messageKind = protocol::MessageKind::Request;
    packet.priority = options.priority;
    packet.sequence = options.sequence;
    packet.monotonicTimestampUsec = options.monotonicTimestampUsec;
    packet.timeoutMs = options.timeoutMs;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.payload = encodeModuleInventoryRequestPayload(inventory);
    return packet;
}

protocol::PacketEnvelope makeModuleInventoryResponsePacket(
    const protocol::PacketEnvelope& request,
    const ModuleInventory& inventory,
    const ModuleInventoryWireResponseOptions& options)
{
    protocol::PacketEnvelope response = request;
    response.messageId = options.messageId == 0 ? request.messageId + 1 : options.messageId;
    response.messageKind = protocol::MessageKind::Response;
    response.responseStatus = options.status;
    response.responseTo = request.messageId;
    response.sequence = options.sequence;
    response.monotonicTimestampUsec = options.monotonicTimestampUsec;
    response.flags = protocol::PacketFlagNone;
    response.payload = encodeModuleInventoryResponsePayload(inventory);
    return response;
}

ModuleInventoryService::ModuleInventoryService(network::INetworkRouter& router)
    : router_(router)
{
}

ModuleInventoryService::~ModuleInventoryService()
{
    stop();
}

ModuleInventoryServiceStartResult ModuleInventoryService::start(
    const ModuleInventoryServiceStartOptions& options)
{
    ModuleInventoryServiceStartResult result;
    if (active_) {
        result.messages.push_back("module inventory service is already active");
        return result;
    }

    controlChannel_ = options.controlChannel;
    nextResponseMessageId_ = options.firstResponseMessageId == 0 ? 1 : options.firstResponseMessageId;
    localInventory_ = options.localInventory;

    requestToken_ = router_.subscribe(requestRoute(controlChannel_),
                                      [this](const protocol::PacketEnvelope& packet) {
                                          this->handleRequest(packet);
                                      });
    if (requestToken_ == 0) {
        result.messages.push_back("module inventory request subscription failed");
        return result;
    }

    active_ = true;
    result.ok = true;
    result.requestToken = requestToken_;
    result.messages.push_back("module inventory service started");
    return result;
}

void ModuleInventoryService::stop()
{
    if (requestToken_ != 0)
        router_.unsubscribe(requestToken_);
    requestToken_ = 0;
    active_ = false;
}

bool ModuleInventoryService::active() const
{
    return active_;
}

bool ModuleInventoryService::handleRequest(const protocol::PacketEnvelope& packet)
{
    if (!looksLikeModuleInventoryPayload(packet.payload)) {
        ++ignoredPackets_;
        return false;
    }

    const ModuleInventoryDecodeResult decoded = decodeModuleInventoryRequestPacket(packet);
    if (!decoded.ok) {
        ++failedRequests_;
        remember(decoded.message);
        if (packet.messageId != 0) {
            protocol::PacketEnvelope response =
                makeErrorResponse(packet, nextResponseMessageId(), protocol::ResponseStatus::ProtocolError);
            if (router_.send(response).status == network::SendStatus::Sent)
                ++sentResponses_;
        }
        return true;
    }

    ++handledRequests_;
    lastRemoteInventory_ = decoded.inventory;
    hasLastRemoteInventory_ = true;
    ModuleInventoryWireResponseOptions responseOptions;
    responseOptions.messageId = nextResponseMessageId();
    protocol::PacketEnvelope response =
        makeModuleInventoryResponsePacket(packet, localInventory_, responseOptions);
    const network::SendResult sent = router_.send(response);
    if (sent.status == network::SendStatus::Sent)
        ++sentResponses_;
    else
        remember(sent.message);
    return true;
}

ModuleInventoryServiceSnapshot ModuleInventoryService::snapshot() const
{
    ModuleInventoryServiceSnapshot snapshot;
    snapshot.active = active_;
    snapshot.handledRequests = handledRequests_;
    snapshot.ignoredPackets = ignoredPackets_;
    snapshot.failedRequests = failedRequests_;
    snapshot.sentResponses = sentResponses_;
    snapshot.hasLastRemoteInventory = hasLastRemoteInventory_;
    snapshot.lastRemoteSessionId = lastRemoteInventory_.sessionId;
    snapshot.lastRemoteModuleCount = lastRemoteInventory_.manifests.size();
    snapshot.messages = messages_;
    return snapshot;
}

const ModuleInventory& ModuleInventoryService::lastRemoteInventory() const
{
    return lastRemoteInventory_;
}

bool ModuleInventoryService::looksLikeModuleInventoryPayload(const protocol::ByteBuffer& payload)
{
    if (payload.size() < 4)
        return false;
    return payload[0] == 'F' && payload[1] == 'D' && payload[2] == 'M' && payload[3] == 'I';
}

network::RouteMatch ModuleInventoryService::requestRoute(network::ChannelKey controlChannel)
{
    network::RouteMatch route;
    route.channelId = controlChannel.channelId;
    route.channelType = controlChannel.channelType;
    route.packetType = protocol::PacketType::Exchange;
    route.messageKind = protocol::MessageKind::Request;
    return route;
}

protocol::MessageId ModuleInventoryService::nextResponseMessageId()
{
    return nextResponseMessageId_++;
}

void ModuleInventoryService::remember(std::string message)
{
    if (!message.empty())
        messages_.push_back(std::move(message));
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
