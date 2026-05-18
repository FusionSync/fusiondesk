#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

#include <algorithm>
#include <limits>

namespace fusiondesk {
namespace modules {
namespace clipboard {

TransferFileDrainResult drainRemoteFileRange(
    IClipboardRemoteFileReader& reader,
    const TransferFileRangeRequest& baseRequest,
    ITransferFileRangeSink& sink,
    const TransferFileDrainOptions& options)
{
    TransferFileDrainResult result;
    if (options.chunkBytes == 0) {
        result.status = protocol::ResponseStatus::InvalidArgument;
        result.message = "clipboard file drain chunk size is zero";
        return result;
    }

    std::uint64_t offset = baseRequest.offset;
    for (;;) {
        if (options.maxTotalBytes != 0 &&
            result.bytesWritten >= options.maxTotalBytes) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "clipboard file exceeds max drain bytes";
            return result;
        }

        std::uint64_t requestedBytes = options.chunkBytes;
        if (options.maxTotalBytes != 0) {
            requestedBytes = std::min(requestedBytes,
                                      options.maxTotalBytes -
                                          result.bytesWritten);
        }
        if (requestedBytes == 0) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "clipboard file exceeds max drain bytes";
            return result;
        }

        TransferFileRangeRequest rangeRequest = baseRequest;
        rangeRequest.offset = offset;
        rangeRequest.requestedBytes = requestedBytes;

        TransferFileRangeResult range =
            reader.readRemoteFileRange(rangeRequest, options.timeoutMs);
        if (!range.ok()) {
            result.status = range.status;
            result.message = range.message.empty()
                                 ? "clipboard remote file range read failed"
                                 : range.message;
            return result;
        }

        const std::uint64_t receivedBytes =
            static_cast<std::uint64_t>(range.bytes.size());
        if (receivedBytes > requestedBytes) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message =
                "clipboard remote file range exceeded requested bytes";
            return result;
        }
        if (receivedBytes == 0 && !range.endOfFile) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message =
                "clipboard remote file range made no progress";
            return result;
        }
        if (options.maxTotalBytes != 0 &&
            receivedBytes == options.maxTotalBytes - result.bytesWritten &&
            !range.endOfFile) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "clipboard file exceeds max drain bytes";
            return result;
        }

        const TransferFileDrainSinkResult write =
            sink.writeRange(rangeRequest, range.bytes, range.endOfFile);
        if (!write.ok()) {
            result.status = write.status;
            result.message = write.message.empty()
                                 ? "clipboard file range sink rejected data"
                                 : write.message;
            return result;
        }

        result.bytesWritten += receivedBytes;
        ++result.chunksWritten;
        if (range.endOfFile) {
            result.status = protocol::ResponseStatus::Ok;
            result.endOfFile = true;
            return result;
        }

        if (offset > std::numeric_limits<std::uint64_t>::max() -
                         receivedBytes) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.message = "clipboard remote file range offset overflow";
            return result;
        }
        offset += receivedBytes;
    }
}

TransferFileRangeResult readRemoteFileRangeWindow(
    IClipboardRemoteFileReader& reader,
    const TransferFileRangeRequest& baseRequest,
    std::uint64_t offset,
    std::uint64_t requestedBytes,
    const TransferFileWindowReadOptions& options)
{
    TransferFileRangeResult result;
    if (requestedBytes == 0) {
        result.status = protocol::ResponseStatus::Ok;
        return result;
    }
    if (options.chunkBytes == 0) {
        result.status = protocol::ResponseStatus::InvalidArgument;
        result.message = "clipboard file window chunk size is zero";
        return result;
    }

    std::uint64_t currentOffset = offset;
    std::uint64_t remaining = requestedBytes;
    result.status = protocol::ResponseStatus::Ok;
    while (remaining > 0) {
        const std::uint64_t wanted = std::min(remaining, options.chunkBytes);
        TransferFileRangeRequest request = baseRequest;
        request.offset = currentOffset;
        request.requestedBytes = wanted;

        TransferFileRangeResult range =
            reader.readRemoteFileRange(request, options.timeoutMs);
        if (!range.ok())
            return range;

        const std::uint64_t received =
            static_cast<std::uint64_t>(range.bytes.size());
        if (received > wanted) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.bytes.clear();
            result.message =
                "clipboard remote file range exceeded requested window bytes";
            return result;
        }
        if (received == 0 && !range.endOfFile) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.bytes.clear();
            result.message =
                "clipboard remote file window read made no progress";
            return result;
        }

        result.bytes.insert(result.bytes.end(),
                            range.bytes.begin(),
                            range.bytes.end());
        if (range.endOfFile) {
            result.endOfFile = true;
            return result;
        }
        if (currentOffset > std::numeric_limits<std::uint64_t>::max() -
                                received) {
            result.status = protocol::ResponseStatus::ProtocolError;
            result.bytes.clear();
            result.message = "clipboard remote file window offset overflow";
            return result;
        }
        currentOffset += received;
        remaining -= received;
    }
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
