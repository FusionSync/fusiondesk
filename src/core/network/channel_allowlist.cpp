#include "fusiondesk/core/network/channel_allowlist.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace fusiondesk {
namespace network {

namespace {

template <typename T>
bool contains(const std::vector<T>& values, T value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

ChannelAllowlistValidator::ChannelAllowlistValidator(protocol::NegotiatedCapabilities capabilities)
    : capabilities_(std::move(capabilities))
{
}

ChannelAllowlistResult ChannelAllowlistValidator::validate(const protocol::PacketEnvelope& packet) const
{
    if (!allows(packet.channelType)) {
        return ChannelAllowlistResult::denied(ChannelAllowlistStatus::ChannelTypeNotNegotiated,
                                             protocol::ResponseStatus::Unsupported,
                                             "channel type is not negotiated");
    }

    if (!allows(packet.packetType)) {
        return ChannelAllowlistResult::denied(ChannelAllowlistStatus::PacketTypeNotNegotiated,
                                             protocol::ResponseStatus::Unsupported,
                                             "packet type is not negotiated");
    }

    if (!allows(packet.messageKind)) {
        return ChannelAllowlistResult::denied(ChannelAllowlistStatus::MessageKindNotNegotiated,
                                             protocol::ResponseStatus::Unsupported,
                                             "message kind is not negotiated");
    }

    return ChannelAllowlistResult::ok();
}

bool ChannelAllowlistValidator::allows(protocol::ChannelType value) const
{
    return contains(capabilities_.channelTypes, value);
}

bool ChannelAllowlistValidator::allows(protocol::PacketType value) const
{
    return contains(capabilities_.packetTypes, value);
}

bool ChannelAllowlistValidator::allows(protocol::MessageKind value) const
{
    return contains(capabilities_.messageKinds, value);
}

} // namespace network
} // namespace fusiondesk
