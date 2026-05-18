#include <cassert>
#include <vector>

#include "fusiondesk/core/protocol/capability_negotiation.h"

using namespace fusiondesk;

namespace {

protocol::ProtocolCapabilities makeLocal()
{
    protocol::ProtocolCapabilities capabilities;
    capabilities.protocolMinor = 2;
    capabilities.features.bits = protocol::feature::Display | protocol::feature::Mouse;
    capabilities.channelTypes = {protocol::ChannelType::Control, protocol::ChannelType::Video, protocol::ChannelType::Bulk};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit, protocol::PacketType::Video, protocol::PacketType::PayloadAck};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Ack,
                                 protocol::MessageKind::Error};
    capabilities.limits.maxPayloadBytes = 16U * 1024U * 1024U;
    capabilities.limits.maxStreamChunkBytes = 1024U * 1024U;
    capabilities.limits.minRequestTimeoutMs = 100;
    capabilities.limits.defaultRequestTimeoutMs = 5000;
    capabilities.limits.maxRequestTimeoutMs = 30000;
    capabilities.limits.maxChannels = 16;
    capabilities.limits.maxPendingRequests = 4096;
    capabilities.directTunnelSupported = true;
    return capabilities;
}

protocol::ProtocolCapabilities makeRemote()
{
    protocol::ProtocolCapabilities capabilities;
    capabilities.protocolMinor = 0;
    capabilities.features.bits = protocol::feature::Display | protocol::feature::Keyboard;
    capabilities.channelTypes = {protocol::ChannelType::Video, protocol::ChannelType::Control};
    capabilities.packetTypes = {protocol::PacketType::Video, protocol::PacketType::PayloadAck};
    capabilities.messageKinds = {protocol::MessageKind::Event,
                                 protocol::MessageKind::Request,
                                 protocol::MessageKind::Response};
    capabilities.limits.maxPayloadBytes = 8U * 1024U * 1024U;
    capabilities.limits.maxStreamChunkBytes = 512U * 1024U;
    capabilities.limits.minRequestTimeoutMs = 250;
    capabilities.limits.defaultRequestTimeoutMs = 3000;
    capabilities.limits.maxRequestTimeoutMs = 10000;
    capabilities.limits.maxChannels = 8;
    capabilities.limits.maxPendingRequests = 1024;
    capabilities.directTunnelSupported = false;
    return capabilities;
}

void payloadRoundTrips()
{
    protocol::CapabilityPayloadCodec codec;
    protocol::ProtocolCapabilities local = makeLocal();

    protocol::ByteBuffer payload = codec.encode(local);
    protocol::CapabilityDecodeResult decoded = codec.decode(payload);

    assert(decoded.ok());
    assert(decoded.capabilities.protocolMajor == local.protocolMajor);
    assert(decoded.capabilities.protocolMinor == local.protocolMinor);
    assert(decoded.capabilities.features.bits == local.features.bits);
    assert(decoded.capabilities.channelTypes == local.channelTypes);
    assert(decoded.capabilities.packetTypes == local.packetTypes);
    assert(decoded.capabilities.messageKinds == local.messageKinds);
    assert(decoded.capabilities.limits.maxPayloadBytes == local.limits.maxPayloadBytes);
    assert(decoded.capabilities.limits.maxStreamChunkBytes == local.limits.maxStreamChunkBytes);
    assert(decoded.capabilities.limits.minRequestTimeoutMs == local.limits.minRequestTimeoutMs);
    assert(decoded.capabilities.limits.defaultRequestTimeoutMs == local.limits.defaultRequestTimeoutMs);
    assert(decoded.capabilities.limits.maxRequestTimeoutMs == local.limits.maxRequestTimeoutMs);
    assert(decoded.capabilities.limits.maxChannels == local.limits.maxChannels);
    assert(decoded.capabilities.limits.maxPendingRequests == local.limits.maxPendingRequests);
    assert(decoded.capabilities.directTunnelSupported == local.directTunnelSupported);
}

void rejectsMalformedPayloads()
{
    protocol::CapabilityPayloadCodec codec;
    protocol::ByteBuffer payload = codec.encode(makeLocal());

    protocol::ByteBuffer badMagic = payload;
    badMagic[0] = 0;
    assert(codec.decode(badMagic).status == protocol::CapabilityDecodeStatus::InvalidMagic);

    protocol::ByteBuffer badVersion = payload;
    badVersion[5] = 2;
    assert(codec.decode(badVersion).status == protocol::CapabilityDecodeStatus::UnsupportedPayloadVersion);

    protocol::ByteBuffer truncated = payload;
    truncated.pop_back();
    assert(codec.decode(truncated).status == protocol::CapabilityDecodeStatus::Malformed);

    protocol::CapabilityPayloadOptions options;
    options.maxPayloadBytes = 16;
    protocol::CapabilityPayloadCodec limited(options);
    assert(limited.decode(payload).status == protocol::CapabilityDecodeStatus::PayloadTooLarge);

    protocol::ByteBuffer incomplete(8, 0);
    assert(codec.decode(incomplete).status == protocol::CapabilityDecodeStatus::Incomplete);
}

void negotiatesCommonCapabilities()
{
    protocol::CapabilityNegotiationResult result =
        protocol::CapabilityNegotiator::negotiate(makeLocal(), makeRemote());

    assert(result.compatible);
    assert(result.negotiated.protocolMajor == protocol::CurrentProtocolMajor);
    assert(result.negotiated.protocolMinor == 0);
    assert(result.negotiated.features.bits == protocol::feature::Display);
    assert((result.negotiated.channelTypes == std::vector<protocol::ChannelType>{protocol::ChannelType::Control,
                                                                                  protocol::ChannelType::Video}));
    assert((result.negotiated.packetTypes == std::vector<protocol::PacketType>{protocol::PacketType::Video,
                                                                               protocol::PacketType::PayloadAck}));
    assert((result.negotiated.messageKinds == std::vector<protocol::MessageKind>{protocol::MessageKind::Request,
                                                                                 protocol::MessageKind::Response,
                                                                                 protocol::MessageKind::Event}));
    assert(result.negotiated.limits.maxPayloadBytes == 8U * 1024U * 1024U);
    assert(result.negotiated.limits.maxStreamChunkBytes == 512U * 1024U);
    assert(result.negotiated.limits.minRequestTimeoutMs == 250);
    assert(result.negotiated.limits.defaultRequestTimeoutMs == 3000);
    assert(result.negotiated.limits.maxRequestTimeoutMs == 10000);
    assert(result.negotiated.limits.maxChannels == 8);
    assert(result.negotiated.limits.maxPendingRequests == 1024);
    assert(!result.negotiated.directTunnelEnabled);
    assert(!result.denials.empty());
}

void rejectsIncompatibleMajorVersion()
{
    protocol::ProtocolCapabilities remote = makeRemote();
    remote.protocolMajor = 2;

    protocol::CapabilityNegotiationResult result =
        protocol::CapabilityNegotiator::negotiate(makeLocal(), remote);

    assert(!result.compatible);
    assert(!result.denials.empty());
    assert(result.denials.front().kind == protocol::CapabilityKind::ProtocolVersion);
}

void rejectsNoCommonPacketType()
{
    protocol::ProtocolCapabilities remote = makeRemote();
    remote.packetTypes = {protocol::PacketType::Clipboard};

    protocol::CapabilityNegotiationResult result =
        protocol::CapabilityNegotiator::negotiate(makeLocal(), remote);

    assert(!result.compatible);
}

void rejectsNonOverlappingTimeoutLimits()
{
    protocol::ProtocolCapabilities remote = makeRemote();
    remote.limits.minRequestTimeoutMs = 60000;
    remote.limits.maxRequestTimeoutMs = 70000;

    protocol::CapabilityNegotiationResult result =
        protocol::CapabilityNegotiator::negotiate(makeLocal(), remote);

    assert(!result.compatible);
}

} // namespace

int main()
{
    payloadRoundTrips();
    rejectsMalformedPayloads();
    negotiatesCommonCapabilities();
    rejectsIncompatibleMajorVersion();
    rejectsNoCommonPacketType();
    rejectsNonOverlappingTimeoutLimits();
    return 0;
}
