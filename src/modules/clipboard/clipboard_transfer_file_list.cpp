#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

void appendU8(protocol::ByteBuffer& output, std::uint8_t value)
{
    output.push_back(value);
}

void appendU16(protocol::ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32(protocol::ByteBuffer& output, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        output.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffU));
}

void appendU64(protocol::ByteBuffer& output, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        output.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffU));
}

class BinaryReader
{
public:
    explicit BinaryReader(const protocol::ByteBuffer& bytes)
        : bytes_(bytes)
    {
    }

    bool readU8(std::uint8_t& value)
    {
        if (offset_ + 1 > bytes_.size())
            return false;
        value = bytes_[offset_++];
        return true;
    }

    bool readU16(std::uint16_t& value)
    {
        if (offset_ + 2 > bytes_.size())
            return false;
        value = static_cast<std::uint16_t>(bytes_[offset_]) |
                static_cast<std::uint16_t>(bytes_[offset_ + 1] << 8U);
        offset_ += 2;
        return true;
    }

    bool readU32(std::uint32_t& value)
    {
        if (offset_ + 4 > bytes_.size())
            return false;
        value = 0;
        for (int i = 0; i < 4; ++i)
            value |= static_cast<std::uint32_t>(bytes_[offset_ + i]) << (i * 8);
        offset_ += 4;
        return true;
    }

    bool readU64(std::uint64_t& value)
    {
        if (offset_ + 8 > bytes_.size())
            return false;
        value = 0;
        for (int i = 0; i < 8; ++i)
            value |= static_cast<std::uint64_t>(bytes_[offset_ + i]) << (i * 8);
        offset_ += 8;
        return true;
    }

    bool readString(std::string& value, std::size_t maxBytes)
    {
        std::uint16_t length = 0;
        if (!readU16(length) ||
            length > maxBytes ||
            offset_ + length > bytes_.size())
            return false;
        value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     bytes_.begin() +
                         static_cast<std::ptrdiff_t>(offset_ + length));
        offset_ += length;
        return true;
    }

    bool finished() const
    {
        return offset_ == bytes_.size();
    }

private:
    const protocol::ByteBuffer& bytes_;
    std::size_t offset_ = 0;
};

bool matchesFileDescriptor(const TransferFormatDescriptor& descriptor,
                           const TransferReadRequest& request)
{
    if (!request.canonicalFormat.empty() &&
        request.canonicalFormat != descriptor.canonicalFormat)
        return false;
    if (request.formatId != 0 && request.formatId != descriptor.formatId)
        return false;
    if (request.itemIndex != descriptor.itemIndex)
        return false;
    return request.localFormatToken == 0 ||
           descriptor.localFormatToken == request.localFormatToken;
}

} // namespace

std::string sanitizeTransferFileDisplayName(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '\0')
            result.push_back('_');
        else
            result.push_back(ch);
    }

    while (!result.empty() &&
           (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }

    for (char& ch : result) {
        if (ch == ' ' || ch == '.')
            ch = '_';
        else
            break;
    }

    if (result.empty() || result == "." || result == "..")
        return "unnamed";
    return result;
}

std::string sanitizeTransferFileRelativePath(const std::string& value)
{
    std::string result;
    std::string segment;
    auto flushSegment = [&result, &segment]() {
        if (segment.empty())
            return;
        const std::string safe = sanitizeTransferFileDisplayName(segment);
        segment.clear();
        if (safe.empty() || safe == "unnamed")
            return;
        if (!result.empty())
            result.push_back('/');
        result += safe;
    };

    for (char ch : value) {
        if (ch == '/' || ch == '\\') {
            flushSegment();
            continue;
        }
        if (ch == ':' || ch == '\0')
            segment.push_back('_');
        else
            segment.push_back(ch);
    }
    flushSegment();
    return result.empty() ? sanitizeTransferFileDisplayName(value) : result;
}

protocol::ByteBuffer encodeTransferFileList(const TransferFileList& fileList)
{
    protocol::ByteBuffer output;
    output.push_back('F');
    output.push_back('D');
    output.push_back('F');
    output.push_back('L');
    appendU16(output, 1);
    appendU16(output, 1);
    appendU32(output, static_cast<std::uint32_t>(fileList.files.size()));

    for (const TransferFileDescriptor& file : fileList.files) {
        const std::string displayName =
            sanitizeTransferFileDisplayName(file.displayName);
        std::string relativePath =
            sanitizeTransferFileRelativePath(file.relativePath);
        if (relativePath.empty() || relativePath == "unnamed")
            relativePath = displayName;
        appendU64(output, file.objectId);
        appendU64(output, file.sizeBytes);
        appendU64(output, file.lastModifiedUnixUsec);
        appendU8(output, file.directory ? 1U : 0U);
        appendU16(output, static_cast<std::uint16_t>(
                              std::min<std::size_t>(displayName.size(),
                                                     65535)));
        output.insert(output.end(),
                      displayName.begin(),
                      displayName.begin() +
                          static_cast<std::ptrdiff_t>(
                              std::min<std::size_t>(displayName.size(),
                                                     65535)));
        appendU16(output, static_cast<std::uint16_t>(
                              std::min<std::size_t>(relativePath.size(),
                                                     65535)));
        output.insert(output.end(),
                      relativePath.begin(),
                      relativePath.begin() +
                          static_cast<std::ptrdiff_t>(
                              std::min<std::size_t>(relativePath.size(),
                                                     65535)));
    }
    return output;
}

TransferFileListDecodeResult decodeTransferFileList(
    const protocol::ByteBuffer& bytes,
    std::size_t maxFiles,
    std::size_t maxNameBytes)
{
    TransferFileListDecodeResult result;
    if (bytes.size() < 12 ||
        bytes[0] != 'F' ||
        bytes[1] != 'D' ||
        bytes[2] != 'F' ||
        bytes[3] != 'L') {
        result.status = protocol::ResponseStatus::ProtocolError;
        result.message = "FDCL file list magic is invalid";
        return result;
    }

    BinaryReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint32_t count = 0;
    if (!reader.readU32(magic) ||
        !reader.readU16(major) ||
        !reader.readU16(minor) ||
        !reader.readU32(count)) {
        result.status = protocol::ResponseStatus::ProtocolError;
        result.message = "FDCL file list header is truncated";
        return result;
    }
    (void)magic;

    if (major != 1 || minor > 1) {
        result.status = protocol::ResponseStatus::Unsupported;
        result.message = "FDCL file list version is unsupported";
        return result;
    }
    if (count > maxFiles) {
        result.status = protocol::ResponseStatus::TooLarge;
        result.message = "FDCL file list exceeds max file count";
        return result;
    }

    result.fileList.files.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TransferFileDescriptor file;
        std::uint8_t directory = 0;
        if (!reader.readU64(file.objectId) ||
            !reader.readU64(file.sizeBytes) ||
            !reader.readU64(file.lastModifiedUnixUsec) ||
            !reader.readU8(directory) ||
            !reader.readString(file.displayName, maxNameBytes)) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message = "FDCL file list entry is truncated";
            return result;
        }
        file.directory = directory != 0;
        file.displayName = sanitizeTransferFileDisplayName(file.displayName);
        if (minor >= 1) {
            if (!reader.readString(file.relativePath, maxNameBytes * 8)) {
                result.status = protocol::ResponseStatus::ProtocolError;
                result.message = "FDCL file list relative path is truncated";
                return result;
            }
            file.relativePath =
                sanitizeTransferFileRelativePath(file.relativePath);
        }
        if (file.relativePath.empty() || file.relativePath == "unnamed")
            file.relativePath = file.displayName;
        result.fileList.files.push_back(std::move(file));
    }

    if (!reader.finished()) {
        result.status = protocol::ResponseStatus::ProtocolError;
        result.message = "FDCL file list has trailing bytes";
        return result;
    }

    result.ok = true;
    result.status = protocol::ResponseStatus::Ok;
    return result;
}

FileGroupTransferSource::FileGroupTransferSource(
    TransferSourceId sourceId,
    TransferFormatDescriptor descriptor,
    TransferFileList fileList)
    : sourceId_(sourceId),
      descriptor_(std::move(descriptor)),
      fileList_(std::move(fileList))
{
    descriptor_.canonicalFormat = FdclFileListFormat;
    descriptor_.preferredEncoding = TransferEncodingMode::CanonicalBytes;
    descriptor_.canInline = true;
}

TransferSourceId FileGroupTransferSource::id() const
{
    return sourceId_;
}

std::vector<TransferFormatDescriptor> FileGroupTransferSource::formats() const
{
    return {descriptor_};
}

TransferReadResult FileGroupTransferSource::read(
    const TransferReadRequest& request)
{
    TransferReadResult result;
    if (request.sourceId != 0 && request.sourceId != sourceId_) {
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "file group source id is not found";
        return result;
    }

    if (!matchesFileDescriptor(descriptor_, request)) {
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "file group descriptor format is not found";
        return result;
    }

    protocol::ByteBuffer encoded = encodeTransferFileList(fileList_);
    if (request.acceptedMaxBytes != 0 &&
        encoded.size() > request.acceptedMaxBytes) {
        result.status = protocol::ResponseStatus::TooLarge;
        result.message = "file group descriptor exceeds accepted maximum";
        return result;
    }

    result.status = protocol::ResponseStatus::Ok;
    result.canonicalFormat = FdclFileListFormat;
    result.encoding = TransferEncodingMode::CanonicalBytes;
    result.bytes = std::move(encoded);
    return result;
}

const TransferFileList& FileGroupTransferSource::fileList() const
{
    return fileList_;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
