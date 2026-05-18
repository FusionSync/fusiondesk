#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

class MemoryRemoteFileReader : public IClipboardRemoteFileReader
{
public:
    explicit MemoryRemoteFileReader(protocol::ByteBuffer content)
        : content_(std::move(content))
    {
    }

    TransferFileRangeResult readRemoteFileRange(
        const TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) override
    {
        (void)timeoutMs;
        requests.push_back(request);
        TransferFileRangeResult result;
        result.status = protocol::ResponseStatus::Ok;

        if (request.offset >= content_.size()) {
            result.endOfFile = true;
            return result;
        }

        const std::size_t requested =
            static_cast<std::size_t>(request.requestedBytes);
        const std::size_t offset =
            static_cast<std::size_t>(request.offset);
        const std::size_t count =
            std::min(requested, content_.size() - offset);
        result.bytes.insert(result.bytes.end(),
                            content_.begin() +
                                static_cast<std::ptrdiff_t>(offset),
                            content_.begin() +
                                static_cast<std::ptrdiff_t>(offset + count));
        result.endOfFile = offset + count >= content_.size();
        return result;
    }

    std::vector<TransferFileRangeRequest> requests;

private:
    protocol::ByteBuffer content_;
};

class FixedRemoteFileReader : public IClipboardRemoteFileReader
{
public:
    TransferFileRangeResult fixedResult;

    TransferFileRangeResult readRemoteFileRange(
        const TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) override
    {
        (void)request;
        (void)timeoutMs;
        return fixedResult;
    }
};

class MemoryRangeSink : public ITransferFileRangeSink
{
public:
    TransferFileDrainSinkResult writeRange(
        const TransferFileRangeRequest& request,
        const protocol::ByteBuffer& bytes,
        bool endOfFile) override
    {
        offsets.push_back(request.offset);
        eofFlags.push_back(endOfFile);
        content.insert(content.end(), bytes.begin(), bytes.end());
        return {};
    }

    protocol::ByteBuffer content;
    std::vector<std::uint64_t> offsets;
    std::vector<bool> eofFlags;
};

void mapsNativeNamesThroughCanonicalRegistry()
{
    DefaultTransferFormatMapper mapper;

    TransferFormatMappingRequest request;
    request.native.platform = TransferPlatformFamily::Windows;
    request.native.nativeFormatName = "CF_UNICODETEXT";
    request.native.localFormatToken = 13;
    request.formatId = 7;
    request.estimatedBytes = 16;

    const TransferFormatMappingResult mapped =
        mapper.mapNativeToCanonical(request);
    assert(mapped.mapped);
    assert(mapped.descriptor.canonicalFormat == TextPlainUtf8Format);
    assert(mapped.descriptor.nativeFormatName == "CF_UNICODETEXT");
    assert(mapped.descriptor.localFormatToken == 13);
    assert(mapped.descriptor.formatId == 7);
    assert(mapped.descriptor.preferredEncoding ==
           TransferEncodingMode::NativePassthrough);
    assert(mapped.formatClass == TransferFormatClass::PlainText);

    const std::vector<NativeTransferFormatCandidate> linuxTargets =
        mapper.nativeCandidates(TextPlainUtf8Format,
                                TransferPlatformFamily::Linux);
    assert(!linuxTargets.empty());
    assert(linuxTargets.front().native.nativeFormatName ==
           "text/plain;charset=utf-8");
    assert(linuxTargets.front().encoding ==
           TransferEncodingMode::CanonicalBytes);

    const std::vector<NativeTransferFormatCandidate> windowsImageTargets =
        mapper.nativeCandidates(ImagePngFormat,
                                TransferPlatformFamily::Windows);
    assert(windowsImageTargets.size() >= 3);
    assert(windowsImageTargets[0].native.nativeFormatName == "PNG");
    assert(windowsImageTargets[0].encoding ==
           TransferEncodingMode::NativePassthrough);
    assert(std::any_of(
        windowsImageTargets.begin(),
        windowsImageTargets.end(),
        [](const NativeTransferFormatCandidate& candidate) {
            return candidate.native.nativeFormatName == "CF_DIBV5" &&
                   candidate.encoding == TransferEncodingMode::Transcoded;
        }));
    assert(std::any_of(
        windowsImageTargets.begin(),
        windowsImageTargets.end(),
        [](const NativeTransferFormatCandidate& candidate) {
            return candidate.native.nativeFormatName == "CF_DIB" &&
                   candidate.encoding == TransferEncodingMode::Transcoded;
        }));
}

void identityTranscoderPassesCompatibleRepresentationsOnly()
{
    IdentityTransferTranscoder transcoder;

    TransferTranscodeRequest request;
    request.canonicalFormat = TextPlainUtf8Format;
    request.sourceEncoding = TransferEncodingMode::CanonicalBytes;
    request.targetEncoding = TransferEncodingMode::CanonicalBytes;
    request.bytes = bytes("hello");

    assert(transcoder.canTranscode(request));
    const TransferTranscodeResult result = transcoder.transcode(request);
    assert(result.ok());
    assert(result.encoding == TransferEncodingMode::CanonicalBytes);
    assert(result.bytes == bytes("hello"));

    request.sourceEncoding = TransferEncodingMode::NativePassthrough;
    request.targetEncoding = TransferEncodingMode::CanonicalBytes;
    assert(!transcoder.canTranscode(request));
    assert(transcoder.transcode(request).status ==
           protocol::ResponseStatus::Unsupported);
}

void sourceRegistryEnforcesBundleIdentity()
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.formatId = 55;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("hello");

    TransferSourceBundle bundle;
    bundle.bundleId = 10;
    bundle.offerId = 20;
    bundle.ownerEpoch = 30;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            40,
            std::vector<MaterializedTransferEntry>{entry}));

    InMemoryTransferSourceRegistry registry;
    assert(registry.store(bundle) == protocol::ResponseStatus::Ok);
    assert(registry.size() == 1);

    TransferReadRequest request;
    request.bundleId = 10;
    request.offerId = 20;
    request.ownerEpoch = 30;
    request.sourceId = 40;
    request.formatId = 55;
    request.canonicalFormat = TextPlainUtf8Format;

    TransferSourceLookup lookup = registry.lookupSource(request);
    assert(lookup.found());
    assert(lookup.source->read(request).bytes == bytes("hello"));

    request.ownerEpoch = 31;
    lookup = registry.lookupSource(request);
    assert(!lookup.found());
    assert(lookup.status == protocol::ResponseStatus::Conflict);
}

void sourceRegistryRetainsLockedFileObjectsUntilUnlock()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "archive.zip";
    file.sizeBytes = 8192;
    list.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.formatId = 55;
    descriptor.localFormatToken = 66;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.canStream = true;

    TransferSourceBundle bundle;
    bundle.bundleId = 10;
    bundle.offerId = 20;
    bundle.ownerEpoch = 30;
    bundle.sources.push_back(
        std::make_shared<FileGroupTransferSource>(40, descriptor, list));

    InMemoryTransferSourceRegistry registry;
    assert(registry.store(bundle) == protocol::ResponseStatus::Ok);

    TransferObjectLockRequest lockRequest;
    lockRequest.bundleId = 10;
    lockRequest.offerId = 20;
    lockRequest.ownerEpoch = 30;
    lockRequest.sourceId = 40;
    lockRequest.objectId = 9001;
    lockRequest.fileIndex = 0;
    lockRequest.leaseUsec = 30000000;

    const TransferObjectLockResult lock = registry.lockObject(lockRequest);
    assert(lock.ok());
    assert(lock.lockId != 0);
    assert(lock.leaseUsec == 30000000);
    assert(registry.lockCount() == 1);

    assert(registry.clearOffer(20));
    assert(registry.size() == 1);

    TransferReadRequest read;
    read.bundleId = 10;
    read.offerId = 20;
    read.ownerEpoch = 30;
    read.sourceId = 40;
    read.formatId = 55;
    read.localFormatToken = 66;
    read.canonicalFormat = FdclFileListFormat;
    assert(registry.lookupSource(read).found());

    lockRequest.lockId = lock.lockId;
    const TransferObjectLockResult unlock = registry.unlockObject(lockRequest);
    assert(unlock.ok());
    assert(unlock.lockId == lock.lockId);
    assert(registry.lockCount() == 0);
    assert(registry.size() == 0);
}

void sourceRegistryRejectsStaleObjectLockIdentity()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "archive.zip";
    list.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.formatId = 55;
    descriptor.canInline = true;
    descriptor.canStream = true;

    TransferSourceBundle bundle;
    bundle.bundleId = 10;
    bundle.offerId = 20;
    bundle.ownerEpoch = 30;
    bundle.sources.push_back(
        std::make_shared<FileGroupTransferSource>(40, descriptor, list));

    InMemoryTransferSourceRegistry registry;
    assert(registry.store(bundle) == protocol::ResponseStatus::Ok);

    TransferObjectLockRequest lockRequest;
    lockRequest.bundleId = 10;
    lockRequest.offerId = 20;
    lockRequest.ownerEpoch = 31;
    lockRequest.sourceId = 40;
    lockRequest.objectId = 9001;
    lockRequest.fileIndex = 0;

    const TransferObjectLockResult lock = registry.lockObject(lockRequest);
    assert(lock.status == protocol::ResponseStatus::Conflict);
}

void sourceRegistryCanReleaseAllObjectLocks()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "archive.zip";
    list.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.formatId = 55;
    descriptor.canInline = true;
    descriptor.canStream = true;

    TransferSourceBundle bundle;
    bundle.bundleId = 10;
    bundle.offerId = 20;
    bundle.ownerEpoch = 30;
    bundle.sources.push_back(
        std::make_shared<FileGroupTransferSource>(40, descriptor, list));

    InMemoryTransferSourceRegistry registry;
    assert(registry.store(bundle) == protocol::ResponseStatus::Ok);

    TransferObjectLockRequest lockRequest;
    lockRequest.bundleId = 10;
    lockRequest.offerId = 20;
    lockRequest.ownerEpoch = 30;
    lockRequest.sourceId = 40;
    lockRequest.objectId = 9001;
    lockRequest.fileIndex = 0;
    assert(registry.lockObject(lockRequest).ok());
    assert(registry.lockCount() == 1);

    assert(registry.releaseAllLocks() == 1);
    assert(registry.lockCount() == 0);
    assert(registry.size() == 1);
    assert(registry.clearAll() == 1);
    assert(registry.size() == 0);
}

void fileListEncodingSanitizesAndRoundTrips()
{
    TransferFileList list;
    TransferFileDescriptor first;
    first.objectId = 1001;
    first.displayName = "..\\secret.txt";
    first.sizeBytes = 42;
    first.lastModifiedUnixUsec = 12345;
    list.files.push_back(first);

    TransferFileDescriptor second;
    second.objectId = 1002;
    second.displayName = "folder";
    second.directory = true;
    list.files.push_back(second);

    const protocol::ByteBuffer encoded = encodeTransferFileList(list);
    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(encoded);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 2);
    assert(decoded.fileList.files[0].objectId == 1001);
    assert(decoded.fileList.files[0].displayName == "___secret.txt");
    assert(decoded.fileList.files[0].sizeBytes == 42);
    assert(decoded.fileList.files[0].relativePath == "___secret.txt");
    assert(decoded.fileList.files[1].directory);

    assert(decodeTransferFileList(encoded, 1).status ==
           protocol::ResponseStatus::TooLarge);
}

void fileListEncodingPreservesRelativePaths()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 3001;
    file.displayName = "report.pdf";
    file.relativePath = "folder\\nested/report.pdf";
    file.sizeBytes = 4096;
    list.files.push_back(file);

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(encodeTransferFileList(list));
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 1);
    assert(decoded.fileList.files.front().displayName == "report.pdf");
    assert(decoded.fileList.files.front().relativePath ==
           "folder/nested/report.pdf");
}

void fileGroupSourceServesCanonicalFileList()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 2001;
    file.displayName = "report.pdf";
    file.sizeBytes = 1024;
    list.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.localFormatToken = 77;
    descriptor.formatId = 88;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 64;
    descriptor.canInline = true;
    descriptor.canStream = false;

    FileGroupTransferSource source(3001, descriptor, list);
    const std::vector<TransferFormatDescriptor> formats = source.formats();
    assert(formats.size() == 1);
    assert(formats.front().canonicalFormat == FdclFileListFormat);
    assert(formats.front().nativeFormatName == "FileGroupDescriptorW");

    TransferReadRequest request;
    request.sourceId = 3001;
    request.formatId = 88;
    request.localFormatToken = 77;
    request.canonicalFormat = FdclFileListFormat;
    request.acceptedMaxBytes = 1024;
    const TransferReadResult result = source.read(request);
    assert(result.ok());
    assert(result.canonicalFormat == FdclFileListFormat);

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(result.bytes);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 1);
    assert(decoded.fileList.files.front().displayName == "report.pdf");

    request.acceptedMaxBytes = 1;
    assert(source.read(request).status == protocol::ResponseStatus::TooLarge);
}

void remoteFileDrainWritesSequentialChunksUntilEof()
{
    MemoryRemoteFileReader reader(bytes("abcdefghij"));
    MemoryRangeSink sink;

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;
    request.objectId = 5;
    request.fileIndex = 0;

    TransferFileDrainOptions options;
    options.chunkBytes = 4;
    options.timeoutMs = 50;

    const TransferFileDrainResult result =
        drainRemoteFileRange(reader, request, sink, options);
    assert(result.ok());
    assert(result.bytesWritten == 10);
    assert(result.chunksWritten == 3);
    assert(result.endOfFile);
    assert(sink.content == bytes("abcdefghij"));
    assert(reader.requests.size() == 3);
    assert(reader.requests[0].offset == 0);
    assert(reader.requests[0].requestedBytes == 4);
    assert(reader.requests[1].offset == 4);
    assert(reader.requests[2].offset == 8);
    assert(sink.eofFlags.size() == 3);
    assert(!sink.eofFlags[0]);
    assert(!sink.eofFlags[1]);
    assert(sink.eofFlags[2]);
}

void remoteFileDrainWritesEmptyFileEofMarker()
{
    MemoryRemoteFileReader reader({});
    MemoryRangeSink sink;

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileDrainOptions options;
    options.chunkBytes = 8;

    const TransferFileDrainResult result =
        drainRemoteFileRange(reader, request, sink, options);
    assert(result.ok());
    assert(result.bytesWritten == 0);
    assert(result.chunksWritten == 1);
    assert(result.endOfFile);
    assert(sink.content.empty());
    assert(sink.eofFlags.size() == 1);
    assert(sink.eofFlags.front());
}

void remoteFileDrainRejectsZeroByteNonEof()
{
    FixedRemoteFileReader reader;
    reader.fixedResult.status = protocol::ResponseStatus::Ok;
    reader.fixedResult.endOfFile = false;
    MemoryRangeSink sink;

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileDrainOptions options;
    options.chunkBytes = 8;

    const TransferFileDrainResult result =
        drainRemoteFileRange(reader, request, sink, options);
    assert(result.status == protocol::ResponseStatus::ProtocolError);
    assert(result.bytesWritten == 0);
    assert(result.chunksWritten == 0);
    assert(sink.content.empty());
}

void remoteFileDrainRejectsOversizedResponse()
{
    FixedRemoteFileReader reader;
    reader.fixedResult.status = protocol::ResponseStatus::Ok;
    reader.fixedResult.bytes = bytes("abcdef");
    reader.fixedResult.endOfFile = true;
    MemoryRangeSink sink;

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileDrainOptions options;
    options.chunkBytes = 4;

    const TransferFileDrainResult result =
        drainRemoteFileRange(reader, request, sink, options);
    assert(result.status == protocol::ResponseStatus::ProtocolError);
    assert(sink.content.empty());
}

void remoteFileDrainHonorsMaximumTotalBytes()
{
    MemoryRemoteFileReader reader(bytes("abcdef"));
    MemoryRangeSink sink;

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileDrainOptions options;
    options.chunkBytes = 3;
    options.maxTotalBytes = 5;

    const TransferFileDrainResult result =
        drainRemoteFileRange(reader, request, sink, options);
    assert(result.status == protocol::ResponseStatus::TooLarge);
    assert(result.bytesWritten == 3);
    assert(result.chunksWritten == 1);
    assert(sink.content == bytes("abc"));
}

void remoteFileWindowReadFillsRequestedBytesAcrossChunks()
{
    MemoryRemoteFileReader reader(bytes("abcdefghijklmnop"));

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileWindowReadOptions options;
    options.chunkBytes = 4;
    options.timeoutMs = 50;

    const TransferFileRangeResult result =
        readRemoteFileRangeWindow(reader, request, 0, 10, options);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytes == bytes("abcdefghij"));
    assert(!result.endOfFile);
    assert(reader.requests.size() == 3);
    assert(reader.requests[0].offset == 0);
    assert(reader.requests[0].requestedBytes == 4);
    assert(reader.requests[1].offset == 4);
    assert(reader.requests[1].requestedBytes == 4);
    assert(reader.requests[2].offset == 8);
    assert(reader.requests[2].requestedBytes == 2);
}

void remoteFileWindowReadStopsAtTrueEof()
{
    MemoryRemoteFileReader reader(bytes("abcde"));

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileWindowReadOptions options;
    options.chunkBytes = 4;

    const TransferFileRangeResult result =
        readRemoteFileRangeWindow(reader, request, 0, 10, options);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytes == bytes("abcde"));
    assert(result.endOfFile);
    assert(reader.requests.size() == 2);
    assert(reader.requests[0].offset == 0);
    assert(reader.requests[1].offset == 4);
}

void remoteFileWindowReadRejectsZeroByteNonEof()
{
    FixedRemoteFileReader reader;
    reader.fixedResult.status = protocol::ResponseStatus::Ok;
    reader.fixedResult.endOfFile = false;

    TransferFileRangeRequest request;
    request.bundleId = 1;
    request.offerId = 2;
    request.ownerEpoch = 3;
    request.sourceId = 4;

    TransferFileWindowReadOptions options;
    options.chunkBytes = 4;

    const TransferFileRangeResult result =
        readRemoteFileRangeWindow(reader, request, 0, 10, options);
    assert(result.status == protocol::ResponseStatus::ProtocolError);
}

} // namespace

int main()
{
    mapsNativeNamesThroughCanonicalRegistry();
    identityTranscoderPassesCompatibleRepresentationsOnly();
    sourceRegistryEnforcesBundleIdentity();
    sourceRegistryRetainsLockedFileObjectsUntilUnlock();
    sourceRegistryRejectsStaleObjectLockIdentity();
    sourceRegistryCanReleaseAllObjectLocks();
    fileListEncodingSanitizesAndRoundTrips();
    fileListEncodingPreservesRelativePaths();
    fileGroupSourceServesCanonicalFileList();
    remoteFileDrainWritesSequentialChunksUntilEof();
    remoteFileDrainWritesEmptyFileEofMarker();
    remoteFileDrainRejectsZeroByteNonEof();
    remoteFileDrainRejectsOversizedResponse();
    remoteFileDrainHonorsMaximumTotalBytes();
    remoteFileWindowReadFillsRequestedBytesAcrossChunks();
    remoteFileWindowReadStopsAtTrueEof();
    remoteFileWindowReadRejectsZeroByteNonEof();
    return 0;
}
