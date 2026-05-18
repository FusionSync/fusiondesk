#ifndef FUSIONDESK_PROTOCOL_BYTE_IO_H
#define FUSIONDESK_PROTOCOL_BYTE_IO_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "fusiondesk/core/protocol/protocol_types.h"

namespace fusiondesk {
namespace protocol {

inline bool writeU8At(ByteBuffer& output, std::size_t offset, std::uint8_t value)
{
    if (offset >= output.size())
        return false;
    output[offset] = value;
    return true;
}

inline bool writeU16At(ByteBuffer& output, std::size_t offset, std::uint16_t value)
{
    if (offset + 2U > output.size())
        return false;
    output[offset] = static_cast<std::uint8_t>((value >> 8) & 0xffU);
    output[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
    return true;
}

inline bool writeU32At(ByteBuffer& output, std::size_t offset, std::uint32_t value)
{
    if (offset + 4U > output.size())
        return false;
    output[offset] = static_cast<std::uint8_t>((value >> 24) & 0xffU);
    output[offset + 1U] = static_cast<std::uint8_t>((value >> 16) & 0xffU);
    output[offset + 2U] = static_cast<std::uint8_t>((value >> 8) & 0xffU);
    output[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
    return true;
}

inline bool writeU64At(ByteBuffer& output, std::size_t offset, std::uint64_t value)
{
    if (offset + 8U > output.size())
        return false;
    for (std::size_t index = 0; index < 8U; ++index) {
        output[offset + index] =
            static_cast<std::uint8_t>((value >> ((7U - index) * 8U)) & 0xffU);
    }
    return true;
}

inline bool readU8At(const ByteBuffer& input, std::size_t offset, std::uint8_t& value)
{
    if (offset >= input.size())
        return false;
    value = input[offset];
    return true;
}

inline bool readU16At(const ByteBuffer& input, std::size_t offset, std::uint16_t& value)
{
    if (offset + 2U > input.size())
        return false;
    value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8) |
        static_cast<std::uint16_t>(input[offset + 1U]));
    return true;
}

inline bool readU32At(const ByteBuffer& input, std::size_t offset, std::uint32_t& value)
{
    if (offset + 4U > input.size())
        return false;
    value = (static_cast<std::uint32_t>(input[offset]) << 24) |
            (static_cast<std::uint32_t>(input[offset + 1U]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 2U]) << 8) |
            static_cast<std::uint32_t>(input[offset + 3U]);
    return true;
}

inline bool readU64At(const ByteBuffer& input, std::size_t offset, std::uint64_t& value)
{
    if (offset + 8U > input.size())
        return false;
    value = 0;
    for (std::size_t index = 0; index < 8U; ++index)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + index]);
    return true;
}

class ByteWriter
{
public:
    explicit ByteWriter(std::size_t reserveBytes = 0)
    {
        bytes_.reserve(reserveBytes);
    }

    void u8(std::uint8_t value)
    {
        bytes_.push_back(value);
    }

    void u16(std::uint16_t value)
    {
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
    }

    void u32(std::uint32_t value)
    {
        bytes_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xffU));
    }

    void u64(std::uint64_t value)
    {
        for (std::size_t index = 0; index < 8U; ++index)
            bytes_.push_back(
                static_cast<std::uint8_t>((value >> ((7U - index) * 8U)) & 0xffU));
    }

    void i32(std::int32_t value)
    {
        u32(static_cast<std::uint32_t>(value));
    }

    void raw(const ByteBuffer& bytes)
    {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    bool string16(const std::string& value)
    {
        if (value.size() > std::numeric_limits<std::uint16_t>::max())
            return false;
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    bool string32(const std::string& value)
    {
        if (value.size() > std::numeric_limits<std::uint32_t>::max())
            return false;
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    bool bytes32(const ByteBuffer& value)
    {
        if (value.size() > std::numeric_limits<std::uint32_t>::max())
            return false;
        u32(static_cast<std::uint32_t>(value.size()));
        raw(value);
        return true;
    }

    const ByteBuffer& bytes() const
    {
        return bytes_;
    }

    ByteBuffer take()
    {
        return std::move(bytes_);
    }

private:
    ByteBuffer bytes_;
};

class ByteReader
{
public:
    explicit ByteReader(const ByteBuffer& bytes, std::size_t offset = 0)
        : bytes_(bytes)
        , offset_(offset)
    {
    }

    bool u8(std::uint8_t& value)
    {
        if (!readU8At(bytes_, offset_, value))
            return false;
        offset_ += 1U;
        return true;
    }

    bool u16(std::uint16_t& value)
    {
        if (!readU16At(bytes_, offset_, value))
            return false;
        offset_ += 2U;
        return true;
    }

    bool u32(std::uint32_t& value)
    {
        if (!readU32At(bytes_, offset_, value))
            return false;
        offset_ += 4U;
        return true;
    }

    bool u64(std::uint64_t& value)
    {
        if (!readU64At(bytes_, offset_, value))
            return false;
        offset_ += 8U;
        return true;
    }

    bool i32(std::int32_t& value)
    {
        std::uint32_t raw = 0;
        if (!u32(raw))
            return false;
        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool string16(std::string& value,
                  std::size_t maxBytes = std::numeric_limits<std::size_t>::max())
    {
        std::uint16_t size = 0;
        if (!u16(size) || size > maxBytes || offset_ + size > bytes_.size())
            return false;
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return true;
    }

    bool string32(std::string& value,
                  std::size_t maxBytes = std::numeric_limits<std::size_t>::max())
    {
        std::uint32_t size = 0;
        if (!u32(size) || size > maxBytes || offset_ + size > bytes_.size())
            return false;
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return true;
    }

    bool bytes32(ByteBuffer& value,
                 std::size_t maxBytes = std::numeric_limits<std::size_t>::max())
    {
        std::uint32_t size = 0;
        if (!u32(size) || size > maxBytes || offset_ + size > bytes_.size())
            return false;
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return true;
    }

    bool skip(std::size_t bytes)
    {
        if (offset_ + bytes > bytes_.size())
            return false;
        offset_ += bytes;
        return true;
    }

    std::size_t offset() const
    {
        return offset_;
    }

    std::size_t remaining() const
    {
        return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0;
    }

    bool atEnd() const
    {
        return offset_ == bytes_.size();
    }

private:
    const ByteBuffer& bytes_;
    std::size_t offset_ = 0;
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_BYTE_IO_H
