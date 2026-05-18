#ifndef FUSIONDESK_PROTOCOL_PACKET_CODEC_H
#define FUSIONDESK_PROTOCOL_PACKET_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "fusiondesk/core/protocol/protocol_validator.h"

namespace fusiondesk {
namespace protocol {

constexpr std::uint32_t PacketWireMagic = 0x46443250U; // "FD2P"
constexpr std::uint16_t PacketWireHeaderLength = 100;

struct PacketCodecOptions
{
    std::size_t maxPayloadBytes = 16U * 1024U * 1024U;
    bool validatePayloadCrc = true;
    bool validateHeaderCrc = true;
};

enum class PacketDecodeStatus
{
    Ok,
    Incomplete,
    InvalidMagic,
    InvalidHeaderLength,
    UnsupportedVersion,
    PayloadTooLarge,
    LengthMismatch,
    HeaderChecksumMismatch,
    PayloadChecksumMismatch,
    ValidationFailed
};

struct PacketDecodeResult
{
    PacketDecodeStatus status = PacketDecodeStatus::ValidationFailed;
    PacketEnvelope packet;
    std::string message;

    bool ok() const
    {
        return status == PacketDecodeStatus::Ok;
    }
};

struct PacketFrameInspection
{
    PacketDecodeStatus status = PacketDecodeStatus::Incomplete;
    std::size_t frameSize = 0;
    bool complete = false;
    std::string message;

    bool ok() const
    {
        return status == PacketDecodeStatus::Ok;
    }
};

class PacketCodec
{
public:
    explicit PacketCodec(PacketCodecOptions options = {});

    ByteBuffer encode(const PacketEnvelope& packet) const;
    PacketFrameInspection inspectFrame(const ByteBuffer& bytes) const;
    PacketDecodeResult decode(const ByteBuffer& bytes) const;

    static std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

private:
    PacketCodecOptions options_;
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_PACKET_CODEC_H
