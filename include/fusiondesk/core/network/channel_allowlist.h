#ifndef FUSIONDESK_NETWORK_CHANNEL_ALLOWLIST_H
#define FUSIONDESK_NETWORK_CHANNEL_ALLOWLIST_H

#include <string>

#include "fusiondesk/core/network/channel.h"
#include "fusiondesk/core/protocol/capability_negotiation.h"

namespace fusiondesk {
namespace network {

enum class ChannelAllowlistStatus
{
    Allowed,
    ChannelTypeNotNegotiated,
    PacketTypeNotNegotiated,
    MessageKindNotNegotiated
};

struct ChannelAllowlistResult
{
    bool allowed = false;
    ChannelAllowlistStatus status = ChannelAllowlistStatus::PacketTypeNotNegotiated;
    protocol::ResponseStatus responseStatus = protocol::ResponseStatus::Unsupported;
    std::string message;

    static ChannelAllowlistResult ok()
    {
        return {true, ChannelAllowlistStatus::Allowed, protocol::ResponseStatus::Ok, {}};
    }

    static ChannelAllowlistResult denied(ChannelAllowlistStatus status,
                                         protocol::ResponseStatus responseStatus,
                                         std::string message)
    {
        return {false, status, responseStatus, message};
    }
};

class ChannelAllowlistValidator
{
public:
    explicit ChannelAllowlistValidator(protocol::NegotiatedCapabilities capabilities);

    ChannelAllowlistResult validate(const protocol::PacketEnvelope& packet) const;

private:
    bool allows(protocol::ChannelType value) const;
    bool allows(protocol::PacketType value) const;
    bool allows(protocol::MessageKind value) const;

private:
    protocol::NegotiatedCapabilities capabilities_;
};

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_CHANNEL_ALLOWLIST_H
