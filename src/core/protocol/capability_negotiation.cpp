#include "fusiondesk/core/protocol/capability_negotiation.h"

#include <algorithm>
#include <utility>

namespace fusiondesk {
namespace protocol {

namespace {

constexpr std::size_t kMinimumPayloadSize = 52;
constexpr std::uint16_t kCapabilityFlagDirectTunnel = 0x0001;

void writeU16(ByteBuffer& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void writeU32(ByteBuffer& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void writeU64(ByteBuffer& bytes, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> ((7 - i) * 8)) & 0xffU));
}

std::uint16_t readU16(const ByteBuffer& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) |
                                      static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::uint32_t readU32(const ByteBuffer& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint64_t readU64(const ByteBuffer& bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<std::uint64_t>(bytes[offset + i]);
    return value;
}

CapabilityDecodeResult decodeFail(CapabilityDecodeStatus status, std::string message)
{
    CapabilityDecodeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

template <typename Enum>
void writeEnumList(ByteBuffer& bytes, const std::vector<Enum>& values)
{
    writeU16(bytes, static_cast<std::uint16_t>(values.size()));
    for (Enum value : values)
        writeU16(bytes, static_cast<std::uint16_t>(value));
}

template <typename Enum>
bool readEnumList(const ByteBuffer& bytes,
                  std::size_t maxListItems,
                  std::size_t& offset,
                  std::vector<Enum>& values,
                  std::string& error)
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint16_t)) {
        error = "missing capability list length";
        return false;
    }

    const std::uint16_t count = readU16(bytes, offset);
    offset += sizeof(std::uint16_t);
    if (count > maxListItems) {
        error = "capability list exceeds decoder limit";
        return false;
    }

    if ((bytes.size() - offset) / sizeof(std::uint16_t) < count) {
        error = "capability list is truncated";
        return false;
    }

    values.clear();
    values.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        values.push_back(static_cast<Enum>(readU16(bytes, offset)));
        offset += sizeof(std::uint16_t);
    }
    return true;
}

template <typename T>
bool contains(const std::vector<T>& values, T value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
std::vector<T> intersectValues(const std::vector<T>& local, const std::vector<T>& remote)
{
    std::vector<T> result;
    for (T value : local) {
        if (!contains(result, value) && contains(remote, value))
            result.push_back(value);
    }
    return result;
}

void addDenial(std::vector<CapabilityDenial>& denials,
               CapabilityKind kind,
               std::uint64_t value,
               CapabilityDenialReason reason,
               std::string message)
{
    CapabilityDenial denial;
    denial.kind = kind;
    denial.value = value;
    denial.reason = reason;
    denial.message = std::move(message);
    denials.push_back(std::move(denial));
}

template <typename T>
void addUnsupportedValues(std::vector<CapabilityDenial>& denials,
                          CapabilityKind kind,
                          const std::vector<T>& offered,
                          const std::vector<T>& supported,
                          CapabilityDenialReason reason,
                          const char* message)
{
    for (T value : offered) {
        if (!contains(supported, value))
            addDenial(denials, kind, static_cast<std::uint64_t>(value), reason, message);
    }
}

ProtocolLimits negotiateLimits(const ProtocolLimits& local, const ProtocolLimits& remote)
{
    ProtocolLimits limits;
    limits.maxPayloadBytes = std::min(local.maxPayloadBytes, remote.maxPayloadBytes);
    limits.maxStreamChunkBytes = std::min(local.maxStreamChunkBytes, remote.maxStreamChunkBytes);
    limits.minRequestTimeoutMs = std::max(local.minRequestTimeoutMs, remote.minRequestTimeoutMs);
    limits.maxRequestTimeoutMs = std::min(local.maxRequestTimeoutMs, remote.maxRequestTimeoutMs);
    limits.maxChannels = std::min(local.maxChannels, remote.maxChannels);
    limits.maxPendingRequests = std::min(local.maxPendingRequests, remote.maxPendingRequests);

    const std::uint32_t preferredDefault = std::min(local.defaultRequestTimeoutMs, remote.defaultRequestTimeoutMs);
    if (limits.minRequestTimeoutMs <= limits.maxRequestTimeoutMs) {
        limits.defaultRequestTimeoutMs = std::min(std::max(preferredDefault, limits.minRequestTimeoutMs),
                                                  limits.maxRequestTimeoutMs);
    } else {
        limits.defaultRequestTimeoutMs = preferredDefault;
    }
    return limits;
}

bool validateLimits(const ProtocolLimits& limits, std::vector<CapabilityDenial>& denials)
{
    bool valid = true;
    if (limits.maxPayloadBytes == 0) {
        addDenial(denials, CapabilityKind::Limit, 0, CapabilityDenialReason::LimitConflict,
                  "negotiated max payload size is zero");
        valid = false;
    }

    if (limits.maxStreamChunkBytes == 0) {
        addDenial(denials, CapabilityKind::Limit, 0, CapabilityDenialReason::LimitConflict,
                  "negotiated max stream chunk size is zero");
        valid = false;
    }

    if (limits.minRequestTimeoutMs > limits.maxRequestTimeoutMs) {
        addDenial(denials, CapabilityKind::Limit, 0, CapabilityDenialReason::LimitConflict,
                  "request timeout ranges do not overlap");
        valid = false;
    }

    if (limits.maxChannels == 0) {
        addDenial(denials, CapabilityKind::Limit, 0, CapabilityDenialReason::LimitConflict,
                  "negotiated max channel count is zero");
        valid = false;
    }

    if (limits.maxPendingRequests == 0) {
        addDenial(denials, CapabilityKind::Limit, 0, CapabilityDenialReason::LimitConflict,
                  "negotiated max pending request count is zero");
        valid = false;
    }

    return valid;
}

} // namespace

CapabilityPayloadCodec::CapabilityPayloadCodec(CapabilityPayloadOptions options)
    : options_(options)
{
}

ByteBuffer CapabilityPayloadCodec::encode(const ProtocolCapabilities& capabilities) const
{
    ByteBuffer bytes;
    bytes.reserve(kMinimumPayloadSize + capabilities.channelTypes.size() * 2 +
                  capabilities.packetTypes.size() * 2 + capabilities.messageKinds.size() * 2);

    writeU32(bytes, CapabilityPayloadMagic);
    writeU16(bytes, CapabilityPayloadVersion);
    writeU16(bytes, capabilities.protocolMajor);
    writeU16(bytes, capabilities.protocolMinor);
    writeU16(bytes, capabilities.directTunnelSupported ? kCapabilityFlagDirectTunnel : 0);
    writeU64(bytes, capabilities.features.bits);
    writeU32(bytes, capabilities.limits.maxPayloadBytes);
    writeU32(bytes, capabilities.limits.maxStreamChunkBytes);
    writeU32(bytes, capabilities.limits.minRequestTimeoutMs);
    writeU32(bytes, capabilities.limits.defaultRequestTimeoutMs);
    writeU32(bytes, capabilities.limits.maxRequestTimeoutMs);
    writeU16(bytes, capabilities.limits.maxChannels);
    writeU32(bytes, capabilities.limits.maxPendingRequests);
    writeEnumList(bytes, capabilities.channelTypes);
    writeEnumList(bytes, capabilities.packetTypes);
    writeEnumList(bytes, capabilities.messageKinds);
    return bytes;
}

CapabilityDecodeResult CapabilityPayloadCodec::decode(const ByteBuffer& payload) const
{
    if (payload.size() > options_.maxPayloadBytes)
        return decodeFail(CapabilityDecodeStatus::PayloadTooLarge, "capability payload exceeds decoder limit");

    if (payload.size() < kMinimumPayloadSize)
        return decodeFail(CapabilityDecodeStatus::Incomplete, "capability payload is shorter than fixed header");

    std::size_t offset = 0;
    if (readU32(payload, offset) != CapabilityPayloadMagic)
        return decodeFail(CapabilityDecodeStatus::InvalidMagic, "capability payload magic mismatch");
    offset += sizeof(std::uint32_t);

    const std::uint16_t payloadVersion = readU16(payload, offset);
    offset += sizeof(std::uint16_t);
    if (payloadVersion != CapabilityPayloadVersion)
        return decodeFail(CapabilityDecodeStatus::UnsupportedPayloadVersion,
                          "unsupported capability payload version");

    ProtocolCapabilities capabilities;
    capabilities.protocolMajor = readU16(payload, offset);
    offset += sizeof(std::uint16_t);
    capabilities.protocolMinor = readU16(payload, offset);
    offset += sizeof(std::uint16_t);

    const std::uint16_t flags = readU16(payload, offset);
    offset += sizeof(std::uint16_t);
    capabilities.directTunnelSupported = (flags & kCapabilityFlagDirectTunnel) == kCapabilityFlagDirectTunnel;

    capabilities.features.bits = readU64(payload, offset);
    offset += sizeof(std::uint64_t);
    capabilities.limits.maxPayloadBytes = readU32(payload, offset);
    offset += sizeof(std::uint32_t);
    capabilities.limits.maxStreamChunkBytes = readU32(payload, offset);
    offset += sizeof(std::uint32_t);
    capabilities.limits.minRequestTimeoutMs = readU32(payload, offset);
    offset += sizeof(std::uint32_t);
    capabilities.limits.defaultRequestTimeoutMs = readU32(payload, offset);
    offset += sizeof(std::uint32_t);
    capabilities.limits.maxRequestTimeoutMs = readU32(payload, offset);
    offset += sizeof(std::uint32_t);
    capabilities.limits.maxChannels = readU16(payload, offset);
    offset += sizeof(std::uint16_t);
    capabilities.limits.maxPendingRequests = readU32(payload, offset);
    offset += sizeof(std::uint32_t);

    std::string error;
    if (!readEnumList(payload, options_.maxListItems, offset, capabilities.channelTypes, error))
        return decodeFail(CapabilityDecodeStatus::Malformed, error);

    if (!readEnumList(payload, options_.maxListItems, offset, capabilities.packetTypes, error))
        return decodeFail(CapabilityDecodeStatus::Malformed, error);

    if (!readEnumList(payload, options_.maxListItems, offset, capabilities.messageKinds, error))
        return decodeFail(CapabilityDecodeStatus::Malformed, error);

    if (offset != payload.size())
        return decodeFail(CapabilityDecodeStatus::Malformed, "capability payload has trailing bytes");

    CapabilityDecodeResult result;
    result.status = CapabilityDecodeStatus::Ok;
    result.capabilities = std::move(capabilities);
    return result;
}

CapabilityNegotiationResult CapabilityNegotiator::negotiate(const ProtocolCapabilities& local,
                                                            const ProtocolCapabilities& remote)
{
    CapabilityNegotiationResult result;
    result.negotiated.protocolMajor = local.protocolMajor;
    result.negotiated.protocolMinor = std::min(local.protocolMinor, remote.protocolMinor);

    if (local.protocolMajor != remote.protocolMajor) {
        addDenial(result.denials, CapabilityKind::ProtocolVersion, remote.protocolMajor,
                  CapabilityDenialReason::IncompatibleProtocol, "protocol major versions do not match");
        result.message = "capability negotiation failed";
        return result;
    }

    result.negotiated.features.bits = local.features.bits & remote.features.bits;
    const FeatureMask localOnlyFeatures = local.features.bits & ~remote.features.bits;
    const FeatureMask remoteOnlyFeatures = remote.features.bits & ~local.features.bits;
    if (localOnlyFeatures != 0) {
        addDenial(result.denials, CapabilityKind::Feature, localOnlyFeatures,
                  CapabilityDenialReason::UnsupportedByRemote, "local features are not supported by remote");
    }
    if (remoteOnlyFeatures != 0) {
        addDenial(result.denials, CapabilityKind::Feature, remoteOnlyFeatures,
                  CapabilityDenialReason::UnsupportedByLocal, "remote features are not supported by local");
    }

    result.negotiated.channelTypes = intersectValues(local.channelTypes, remote.channelTypes);
    result.negotiated.packetTypes = intersectValues(local.packetTypes, remote.packetTypes);
    result.negotiated.messageKinds = intersectValues(local.messageKinds, remote.messageKinds);

    addUnsupportedValues(result.denials, CapabilityKind::ChannelType, local.channelTypes, remote.channelTypes,
                         CapabilityDenialReason::UnsupportedByRemote, "local channel type is not supported by remote");
    addUnsupportedValues(result.denials, CapabilityKind::ChannelType, remote.channelTypes, local.channelTypes,
                         CapabilityDenialReason::UnsupportedByLocal, "remote channel type is not supported by local");
    addUnsupportedValues(result.denials, CapabilityKind::PacketType, local.packetTypes, remote.packetTypes,
                         CapabilityDenialReason::UnsupportedByRemote, "local packet type is not supported by remote");
    addUnsupportedValues(result.denials, CapabilityKind::PacketType, remote.packetTypes, local.packetTypes,
                         CapabilityDenialReason::UnsupportedByLocal, "remote packet type is not supported by local");
    addUnsupportedValues(result.denials, CapabilityKind::MessageKind, local.messageKinds, remote.messageKinds,
                         CapabilityDenialReason::UnsupportedByRemote, "local message kind is not supported by remote");
    addUnsupportedValues(result.denials, CapabilityKind::MessageKind, remote.messageKinds, local.messageKinds,
                         CapabilityDenialReason::UnsupportedByLocal, "remote message kind is not supported by local");

    bool compatible = true;
    if (result.negotiated.channelTypes.empty()) {
        addDenial(result.denials, CapabilityKind::ChannelType, 0, CapabilityDenialReason::NoCommonValue,
                  "no common channel type");
        compatible = false;
    }
    if (result.negotiated.packetTypes.empty()) {
        addDenial(result.denials, CapabilityKind::PacketType, 0, CapabilityDenialReason::NoCommonValue,
                  "no common packet type");
        compatible = false;
    }
    if (result.negotiated.messageKinds.empty()) {
        addDenial(result.denials, CapabilityKind::MessageKind, 0, CapabilityDenialReason::NoCommonValue,
                  "no common message kind");
        compatible = false;
    }

    result.negotiated.limits = negotiateLimits(local.limits, remote.limits);
    compatible = validateLimits(result.negotiated.limits, result.denials) && compatible;

    result.negotiated.directTunnelEnabled = local.directTunnelSupported && remote.directTunnelSupported;
    if (local.directTunnelSupported && !remote.directTunnelSupported) {
        addDenial(result.denials, CapabilityKind::DirectTunnel, 1, CapabilityDenialReason::UnsupportedByRemote,
                  "direct tunnel is not supported by remote");
    }
    if (!local.directTunnelSupported && remote.directTunnelSupported) {
        addDenial(result.denials, CapabilityKind::DirectTunnel, 1, CapabilityDenialReason::UnsupportedByLocal,
                  "direct tunnel is not supported by local");
    }

    result.compatible = compatible;
    result.message = compatible ? "capabilities negotiated" : "capability negotiation failed";
    return result;
}

} // namespace protocol
} // namespace fusiondesk
