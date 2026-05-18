#include "qt_clipboard_local_files.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

std::string pathUtf8(const std::filesystem::path& path)
{
    return path.generic_u8string();
}

std::string displayNameFromPath(const std::filesystem::path& path)
{
    return sanitizeTransferFileDisplayName(pathUtf8(path.filename()));
}

std::string relativePathForChild(const std::string& parent,
                                 const std::filesystem::path& child)
{
    const std::string leaf = displayNameFromPath(child);
    if (parent.empty())
        return leaf;
    return sanitizeTransferFileRelativePath(parent + "/" + leaf);
}

std::uint64_t regularFileSize(const std::filesystem::path& path,
                              std::error_code& error)
{
    error.clear();
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
        return 0;
    return static_cast<std::uint64_t>(std::min<std::uintmax_t>(
        size,
        std::numeric_limits<std::uint64_t>::max()));
}

void appendPathToFileList(TransferFileList& output,
                          std::vector<std::filesystem::path>* paths,
                          const std::filesystem::path& path,
                          const std::string& relativePath,
                          const QtClipboardLocalFileOptions& options,
                          std::uint32_t depth)
{
    const std::uint32_t maxFileCount =
        options.maxFileCount == 0 ? 1 : options.maxFileCount;
    if (output.files.size() >= maxFileCount)
        return;

    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::exists(status))
        return;

    const bool directory = std::filesystem::is_directory(status);
    if (!directory && !std::filesystem::is_regular_file(status))
        return;

    TransferFileDescriptor descriptor;
    descriptor.objectId =
        static_cast<TransferObjectId>(output.files.size() + 1);
    descriptor.displayName = displayNameFromPath(path);
    descriptor.relativePath = sanitizeTransferFileRelativePath(relativePath);
    if (descriptor.relativePath.empty() || descriptor.relativePath == "unnamed")
        descriptor.relativePath = descriptor.displayName;
    descriptor.directory = directory;
    descriptor.sizeBytes = directory ? 0 : regularFileSize(path, error);
    descriptor.lastModifiedUnixUsec = 0;

    output.files.push_back(std::move(descriptor));
    if (paths != nullptr)
        paths->push_back(output.files.back().directory
                             ? std::filesystem::path()
                             : path);

    if (!output.files.back().directory ||
        !options.expandDirectories ||
        depth >= options.maxDirectoryDepth ||
        std::filesystem::is_symlink(status)) {
        return;
    }

    const std::string parentRelative = output.files.back().relativePath;
    std::filesystem::directory_iterator it(path, error);
    if (error)
        return;
    const std::filesystem::directory_iterator end;
    for (; it != end && output.files.size() < maxFileCount; it.increment(error)) {
        if (error)
            break;
        appendPathToFileList(output,
                             paths,
                             it->path(),
                             relativePathForChild(parentRelative, it->path()),
                             options,
                             depth + 1);
    }
}

class LocalQtFileTransferSource final
    : public TransferSource,
      public ITransferFileContentProvider
{
public:
    LocalQtFileTransferSource(TransferSourceId sourceId,
                              TransferFormatDescriptor descriptor,
                              TransferFileList fileList,
                              std::vector<std::filesystem::path> paths,
                              std::uint64_t maxRangeBytes,
                              std::uint64_t maxSingleFileBytes)
        : sourceId_(sourceId),
          descriptor_(std::move(descriptor)),
          fileList_(std::move(fileList)),
          paths_(std::move(paths)),
          maxRangeBytes_(maxRangeBytes == 0
                             ? 4ULL * 1024ULL * 1024ULL
                             : maxRangeBytes),
          maxSingleFileBytes_(maxSingleFileBytes)
    {
        descriptor_.canonicalFormat = FdclFileListFormat;
    }

    TransferSourceId id() const override
    {
        return sourceId_;
    }

    std::vector<TransferFormatDescriptor> formats() const override
    {
        return {descriptor_};
    }

    TransferReadResult read(const TransferReadRequest& request) override
    {
        FileGroupTransferSource source(sourceId_, descriptor_, fileList_);
        return source.read(request);
    }

    TransferFileRangeResult readFileRange(
        const TransferFileRangeRequest& request) override
    {
        TransferFileRangeResult result;
        if (request.sourceId != 0 && request.sourceId != sourceId_) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "qt file source id is not found";
            return result;
        }
        if (request.fileIndex >= paths_.size() ||
            request.fileIndex >= fileList_.files.size()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "qt file index is not found";
            return result;
        }

        const TransferFileDescriptor& descriptor =
            fileList_.files[request.fileIndex];
        if (request.objectId != descriptor.objectId) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "qt file object is not found";
            return result;
        }
        if (descriptor.directory) {
            result.status = protocol::ResponseStatus::Unsupported;
            result.message = "directory file contents are unsupported";
            return result;
        }
        if (paths_[request.fileIndex].empty()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "qt file path is not available";
            return result;
        }
        if (maxSingleFileBytes_ != 0 &&
            descriptor.sizeBytes > maxSingleFileBytes_) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "qt file exceeds max single file bytes";
            return result;
        }
        if (request.requestedBytes == 0 ||
            request.requestedBytes > maxRangeBytes_) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "qt file range request is too large";
            return result;
        }
        if (request.offset >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())) {
            result.status = protocol::ResponseStatus::InvalidArgument;
            result.message = "qt file range offset is invalid";
            return result;
        }

        std::ifstream file(paths_[request.fileIndex], std::ios::binary);
        if (!file.is_open()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "qt file cannot be opened";
            return result;
        }

        file.seekg(static_cast<std::streamoff>(request.offset), std::ios::beg);
        if (!file.good()) {
            result.status = protocol::ResponseStatus::InvalidArgument;
            result.message = "qt file range offset is invalid";
            return result;
        }

        const std::uint64_t wanted64 =
            std::min(request.requestedBytes, maxRangeBytes_);
        const std::uint64_t maxStreamBytes = static_cast<std::uint64_t>(
            std::numeric_limits<std::streamsize>::max());
        const std::size_t wanted = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                std::min(wanted64, maxStreamBytes),
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())));
        protocol::ByteBuffer buffer(wanted);
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readBytes = file.gcount();
        if (readBytes < 0) {
            result.status = protocol::ResponseStatus::Failed;
            result.message = "qt file read failed";
            return result;
        }

        buffer.resize(static_cast<std::size_t>(readBytes));
        result.status = protocol::ResponseStatus::Ok;
        result.bytes = std::move(buffer);
        const std::uint64_t received =
            static_cast<std::uint64_t>(result.bytes.size());
        result.endOfFile =
            received < wanted64 ||
            request.offset + received >= descriptor.sizeBytes;
        return result;
    }

private:
    TransferSourceId sourceId_ = 0;
    TransferFormatDescriptor descriptor_;
    TransferFileList fileList_;
    std::vector<std::filesystem::path> paths_;
    std::uint64_t maxRangeBytes_ = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t maxSingleFileBytes_ = 0;
};

} // namespace

bool readQtLocalFileUrlList(TransferFileList& output,
                            std::vector<std::filesystem::path>* paths,
                            const QList<QUrl>& urls,
                            const QtClipboardLocalFileOptions& options)
{
    output.files.clear();
    if (paths != nullptr)
        paths->clear();
    if (options.maxFileCount == 0 || urls.empty())
        return false;

    for (const QUrl& url : urls) {
        if (output.files.size() >= options.maxFileCount)
            break;
        if (!url.isLocalFile())
            continue;

        const std::filesystem::path path(url.toLocalFile().toStdString());
        if (path.empty())
            continue;

        appendPathToFileList(output,
                             paths,
                             path,
                             displayNameFromPath(path),
                             options,
                             0);
    }

    return !output.files.empty();
}

std::shared_ptr<TransferSource> createLocalQtFileTransferSource(
    TransferSourceId sourceId,
    TransferFormatDescriptor descriptor,
    TransferFileList fileList,
    std::vector<std::filesystem::path> paths,
    std::uint64_t maxRangeBytes,
    std::uint64_t maxSingleFileBytes)
{
    return std::make_shared<LocalQtFileTransferSource>(
        sourceId,
        std::move(descriptor),
        std::move(fileList),
        std::move(paths),
        maxRangeBytes,
        maxSingleFileBytes);
}

} // namespace clipboard
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
