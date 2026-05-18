#include "fdcl_codec_internal.h"

#include <cstddef>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

bool readU16(const protocol::ByteBuffer& input,
             std::size_t& offset,
             std::uint16_t& value)
{
    if (offset + 2 > input.size())
        return false;
    value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8) |
        static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

bool readU32(const protocol::ByteBuffer& input,
             std::size_t& offset,
             std::uint32_t& value)
{
    if (offset + 4 > input.size())
        return false;
    value = (static_cast<std::uint32_t>(input[offset]) << 24) |
            (static_cast<std::uint32_t>(input[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 2]) << 8) |
            static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool readU64(const protocol::ByteBuffer& input,
             std::size_t& offset,
             std::uint64_t& value)
{
    if (offset + 8 > input.size())
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + i]);
    offset += 8;
    return true;
}

} // namespace

FdclDecodeResult decodeFdclObjectLockBody(
    const protocol::ByteBuffer& body,
    FdclOperation operation)
{
    FdclDecodeResult result;
    result.operation = operation;

    std::size_t offset = 0;
    std::uint16_t reserved = 0;
    if (!readU64(body, offset, result.objectLock.bundleId) ||
        !readU64(body, offset, result.objectLock.offerId) ||
        !readU64(body, offset, result.objectLock.ownerEpoch) ||
        !readU64(body, offset, result.objectLock.sourceId) ||
        !readU64(body, offset, result.objectLock.objectId) ||
        !readU32(body, offset, result.objectLock.fileIndex) ||
        !readU64(body, offset, result.objectLock.lockId) ||
        !readU64(body, offset, result.objectLock.leaseUsec) ||
        !readU16(body, offset, reserved)) {
        result.error = "object lock body is truncated";
        return result;
    }
    if (reserved != 0) {
        result.error = "object lock reserved field is not zero";
        return result;
    }
    if (offset != body.size()) {
        result.error = "object lock body has trailing bytes";
        return result;
    }
    if (result.objectLock.bundleId == 0 ||
        result.objectLock.offerId == 0 ||
        result.objectLock.ownerEpoch == 0 ||
        result.objectLock.sourceId == 0 ||
        result.objectLock.objectId == 0) {
        result.error = "object lock identity is invalid";
        return result;
    }
    if (operation == FdclOperation::UnlockObject &&
        result.objectLock.lockId == 0) {
        result.error = "object unlock identity is invalid";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
