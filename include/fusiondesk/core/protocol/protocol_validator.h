#ifndef FUSIONDESK_PROTOCOL_PROTOCOL_VALIDATOR_H
#define FUSIONDESK_PROTOCOL_PROTOCOL_VALIDATOR_H

#include <cstddef>
#include <string>
#include <utility>

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace protocol {

struct ProtocolValidationOptions
{
    std::uint16_t expectedMajor = CurrentProtocolMajor;
    std::uint16_t minimumMinor = 0;
    std::size_t maxPayloadBytes = 16U * 1024U * 1024U;
    bool requireSessionId = true;
    bool requireKnownChannel = true;
};

struct ProtocolValidationResult
{
    bool valid = false;
    ResponseStatus status = ResponseStatus::ProtocolError;
    std::string message;

    static ProtocolValidationResult ok()
    {
        return {true, ResponseStatus::Ok, {}};
    }

    static ProtocolValidationResult error(ResponseStatus status, std::string message)
    {
        return {false, status, std::move(message)};
    }
};

class ProtocolValidator
{
public:
    explicit ProtocolValidator(ProtocolValidationOptions options = {});

    ProtocolValidationResult validate(const PacketEnvelope& packet) const;

private:
    static bool isKnownChannelType(ChannelType value);
    static bool isKnownPacketType(PacketType value);
    static bool isKnownMessageKind(MessageKind value);
    static bool isKnownPriority(PacketPriority value);
    static bool isKnownStatus(ResponseStatus value);
    static bool isKnownChannelId(ChannelId value);
    static bool hasFlag(PacketFlags flags, PacketFlags flag);
    static bool isRequestLike(MessageKind value);
    static bool isResponseLike(MessageKind value);
    static bool isStreamMessage(MessageKind value);

private:
    ProtocolValidationOptions options_;
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_PROTOCOL_VALIDATOR_H
