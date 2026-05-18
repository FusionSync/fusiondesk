#ifndef FUSIONDESK_PROTOCOL_PROTOCOL_TYPES_H
#define FUSIONDESK_PROTOCOL_PROTOCOL_TYPES_H

#include <cstdint>
#include <vector>

namespace fusiondesk {
namespace protocol {

using ByteBuffer = std::vector<std::uint8_t>;
using ChannelId = std::uint16_t;
using SessionId = std::uint64_t;
using TraceId = std::uint64_t;
using MessageId = std::uint64_t;
using PacketFlags = std::uint32_t;

constexpr std::uint16_t CurrentProtocolMajor = 1;
constexpr std::uint16_t CurrentProtocolMinor = 0;
constexpr PacketFlags PacketFlagNone = 0x00000000;
constexpr PacketFlags PacketFlagResponseRequired = 0x00000001;
constexpr PacketFlags PacketFlagNoResponseRequired = 0x00000002;
constexpr PacketFlags PacketFlagCoalescable = 0x00000004;
constexpr PacketFlags PacketFlagEndOfStream = 0x00000008;
constexpr PacketFlags PacketFlagKeyFrame = 0x00000010;

enum class ChannelType : std::uint16_t
{
    Standard = 0,
    Video = 1,
    Audio = 2,
    Bulk = 3,
    Control = 4,
    Input = 5
};

enum class MessageKind : std::uint16_t
{
    Event = 0,
    Request = 1,
    Response = 2,
    Ack = 3,
    Error = 4,
    Progress = 5,
    StreamStart = 6,
    StreamChunk = 7,
    StreamEnd = 8,
    Cancel = 9
};

enum class PacketPriority : std::uint8_t
{
    Critical = 0,
    Realtime = 1,
    Interactive = 2,
    Normal = 3,
    Bulk = 4,
    Background = 5
};

enum class ResponseStatus : std::uint16_t
{
    Ok = 0,
    Accepted = 1,
    Progress = 2,
    InvalidArgument = 100,
    Unauthorized = 101,
    DeniedByPolicy = 102,
    Unsupported = 103,
    NotFound = 104,
    Conflict = 105,
    Busy = 106,
    Timeout = 107,
    Cancelled = 108,
    TooLarge = 109,
    BackPressure = 110,
    ChannelUnavailable = 111,
    Failed = 200,
    InternalError = 201,
    ProtocolError = 202
};

enum class ChannelIdValue : ChannelId
{
    UserAuthMain = 0x0001,
    SmallData = 0x1001,
    DesktopAudio = 0x1002,
    DesktopScreen = 0x1003,
    LargeData = 0x1004,
    Microphone = 0x1005,
    Camera = 0x1006,
    Filesystem = 0x1007,
    Printer = 0x1008,
    DesktopSecondScreen = 0x1009,
    Gamepad = 0x100a
};

enum class PacketType : std::uint16_t
{
    ChannelInit = 0x0001,
    Heartbeat = 0x0002,
    Login = 0x0010,
    Mouse = 0x0011,
    Keyboard = 0x0012,
    Audio = 0x0013,
    Video = 0x0014,
    ClientDisconnect = 0x0015,
    ServerDisconnect = 0x0016,
    Control = 0x0017,
    PayloadAck = 0x0018,
    CursorChange = 0x0019,
    Exchange = 0x001a,
    Clipboard = 0x001b,
    Microphone = 0x001c,
    Filesystem = 0x001d,
    Printer = 0x001e,
    FilesystemControl = 0x001f,
    FilesystemIrp = 0x0020,
    UdpInit = 0x0021,
    CheckLicense = 0x0022,
    Touchscreen = 0x0023,
    Gamepad = 0x0024,
    Watermark = 0x0025
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_PROTOCOL_TYPES_H
