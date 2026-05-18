#ifndef FUSIONDESK_PROTOCOL_CAPABILITY_NEGOTIATION_H
#define FUSIONDESK_PROTOCOL_CAPABILITY_NEGOTIATION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/feature_flags.h"
#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace protocol {

constexpr std::uint32_t CapabilityPayloadMagic = 0x46443243U; // "FD2C"
constexpr std::uint16_t CapabilityPayloadVersion = 1;

struct ProtocolLimits
{
    std::uint32_t maxPayloadBytes = 16U * 1024U * 1024U;
    std::uint32_t maxStreamChunkBytes = 1024U * 1024U;
    std::uint32_t minRequestTimeoutMs = 100;
    std::uint32_t defaultRequestTimeoutMs = 5000;
    std::uint32_t maxRequestTimeoutMs = 30000;
    std::uint16_t maxChannels = 16;
    std::uint32_t maxPendingRequests = 4096;
};

struct ProtocolCapabilities
{
    std::uint16_t protocolMajor = CurrentProtocolMajor;
    std::uint16_t protocolMinor = CurrentProtocolMinor;
    FeatureSet features;
    std::vector<ChannelType> channelTypes;
    std::vector<PacketType> packetTypes;
    std::vector<MessageKind> messageKinds;
    ProtocolLimits limits;
    bool directTunnelSupported = false;
};

enum class CapabilityDecodeStatus
{
    Ok,
    Incomplete,
    InvalidMagic,
    UnsupportedPayloadVersion,
    PayloadTooLarge,
    Malformed
};

struct CapabilityDecodeResult
{
    CapabilityDecodeStatus status = CapabilityDecodeStatus::Malformed;
    ProtocolCapabilities capabilities;
    std::string message;

    bool ok() const
    {
        return status == CapabilityDecodeStatus::Ok;
    }
};

struct CapabilityPayloadOptions
{
    std::size_t maxPayloadBytes = 64U * 1024U;
    std::size_t maxListItems = 256;
};

class CapabilityPayloadCodec
{
public:
    explicit CapabilityPayloadCodec(CapabilityPayloadOptions options = {});

    ByteBuffer encode(const ProtocolCapabilities& capabilities) const;
    CapabilityDecodeResult decode(const ByteBuffer& payload) const;

private:
    CapabilityPayloadOptions options_;
};

enum class CapabilityKind
{
    ProtocolVersion,
    Feature,
    ChannelType,
    PacketType,
    MessageKind,
    Limit,
    DirectTunnel
};

enum class CapabilityDenialReason
{
    UnsupportedByLocal,
    UnsupportedByRemote,
    IncompatibleProtocol,
    NoCommonValue,
    LimitConflict
};

struct CapabilityDenial
{
    CapabilityKind kind = CapabilityKind::Feature;
    std::uint64_t value = 0;
    CapabilityDenialReason reason = CapabilityDenialReason::UnsupportedByRemote;
    std::string message;
};

struct NegotiatedCapabilities
{
    std::uint16_t protocolMajor = CurrentProtocolMajor;
    std::uint16_t protocolMinor = CurrentProtocolMinor;
    FeatureSet features;
    std::vector<ChannelType> channelTypes;
    std::vector<PacketType> packetTypes;
    std::vector<MessageKind> messageKinds;
    ProtocolLimits limits;
    bool directTunnelEnabled = false;
};

struct CapabilityNegotiationResult
{
    bool compatible = false;
    NegotiatedCapabilities negotiated;
    std::vector<CapabilityDenial> denials;
    std::string message;
};

class CapabilityNegotiator
{
public:
    static CapabilityNegotiationResult negotiate(const ProtocolCapabilities& local,
                                                 const ProtocolCapabilities& remote);
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_CAPABILITY_NEGOTIATION_H
