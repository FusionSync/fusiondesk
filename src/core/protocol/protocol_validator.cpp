#include "fusiondesk/core/protocol/protocol_validator.h"

#include <utility>

namespace fusiondesk {
namespace protocol {

ProtocolValidator::ProtocolValidator(ProtocolValidationOptions options)
    : options_(options)
{
}

ProtocolValidationResult ProtocolValidator::validate(const PacketEnvelope& packet) const
{
    if (packet.protocolMajor != options_.expectedMajor)
        return ProtocolValidationResult::error(ResponseStatus::Unsupported, "unsupported protocol major version");

    if (packet.protocolMinor < options_.minimumMinor)
        return ProtocolValidationResult::error(ResponseStatus::Unsupported, "unsupported protocol minor version");

    if (options_.requireSessionId && packet.sessionId == 0)
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "session id is required");

    if (options_.requireKnownChannel && !isKnownChannelId(packet.channelId))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "unknown channel id");

    if (!isKnownChannelType(packet.channelType))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "unknown channel type");

    if (!isKnownPacketType(packet.packetType))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "unknown packet type");

    if (!isKnownMessageKind(packet.messageKind))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "unknown message kind");

    if (!isKnownPriority(packet.priority))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "unknown packet priority");

    if (!isKnownStatus(packet.responseStatus))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "unknown response status");

    if (packet.payload.size() > options_.maxPayloadBytes)
        return ProtocolValidationResult::error(ResponseStatus::TooLarge, "payload exceeds negotiated limit");

    if (isRequestLike(packet.messageKind)) {
        if (packet.messageId == 0)
            return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "request message id is required");

        if (packet.correlationId == 0)
            return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "request correlation id is required");

        if (packet.timeoutMs == 0)
            return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "request timeout is required");
    }

    if (packet.messageKind == MessageKind::Event && hasFlag(packet.flags, PacketFlagResponseRequired)) {
        if (packet.messageId == 0 || packet.correlationId == 0 || packet.timeoutMs == 0)
            return ProtocolValidationResult::error(ResponseStatus::InvalidArgument,
                                                   "response-required event needs message id, correlation id, and timeout");
    }

    if (isResponseLike(packet.messageKind) && packet.responseTo == 0)
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "responseTo is required");

    if (isStreamMessage(packet.messageKind) && packet.correlationId == 0)
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument, "stream correlation id is required");

    if (hasFlag(packet.flags, PacketFlagResponseRequired) && hasFlag(packet.flags, PacketFlagNoResponseRequired))
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument,
                                               "response flags are mutually exclusive");

    if (packet.messageKind == MessageKind::Event && !hasFlag(packet.flags, PacketFlagNoResponseRequired) &&
        packet.responseStatus != ResponseStatus::Ok) {
        return ProtocolValidationResult::error(ResponseStatus::InvalidArgument,
                                               "event status must be Ok unless it is a response");
    }

    return ProtocolValidationResult::ok();
}

bool ProtocolValidator::isKnownChannelType(ChannelType value)
{
    switch (value) {
    case ChannelType::Standard:
    case ChannelType::Video:
    case ChannelType::Audio:
    case ChannelType::Bulk:
    case ChannelType::Control:
    case ChannelType::Input:
        return true;
    }
    return false;
}

bool ProtocolValidator::isKnownPacketType(PacketType value)
{
    switch (value) {
    case PacketType::ChannelInit:
    case PacketType::Heartbeat:
    case PacketType::Login:
    case PacketType::Mouse:
    case PacketType::Keyboard:
    case PacketType::Audio:
    case PacketType::Video:
    case PacketType::ClientDisconnect:
    case PacketType::ServerDisconnect:
    case PacketType::Control:
    case PacketType::PayloadAck:
    case PacketType::CursorChange:
    case PacketType::Exchange:
    case PacketType::Clipboard:
    case PacketType::Microphone:
    case PacketType::Filesystem:
    case PacketType::Printer:
    case PacketType::FilesystemControl:
    case PacketType::FilesystemIrp:
    case PacketType::UdpInit:
    case PacketType::CheckLicense:
    case PacketType::Touchscreen:
    case PacketType::Gamepad:
    case PacketType::Watermark:
        return true;
    }
    return false;
}

bool ProtocolValidator::isKnownMessageKind(MessageKind value)
{
    switch (value) {
    case MessageKind::Event:
    case MessageKind::Request:
    case MessageKind::Response:
    case MessageKind::Ack:
    case MessageKind::Error:
    case MessageKind::Progress:
    case MessageKind::StreamStart:
    case MessageKind::StreamChunk:
    case MessageKind::StreamEnd:
    case MessageKind::Cancel:
        return true;
    }
    return false;
}

bool ProtocolValidator::isKnownPriority(PacketPriority value)
{
    switch (value) {
    case PacketPriority::Critical:
    case PacketPriority::Realtime:
    case PacketPriority::Interactive:
    case PacketPriority::Normal:
    case PacketPriority::Bulk:
    case PacketPriority::Background:
        return true;
    }
    return false;
}

bool ProtocolValidator::isKnownStatus(ResponseStatus value)
{
    switch (value) {
    case ResponseStatus::Ok:
    case ResponseStatus::Accepted:
    case ResponseStatus::Progress:
    case ResponseStatus::InvalidArgument:
    case ResponseStatus::Unauthorized:
    case ResponseStatus::DeniedByPolicy:
    case ResponseStatus::Unsupported:
    case ResponseStatus::NotFound:
    case ResponseStatus::Conflict:
    case ResponseStatus::Busy:
    case ResponseStatus::Timeout:
    case ResponseStatus::Cancelled:
    case ResponseStatus::TooLarge:
    case ResponseStatus::BackPressure:
    case ResponseStatus::ChannelUnavailable:
    case ResponseStatus::Failed:
    case ResponseStatus::InternalError:
    case ResponseStatus::ProtocolError:
        return true;
    }
    return false;
}

bool ProtocolValidator::isKnownChannelId(ChannelId value)
{
    switch (static_cast<ChannelIdValue>(value)) {
    case ChannelIdValue::UserAuthMain:
    case ChannelIdValue::SmallData:
    case ChannelIdValue::DesktopAudio:
    case ChannelIdValue::DesktopScreen:
    case ChannelIdValue::LargeData:
    case ChannelIdValue::Microphone:
    case ChannelIdValue::Camera:
    case ChannelIdValue::Filesystem:
    case ChannelIdValue::Printer:
    case ChannelIdValue::DesktopSecondScreen:
    case ChannelIdValue::Gamepad:
        return true;
    }
    return false;
}

bool ProtocolValidator::hasFlag(PacketFlags flags, PacketFlags flag)
{
    return (flags & flag) == flag;
}

bool ProtocolValidator::isRequestLike(MessageKind value)
{
    return value == MessageKind::Request || value == MessageKind::Cancel;
}

bool ProtocolValidator::isResponseLike(MessageKind value)
{
    return value == MessageKind::Response ||
           value == MessageKind::Ack ||
           value == MessageKind::Error ||
           value == MessageKind::Progress;
}

bool ProtocolValidator::isStreamMessage(MessageKind value)
{
    return value == MessageKind::StreamStart ||
           value == MessageKind::StreamChunk ||
           value == MessageKind::StreamEnd;
}

} // namespace protocol
} // namespace fusiondesk

