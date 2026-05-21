#include "fusiondesk/platform/linux/clipboard/linux_clipboard_endpoint.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <clipbus.h>
#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
#include <fuse-promise/fuse-promise.h>
#endif

namespace fusiondesk {
namespace platform {
namespace linux_desktop {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

constexpr const char* LinuxTextPlainTarget = "text/plain;charset=utf-8";
constexpr const char* LinuxUtf8StringTarget = "UTF8_STRING";
constexpr const char* LinuxTextPlainLooseTarget = "text/plain";
constexpr const char* LinuxUriListTarget = "text/uri-list";
constexpr const char* LinuxGnomeCopiedFilesTarget =
    "x-special/gnome-copied-files";
constexpr const char* LinuxMateCopiedFilesTarget =
    "x-special/mate-copied-files";
constexpr const char* LinuxXMozUrlTarget = "text/x-moz-url";
constexpr const char* LinuxNautilusClipboardHeader =
    "x-special/nautilus-clipboard";

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

std::string stringFromBytes(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

std::string lowered(std::string value)
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool sameTargetName(const std::string& left, const std::string& right)
{
    return lowered(left) == lowered(right);
}

bool containsTarget(const std::vector<std::string>& targets,
                    const std::string& target)
{
    return std::any_of(targets.begin(),
                       targets.end(),
                       [&target](const std::string& current) {
                           return sameTargetName(current, target);
                       });
}

void appendUniqueTarget(std::vector<std::string>& targets, std::string target)
{
    if (target.empty() || containsTarget(targets, target))
        return;
    targets.push_back(std::move(target));
}

bool descriptorIsFileList(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == FdclFileListFormat ||
           sameTargetName(descriptor.nativeFormatName, LinuxUriListTarget) ||
           sameTargetName(descriptor.nativeFormatName,
                          LinuxGnomeCopiedFilesTarget) ||
           sameTargetName(descriptor.nativeFormatName,
                          LinuxMateCopiedFilesTarget) ||
           sameTargetName(descriptor.nativeFormatName, LinuxXMozUrlTarget);
}

clipbus_backend_t toClipbusBackend(LinuxClipboardBackend backend)
{
    switch (backend) {
    case LinuxClipboardBackend::X11:
        return CLIPBUS_BACKEND_X11;
    case LinuxClipboardBackend::Fake:
        return CLIPBUS_BACKEND_FAKE;
    case LinuxClipboardBackend::Auto:
    default:
        return CLIPBUS_BACKEND_AUTO;
    }
}

protocol::ResponseStatus responseFromClipbus(clipbus_status_t status)
{
    switch (status) {
    case CLIPBUS_OK:
        return protocol::ResponseStatus::Ok;
    case CLIPBUS_PENDING:
        return protocol::ResponseStatus::Accepted;
    case CLIPBUS_UNSUPPORTED:
        return protocol::ResponseStatus::Unsupported;
    case CLIPBUS_TIMEOUT:
        return protocol::ResponseStatus::Timeout;
    case CLIPBUS_INVALID_ARGUMENT:
        return protocol::ResponseStatus::InvalidArgument;
    case CLIPBUS_ALREADY_STARTED:
        return protocol::ResponseStatus::Conflict;
    case CLIPBUS_NOT_STARTED:
        return protocol::ResponseStatus::ChannelUnavailable;
    case CLIPBUS_NOT_FOUND:
        return protocol::ResponseStatus::NotFound;
    case CLIPBUS_PLATFORM_ERROR:
        return protocol::ResponseStatus::Failed;
    case CLIPBUS_PANIC:
    default:
        return protocol::ResponseStatus::InternalError;
    }
}

clipbus_status_t clipbusFromResponse(protocol::ResponseStatus status)
{
    switch (status) {
    case protocol::ResponseStatus::Ok:
        return CLIPBUS_OK;
    case protocol::ResponseStatus::Accepted:
    case protocol::ResponseStatus::Progress:
        return CLIPBUS_PENDING;
    case protocol::ResponseStatus::Unsupported:
        return CLIPBUS_UNSUPPORTED;
    case protocol::ResponseStatus::Timeout:
        return CLIPBUS_TIMEOUT;
    case protocol::ResponseStatus::InvalidArgument:
    case protocol::ResponseStatus::TooLarge:
        return CLIPBUS_INVALID_ARGUMENT;
    case protocol::ResponseStatus::NotFound:
        return CLIPBUS_NOT_FOUND;
    default:
        return CLIPBUS_PLATFORM_ERROR;
    }
}

bool isHexDigit(char ch)
{
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

int hexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

std::string percentDecodePath(std::string value)
{
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' &&
            index + 2 < value.size() &&
            isHexDigit(value[index + 1]) &&
            isHexDigit(value[index + 2])) {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            output.push_back(static_cast<char>((high << 4) | low));
            index += 2;
            continue;
        }
        output.push_back(value[index]);
    }
    return output;
}

bool fileUriToPath(const std::string& uri, std::filesystem::path* path)
{
    constexpr const char* prefix = "file://";
    if (uri.rfind(prefix, 0) != 0)
        return false;

    std::string rest = uri.substr(std::strlen(prefix));
    std::string pathPart;
    if (!rest.empty() && rest.front() == '/') {
        pathPart = std::move(rest);
    } else {
        const std::size_t slash = rest.find('/');
        if (slash == std::string::npos)
            return false;
        const std::string host = lowered(rest.substr(0, slash));
        if (!host.empty() && host != "localhost")
            return false;
        pathPart = rest.substr(slash);
    }
    if (pathPart.empty() || pathPart.front() != '/')
        return false;

    *path = std::filesystem::path(percentDecodePath(pathPart));
    return true;
}

std::vector<std::filesystem::path> fileUriListPaths(
    const protocol::ByteBuffer& bytes)
{
    std::vector<std::filesystem::path> paths;
    const std::string text = stringFromBytes(bytes);
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const std::size_t next = text.find('\n', offset);
        std::string line = text.substr(
            offset,
            next == std::string::npos ? std::string::npos : next - offset);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
            line.pop_back();
        if (!line.empty() && line.front() != '#') {
            const std::string loweredLine = lowered(line);
            if (loweredLine != "copy" && loweredLine != "cut") {
                std::filesystem::path path;
                if (fileUriToPath(line, &path) && !path.empty())
                    paths.push_back(std::move(path));
            }
        }
        if (next == std::string::npos)
            break;
        offset = next + 1;
    }
    return paths;
}

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

std::string firstRelativePathSegment(const std::string& relativePath)
{
    std::filesystem::path path(relativePath);
    for (const std::filesystem::path& part : path) {
        const std::string value = part.generic_u8string();
        if (!value.empty() && value != ".")
            return sanitizeTransferFileDisplayName(value);
    }
    return {};
}

bool isUriPathByteAllowed(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '/' ||
           ch == '-' ||
           ch == '_' ||
           ch == '.' ||
           ch == '~';
}

std::string fileUriFromPath(const std::filesystem::path& path)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    const std::string value = path.generic_u8string();
    std::string output = "file://";
    output.reserve(output.size() + value.size());
    for (unsigned char ch : value) {
        if (isUriPathByteAllowed(ch)) {
            output.push_back(static_cast<char>(ch));
        } else {
            output.push_back('%');
            output.push_back(hex[(ch >> 4U) & 0x0fU]);
            output.push_back(hex[ch & 0x0fU]);
        }
    }
    return output;
}

std::vector<std::string> promiseClipboardUris(
    const std::filesystem::path& promiseRoot,
    const std::vector<std::string>& promiseRootEntries)
{
    std::vector<std::string> uris;
    std::set<std::string> emitted;
    for (const std::string& entry : promiseRootEntries) {
        if (entry.empty() || !emitted.insert(entry).second)
            continue;
        uris.push_back(fileUriFromPath(promiseRoot / entry));
    }
    return uris;
}

protocol::ByteBuffer uriListBytes(const std::vector<std::string>& uris)
{
    std::string text;
    for (const std::string& uri : uris) {
        text += uri;
        text += "\r\n";
    }
    return protocol::ByteBuffer(text.begin(), text.end());
}

protocol::ByteBuffer copiedFilesBytes(const std::vector<std::string>& uris)
{
    std::string text = "copy\n";
    for (const std::string& uri : uris) {
        text += uri;
        text += "\n";
    }
    return protocol::ByteBuffer(text.begin(), text.end());
}

protocol::ByteBuffer nautilusClipboardBytes(
    const std::vector<std::string>& uris)
{
    std::string text = std::string(LinuxNautilusClipboardHeader) + "\ncopy\n";
    for (const std::string& uri : uris) {
        text += uri;
        text += "\n";
    }
    return protocol::ByteBuffer(text.begin(), text.end());
}

bool bytesStartWithLine(const protocol::ByteBuffer& bytes,
                        const std::string& expectedLine)
{
    if (bytes.size() < expectedLine.size())
        return false;
    if (!std::equal(expectedLine.begin(), expectedLine.end(), bytes.begin()))
        return false;
    if (bytes.size() == expectedLine.size())
        return true;
    const char next = static_cast<char>(bytes[expectedLine.size()]);
    return next == '\n' || next == '\r' || next == '\0';
}

std::int64_t mtimeNsecFromUnixUsec(std::uint64_t usec)
{
    constexpr std::uint64_t maxNsec =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (usec > maxNsec / 1000ULL)
        return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(usec * 1000ULL);
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
                          const LinuxClipboardEndpointOptions& options,
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
    if (paths != nullptr) {
        paths->push_back(output.files.back().directory
                             ? std::filesystem::path()
                             : path);
    }

    if (!output.files.back().directory ||
        !options.expandDroppedDirectories ||
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

bool readLocalFileUriList(TransferFileList& output,
                          std::vector<std::filesystem::path>* paths,
                          const protocol::ByteBuffer& bytes,
                          const LinuxClipboardEndpointOptions& options)
{
    output.files.clear();
    if (paths != nullptr)
        paths->clear();
    if (options.maxFileCount == 0)
        return false;

    const std::vector<std::filesystem::path> uris = fileUriListPaths(bytes);
    if (uris.empty())
        return false;

    for (const std::filesystem::path& path : uris) {
        if (output.files.size() >= options.maxFileCount)
            break;
        appendPathToFileList(output,
                             paths,
                             path,
                             displayNameFromPath(path),
                             options,
                             0);
    }
    return !output.files.empty();
}

#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
fp_status_t responseToFusePromise(protocol::ResponseStatus status)
{
    switch (status) {
    case protocol::ResponseStatus::Ok:
    case protocol::ResponseStatus::Accepted:
    case protocol::ResponseStatus::Progress:
        return FP_OK;
    case protocol::ResponseStatus::InvalidArgument:
    case protocol::ResponseStatus::TooLarge:
    case protocol::ResponseStatus::ProtocolError:
        return FP_ERR_INVALID_ARGUMENT;
    case protocol::ResponseStatus::Unauthorized:
    case protocol::ResponseStatus::DeniedByPolicy:
        return FP_ERR_PERMISSION;
    case protocol::ResponseStatus::NotFound:
    case protocol::ResponseStatus::Conflict:
        return FP_ERR_NOT_FOUND;
    case protocol::ResponseStatus::Timeout:
        return FP_ERR_TIMEOUT;
    case protocol::ResponseStatus::Cancelled:
        return FP_ERR_CANCELLED;
    case protocol::ResponseStatus::ChannelUnavailable:
    case protocol::ResponseStatus::Unsupported:
    case protocol::ResponseStatus::Busy:
    case protocol::ResponseStatus::BackPressure:
        return FP_ERR_UNAVAILABLE;
    case protocol::ResponseStatus::Failed:
    case protocol::ResponseStatus::InternalError:
    default:
        return FP_ERR_IO;
    }
}
#endif

class LocalLinuxFileTransferSource final
    : public TransferSource,
      public ITransferFileContentProvider
{
public:
    LocalLinuxFileTransferSource(TransferSourceId sourceId,
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
                             ? DefaultTransferFileRangeChunkBytes
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
            result.message = "linux file source id is not found";
            return result;
        }
        if (request.fileIndex >= paths_.size() ||
            request.fileIndex >= fileList_.files.size()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "linux file index is not found";
            return result;
        }

        const TransferFileDescriptor& descriptor =
            fileList_.files[request.fileIndex];
        if (request.objectId != descriptor.objectId) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "linux file object is not found";
            return result;
        }
        if (descriptor.directory) {
            result.status = protocol::ResponseStatus::Unsupported;
            result.message = "directory file contents are unsupported";
            return result;
        }
        if (paths_[request.fileIndex].empty()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "linux file path is not available";
            return result;
        }
        if (maxSingleFileBytes_ != 0 &&
            descriptor.sizeBytes > maxSingleFileBytes_) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "linux file exceeds max single file bytes";
            return result;
        }
        if (request.requestedBytes == 0 ||
            request.requestedBytes > maxRangeBytes_) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "linux file range request is too large";
            return result;
        }
        if (request.offset >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())) {
            result.status = protocol::ResponseStatus::InvalidArgument;
            result.message = "linux file range offset is invalid";
            return result;
        }

        std::ifstream file(paths_[request.fileIndex], std::ios::binary);
        if (!file.is_open()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "linux file cannot be opened";
            return result;
        }

        file.seekg(static_cast<std::streamoff>(request.offset), std::ios::beg);
        if (!file.good()) {
            result.status = protocol::ResponseStatus::InvalidArgument;
            result.message = "linux file range offset is invalid";
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
            result.message = "linux file read failed";
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
    std::uint64_t maxRangeBytes_ = DefaultTransferFileRangeChunkBytes;
    std::uint64_t maxSingleFileBytes_ = 0;
};

class LinuxRemoteObjectLockGuard final
{
public:
    ~LinuxRemoteObjectLockGuard()
    {
        unlockSilently();
    }

    LinuxRemoteObjectLockGuard() = default;
    LinuxRemoteObjectLockGuard(const LinuxRemoteObjectLockGuard&) = delete;
    LinuxRemoteObjectLockGuard& operator=(const LinuxRemoteObjectLockGuard&) = delete;

    protocol::ResponseStatus lock(IClipboardRemoteObjectLocker* locker,
                                  TransferObjectLockRequest request,
                                  std::uint32_t timeoutMs,
                                  std::string* message)
    {
        locker_ = locker;
        request_ = request;
        timeoutMs_ = timeoutMs;
        if (locker_ == nullptr)
            return protocol::ResponseStatus::Ok;

        const TransferObjectLockResult result =
            locker_->lockRemoteObject(request_, timeoutMs_);
        if (!result.ok()) {
            if (message != nullptr)
                *message = result.message;
            return result.status;
        }
        if (result.lockId == 0) {
            if (message != nullptr)
                *message = "linux remote file object lock returned empty id";
            return protocol::ResponseStatus::Failed;
        }

        request_.lockId = result.lockId;
        request_.leaseUsec = result.leaseUsec;
        locked_ = true;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus unlock(std::string* message)
    {
        if (!locked_ || locker_ == nullptr)
            return protocol::ResponseStatus::Ok;

        const TransferObjectLockResult result =
            locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
        if (result.ok())
            return protocol::ResponseStatus::Ok;

        if (message != nullptr)
            *message = result.message;
        return result.status;
    }

private:
    void unlockSilently()
    {
        if (!locked_ || locker_ == nullptr)
            return;

        locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
    }

    IClipboardRemoteObjectLocker* locker_ = nullptr;
    TransferObjectLockRequest request_;
    std::uint32_t timeoutMs_ = 0;
    bool locked_ = false;
};

} // namespace

class LinuxClipboardEndpoint::Impl final
{
public:
    Impl(LinuxClipboardEndpointOptions options,
         std::shared_ptr<IClipboardRemoteReader> remoteReader,
         std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader,
         std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker,
         std::shared_ptr<ITransferTranscoder> transcoder,
         std::shared_ptr<IClipboardCallbackDispatcher> callbackDispatcher)
        : options_(std::move(options)),
          remoteReader_(std::move(remoteReader)),
          remoteFileReader_(std::move(remoteFileReader)),
          remoteObjectLocker_(std::move(remoteObjectLocker)),
          transcoder_(std::move(transcoder)),
          callbackDispatcher_(std::move(callbackDispatcher)),
          nextBundleId_(options_.firstBundleId),
          nextOfferId_(options_.firstOfferId),
          nextSourceId_(options_.firstSourceId),
          nextFormatId_(options_.firstFormatId)
    {
#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
        diagnostics_.fusePromiseAvailable = true;
#endif
        if (remoteFileReader_ == nullptr)
            remoteFileReader_ =
                std::dynamic_pointer_cast<IClipboardRemoteFileReader>(
                    remoteReader_);
        if (remoteObjectLocker_ == nullptr)
            remoteObjectLocker_ =
                std::dynamic_pointer_cast<IClipboardRemoteObjectLocker>(
                    remoteReader_);
        if (transcoder_ == nullptr)
            transcoder_ = std::make_shared<DefaultTransferTranscoder>();
        startClipbus();
    }

    ~Impl()
    {
        clipbus_clipboard_t* clipboard = nullptr;
        std::shared_ptr<RemoteFilePromisePublication> publication;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shuttingDown_ = true;
            publication = std::move(remoteFilePublication_);
            clipboard = clipboard_;
            clipboard_ = nullptr;
            started_ = false;
            diagnostics_.started = false;
            diagnostics_.fusePromiseActive = false;
            pendingStreams_.clear();
            renderedTargetCache_.clear();
        }
        destroyRemoteFilePublication(publication);
        if (clipboard != nullptr) {
            clipbus_clipboard_stop(clipboard);
            clipbus_clipboard_destroy(clipboard);
        }
    }

    ClipboardSnapshot snapshot()
    {
        std::optional<TransferSourceBundle> ownedBundle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.snapshots;
            if (!started_ || clipboard_ == nullptr) {
                diagnostics_.lastMessage = "linux clipboard backend is not started";
                return {};
            }
            if (publishedBundle_.offerId != 0 && !pendingNativeClipboardChange_)
                ownedBundle = publishedBundle_;
        }

        if (ownedBundle.has_value())
            return snapshotFromPublishedBundle(std::move(*ownedBundle));

        const TargetListResult targets = requestTargets();
        if (targets.status != CLIPBUS_OK) {
            setClipbusMessage(targets.status, "linux clipboard target list read failed");
            return {};
        }

        const std::optional<std::string> fileTarget =
            findNativeTargetForCanonical(targets.targets, FdclFileListFormat);
        if (fileTarget.has_value()) {
            const TargetDataResult data =
                requestTargetData(*fileTarget, options_.maxInlineBytes);
            if (data.status == CLIPBUS_OK && !data.bytes.empty()) {
                TransferFileList fileList;
                std::vector<std::filesystem::path> paths;
                if (readLocalFileUriList(fileList, &paths, data.bytes, options_)) {
                    return snapshotFromFileList(fileList,
                                                std::move(paths),
                                                *fileTarget,
                                                nextNativeSequence());
                }
            }
        }

        const auto utf8Target = std::find_if(
            targets.targets.begin(),
            targets.targets.end(),
            [](const std::string& target) {
                return sameTargetName(target, LinuxUtf8StringTarget);
            });
        if (utf8Target != targets.targets.end()) {
            const TargetDataResult data =
                requestTargetData(*utf8Target, options_.maxInlineBytes);
            if (data.status == CLIPBUS_OK &&
                bytesStartWithLine(data.bytes, LinuxNautilusClipboardHeader)) {
                TransferFileList fileList;
                std::vector<std::filesystem::path> paths;
                if (readLocalFileUriList(fileList, &paths, data.bytes, options_)) {
                    return snapshotFromFileList(fileList,
                                                std::move(paths),
                                                *utf8Target,
                                                nextNativeSequence());
                }
            }
        }

        std::vector<MaterializedTransferEntry> entries;
        appendSnapshotEntry(targets.targets, ImagePngFormat, entries);
        appendSnapshotEntry(targets.targets, TextHtmlFormat, entries);
        appendSnapshotEntry(targets.targets, TextRtfFormat, entries);
        appendSnapshotEntry(targets.targets, TextPlainUtf8Format, entries);
        if (entries.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            diagnostics_.lastMessage = "linux clipboard has no supported targets";
            return {};
        }

        return snapshotFromEntries(std::move(entries), nextNativeSequence());
    }

    ClipboardSnapshot snapshotFromPublishedBundle(TransferSourceBundle bundle)
    {
        std::vector<std::string> nativeTargets;
        bool hasFileList = false;
        buildNativeTargetsForBundle(bundle, nativeTargets, &hasFileList);

        std::vector<MaterializedTransferEntry> entries;
        appendPublishedSnapshotEntry(bundle,
                                     nativeTargets,
                                     ImagePngFormat,
                                     entries);
        appendPublishedSnapshotEntry(bundle,
                                     nativeTargets,
                                     TextHtmlFormat,
                                     entries);
        appendPublishedSnapshotEntry(bundle,
                                     nativeTargets,
                                     TextRtfFormat,
                                     entries);
        appendPublishedSnapshotEntry(bundle,
                                     nativeTargets,
                                     TextPlainUtf8Format,
                                     entries);
        appendPublishedSnapshotEntry(bundle,
                                     nativeTargets,
                                     FdclFileListFormat,
                                     entries);
        if (entries.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            diagnostics_.lastMessage =
                hasFileList
                    ? "linux published file clipboard is not materialized"
                    : "linux published clipboard has no readable formats";
            return {};
        }

        return snapshotFromEntries(std::move(entries),
                                   bundle.sequence == 0
                                       ? nextNativeSequence()
                                       : bundle.sequence);
    }

    void appendPublishedSnapshotEntry(
        const TransferSourceBundle& bundle,
        const std::vector<std::string>& nativeTargets,
        const std::string& canonicalFormat,
        std::vector<MaterializedTransferEntry>& entries)
    {
        const std::optional<std::string> target =
            findNativeTargetForCanonical(nativeTargets, canonicalFormat);
        if (!target.has_value())
            return;

        TransferReadResult read =
            readBestFormat(bundle, canonicalFormat, *target);
        if (!read.ok() || read.bytes.empty())
            return;

        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = canonicalFormat;
        descriptor.nativeFormatName = *target;
        descriptor.localFormatToken = 0;
        descriptor.formatId = nextFormatId();
        descriptor.itemIndex = static_cast<std::uint32_t>(entries.size());
        descriptor.estimatedBytes = read.bytes.size();
        descriptor.canInline = true;
        descriptor.canStream = false;
        descriptor.preferredEncoding = read.encoding;

        MaterializedTransferEntry entry;
        entry.descriptor = std::move(descriptor);
        entry.bytes = std::move(read.bytes);
        entries.push_back(std::move(entry));
    }

    protocol::ResponseStatus publishBundle(
        const ClipboardPublishRequest& request)
    {
        const protocol::ResponseStatus fileStatus =
            publishRemoteFileListBundle(request.bundle);
        if (fileStatus != protocol::ResponseStatus::NotFound)
            return fileStatus;

        std::vector<std::string> nativeTargets;
        bool hasFileList = false;
        buildNativeTargetsForBundle(request.bundle, nativeTargets, &hasFileList);
        if (nativeTargets.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (hasFileList) {
                ++diagnostics_.remoteFilePromiseFailures;
                diagnostics_.lastMessage =
                    "linux remote file publication needs fuse-promise";
                return protocol::ResponseStatus::Unsupported;
            }
            diagnostics_.lastMessage =
                "linux clipboard bundle has no supported formats";
            return protocol::ResponseStatus::NotFound;
        }

        std::vector<clipbus_target_offer_t> offers;
        offers.reserve(nativeTargets.size());
        for (const std::string& target : nativeTargets) {
            clipbus_target_offer_t offer = {};
            offer.native_target = target.c_str();
            offer.is_promised = 1;
            offer.max_bytes = maxBytesForNativeTarget(request.bundle, target);
            offers.push_back(offer);
        }

        clipbus_clipboard_t* clipboard = nullptr;
        std::shared_ptr<RemoteFilePromisePublication> oldPublication;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_ || clipboard_ == nullptr) {
                diagnostics_.lastMessage = "linux clipboard backend is not started";
                return protocol::ResponseStatus::Failed;
            }
            publishedBundle_ = request.bundle;
            oldPublication = std::move(remoteFilePublication_);
            diagnostics_.fusePromiseActive = false;
            pendingStreams_.clear();
            renderedTargetCache_.clear();
            clipboard = clipboard_;
        }
        destroyRemoteFilePublication(oldPublication);

        const clipbus_status_t status = clipbus_clipboard_publish_targets(
            clipboard,
            offers.data(),
            offers.size());
        if (status != CLIPBUS_OK) {
            setClipbusMessage(status, "linux clipboard publish failed");
            return responseFromClipbus(status);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        diagnostics_.publishedOfferId = request.bundle.offerId;
        diagnostics_.lastNativeSequence = request.bundle.sequence;
        diagnostics_.lastClipbusStatus = status;
        diagnostics_.lastMessage = "linux clipboard targets published";
        ++diagnostics_.publishes;
        ++diagnostics_.delayedPublishes;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus clearPublishedBundle(TransferOfferId offerId)
    {
        clipbus_clipboard_t* clipboard = nullptr;
        std::shared_ptr<RemoteFilePromisePublication> publication;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (diagnostics_.publishedOfferId != 0 &&
                offerId != 0 &&
                diagnostics_.publishedOfferId != offerId) {
                return protocol::ResponseStatus::Conflict;
            }
            if (!started_ || clipboard_ == nullptr) {
                diagnostics_.lastMessage = "linux clipboard backend is not started";
                return protocol::ResponseStatus::Failed;
            }
            publishedBundle_ = {};
            publication = std::move(remoteFilePublication_);
            diagnostics_.fusePromiseActive = false;
            pendingStreams_.clear();
            renderedTargetCache_.clear();
            suppressNextOwnerLost_ = true;
            suppressNextTargetsChanged_ = true;
            clipboard = clipboard_;
        }
        destroyRemoteFilePublication(publication);

        const clipbus_status_t status = clipbus_clipboard_clear(clipboard);
        if (status != CLIPBUS_OK) {
            setClipbusMessage(status, "linux clipboard clear failed");
            return responseFromClipbus(status);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        diagnostics_.publishedOfferId = 0;
        diagnostics_.lastClipbusStatus = status;
        diagnostics_.lastMessage = "linux clipboard cleared";
        ++diagnostics_.clears;
        return protocol::ResponseStatus::Ok;
    }

    bool hasPendingClipboardChange() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pendingNativeClipboardChange_;
    }

    void markClipboardChangeConsumed()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
    }

    LinuxClipboardEndpointDiagnostics diagnostics() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LinuxClipboardEndpointDiagnostics copy = diagnostics_;
        copy.started = started_;
        copy.nativeChangePending = pendingNativeClipboardChange_;
        return copy;
    }

private:
    struct TargetListResult
    {
        bool completed = false;
        clipbus_status_t status = CLIPBUS_TIMEOUT;
        std::vector<std::string> targets;
    };

    struct TargetDataResult
    {
        bool completed = false;
        clipbus_status_t status = CLIPBUS_TIMEOUT;
        std::string nativeTarget;
        protocol::ByteBuffer bytes;
    };

    struct PendingStream
    {
        std::string nativeTarget;
        protocol::ByteBuffer bytes;
        std::size_t offset = 0;
    };

    struct RemoteFilePromiseNode
    {
        TransferFileDescriptor descriptor;
        TransferFileRangeRequest rangeRequest;
        TransferObjectLockRequest lockRequest;
    };

    struct RemoteFilePromisePublication
    {
        TransferOfferId offerId = 0;
        TransferBundleId bundleId = 0;
        std::uint64_t ownerEpoch = 0;
        std::filesystem::path promiseRoot;
        protocol::ByteBuffer uriList;
        protocol::ByteBuffer copiedFiles;
        protocol::ByteBuffer nautilusClipboard;
        std::unordered_map<std::string, RemoteFilePromiseNode> nodes;
#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
        fp_context_t* context = nullptr;
        fp_provider_t* provider = nullptr;
#endif
    };

    void startClipbus()
    {
        clipbus_options_t options = {};
        options.abi_version = CLIPBUS_ABI_VERSION;
        options.backend = toClipbusBackend(options_.backend);
        options.display_name =
            options_.x11DisplayName.empty() ? nullptr
                                            : options_.x11DisplayName.c_str();
        options.request_timeout_ms = options_.requestTimeoutMs;
        options.max_inline_bytes = options_.maxInlineBytes;
        options.owner_window_name = options_.ownerWindowName.empty()
                                        ? nullptr
                                        : options_.ownerWindowName.c_str();

        clipbus_callbacks_t callbacks = {};
        callbacks.abi_version = CLIPBUS_ABI_VERSION;
        callbacks.target_request = &Impl::onTargetRequest;
        callbacks.targets_changed = &Impl::onTargetsChanged;
        callbacks.owner_lost = &Impl::onOwnerLost;
        callbacks.error = &Impl::onError;
        callbacks.target_list = &Impl::onTargetList;
        callbacks.target_data = &Impl::onTargetData;
        callbacks.stream_ready = &Impl::onStreamReady;

        clipbus_clipboard_t* clipboard = nullptr;
        clipbus_status_t status =
            clipbus_clipboard_create(&options, &callbacks, this, &clipboard);
        if (status != CLIPBUS_OK) {
            setClipbusMessage(status, "linux clipboard create failed");
            return;
        }

        status = clipbus_clipboard_start(clipboard);
        if (status != CLIPBUS_OK) {
            clipbus_clipboard_destroy(clipboard);
            setClipbusMessage(status, "linux clipboard start failed");
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        clipboard_ = clipboard;
        started_ = true;
        pendingNativeClipboardChange_ = options_.enableChangeMonitor;
        diagnostics_.started = true;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        diagnostics_.lastClipbusStatus = status;
        diagnostics_.lastMessage = "linux clipboard backend started";
    }

    TargetListResult requestTargets()
    {
        TargetListResult result;
        clipbus_clipboard_t* clipboard = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clipboard = clipboard_;
        }
        if (clipboard == nullptr) {
            result.status = CLIPBUS_NOT_STARTED;
            return result;
        }

        clipbus_request_id_t requestId = 0;
        const clipbus_status_t status =
            clipbus_clipboard_request_targets(clipboard, &requestId);
        if (status != CLIPBUS_OK) {
            result.status = status;
            return result;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        auto completed = completedTargetLists_.find(requestId);
        if (completed != completedTargetLists_.end()) {
            result = std::move(completed->second);
            completedTargetLists_.erase(completed);
        } else {
            pendingTargetLists_.emplace(requestId, TargetListResult{});
            const bool ready = condition_.wait_for(
                lock,
                std::chrono::milliseconds(options_.requestTimeoutMs),
                [this, requestId]() {
                    const auto it = pendingTargetLists_.find(requestId);
                    return it != pendingTargetLists_.end() &&
                           it->second.completed;
                });
            auto it = pendingTargetLists_.find(requestId);
            if (ready && it != pendingTargetLists_.end())
                result = std::move(it->second);
            else
                result.status = CLIPBUS_TIMEOUT;
            if (it != pendingTargetLists_.end())
                pendingTargetLists_.erase(it);
        }

        if (result.status == CLIPBUS_OK)
            ++diagnostics_.targetListReads;
        else
            ++diagnostics_.targetReadFailures;
        return result;
    }

    TargetDataResult requestTargetData(const std::string& nativeTarget,
                                       std::uint64_t maxBytes)
    {
        TargetDataResult result;
        result.nativeTarget = nativeTarget;
        clipbus_clipboard_t* clipboard = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clipboard = clipboard_;
        }
        if (clipboard == nullptr) {
            result.status = CLIPBUS_NOT_STARTED;
            return result;
        }

        clipbus_request_id_t requestId = 0;
        const clipbus_status_t status =
            clipbus_clipboard_request_target_data(clipboard,
                                                  nativeTarget.c_str(),
                                                  maxBytes,
                                                  &requestId);
        if (status != CLIPBUS_OK) {
            result.status = status;
            return result;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        auto completed = completedTargetData_.find(requestId);
        if (completed != completedTargetData_.end()) {
            result = std::move(completed->second);
            completedTargetData_.erase(completed);
        } else {
            pendingTargetData_.emplace(requestId, TargetDataResult{});
            const bool ready = condition_.wait_for(
                lock,
                std::chrono::milliseconds(options_.requestTimeoutMs),
                [this, requestId]() {
                    const auto it = pendingTargetData_.find(requestId);
                    return it != pendingTargetData_.end() &&
                           it->second.completed;
                });
            auto it = pendingTargetData_.find(requestId);
            if (ready && it != pendingTargetData_.end())
                result = std::move(it->second);
            else
                result.status = CLIPBUS_TIMEOUT;
            if (it != pendingTargetData_.end())
                pendingTargetData_.erase(it);
        }

        if (result.status == CLIPBUS_OK)
            ++diagnostics_.targetDataReads;
        else
            ++diagnostics_.targetReadFailures;
        return result;
    }

    bool runCallbackTaskAndWait(ClipboardCallbackTask task,
                                std::uint32_t timeoutMs)
    {
        if (!task)
            return false;
        if (callbackDispatcher_ == nullptr) {
            task();
            return true;
        }
        return callbackDispatcher_->runClipboardTaskAndWait(std::move(task),
                                                            timeoutMs);
    }

    TransferReadResult readRemoteFormat(
        const TransferReadRequest& request,
        std::uint32_t timeoutMs)
    {
        if (remoteReader_ == nullptr) {
            TransferReadResult result;
            result.status = protocol::ResponseStatus::Unsupported;
            result.message = "linux clipboard remote reader is unavailable";
            return result;
        }

        auto sharedResult = std::make_shared<TransferReadResult>();
        const bool completed = runCallbackTaskAndWait(
            [this, request, timeoutMs, sharedResult]() {
                *sharedResult =
                    remoteReader_->readRemoteFormat(request, timeoutMs);
            },
            timeoutMs);
        if (!completed) {
            TransferReadResult result;
            result.status = protocol::ResponseStatus::Timeout;
            result.message = "linux clipboard remote read dispatcher timed out";
            return result;
        }
        return *sharedResult;
    }

    std::optional<std::string> canonicalForNativeTarget(
        const std::string& nativeTarget) const
    {
        TransferFormatMappingRequest request;
        request.native.platform = TransferPlatformFamily::Linux;
        request.native.nativeFormatName = nativeTarget;
        const TransferFormatMappingResult mapped =
            mapper_.mapNativeToCanonical(request);
        if (!mapped.mapped)
            return std::nullopt;
        return mapped.descriptor.canonicalFormat;
    }

    std::optional<std::string> findNativeTargetForCanonical(
        const std::vector<std::string>& targets,
        const std::string& canonicalFormat) const
    {
        std::vector<std::string> preferred;
        if (canonicalFormat == FdclFileListFormat) {
            preferred.push_back(LinuxGnomeCopiedFilesTarget);
            preferred.push_back(LinuxMateCopiedFilesTarget);
            preferred.push_back(LinuxUriListTarget);
            preferred.push_back(LinuxXMozUrlTarget);
        } else if (canonicalFormat == TextPlainUtf8Format) {
            preferred.push_back(LinuxTextPlainTarget);
            preferred.push_back(LinuxUtf8StringTarget);
            preferred.push_back(LinuxTextPlainLooseTarget);
        } else {
            const std::vector<NativeTransferFormatCandidate> candidates =
                mapper_.nativeCandidates(canonicalFormat,
                                         TransferPlatformFamily::Linux);
            for (const NativeTransferFormatCandidate& candidate : candidates)
                preferred.push_back(candidate.native.nativeFormatName);
        }

        for (const std::string& wanted : preferred) {
            const auto it = std::find_if(
                targets.begin(),
                targets.end(),
                [&wanted](const std::string& current) {
                    return sameTargetName(current, wanted);
                });
            if (it != targets.end())
                return *it;
        }

        for (const std::string& target : targets) {
            const std::optional<std::string> mapped =
                canonicalForNativeTarget(target);
            if (mapped.has_value() && *mapped == canonicalFormat)
                return target;
        }
        return std::nullopt;
    }

    void appendSnapshotEntry(const std::vector<std::string>& targets,
                             const std::string& canonicalFormat,
                             std::vector<MaterializedTransferEntry>& entries)
    {
        const std::optional<std::string> target =
            findNativeTargetForCanonical(targets, canonicalFormat);
        if (!target.has_value())
            return;

        const TargetDataResult data =
            requestTargetData(*target, options_.maxInlineBytes);
        if (data.status != CLIPBUS_OK || data.bytes.empty())
            return;

        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = canonicalFormat;
        descriptor.nativeFormatName = *target;
        descriptor.localFormatToken = 0;
        descriptor.formatId = nextFormatId();
        descriptor.itemIndex = 0;
        descriptor.estimatedBytes = data.bytes.size();
        descriptor.canInline = true;
        descriptor.canStream = false;
        descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

        MaterializedTransferEntry entry;
        entry.descriptor = std::move(descriptor);
        entry.bytes = data.bytes;
        entries.push_back(std::move(entry));
    }

    ClipboardSnapshot snapshotFromEntries(
        std::vector<MaterializedTransferEntry> entries,
        std::uint64_t sequence)
    {
        ClipboardSnapshot snapshot;
        if (entries.empty())
            return snapshot;

        bool hasText = false;
        bool hasImage = false;
        for (const MaterializedTransferEntry& entry : entries) {
            hasText = hasText ||
                      entry.descriptor.canonicalFormat == TextPlainUtf8Format ||
                      entry.descriptor.canonicalFormat == TextHtmlFormat ||
                      entry.descriptor.canonicalFormat == TextRtfFormat;
            hasImage = hasImage ||
                       entry.descriptor.canonicalFormat == ImagePngFormat;
        }

        TransferSourceBundle bundle;
        bundle.bundleId = nextBundleId();
        bundle.offerId = nextOfferId();
        bundle.ownerEpoch = sequence == 0 ? 1 : sequence;
        bundle.sequence = sequence;
        bundle.origin = TransferOrigin::Clipboard;
        bundle.side = TransferSide::Local;
        bundle.originSessionId = options_.originSessionId;
        bundle.policyVersion = options_.policyVersion;
        bundle.createdMonotonicUsec = monotonicNowUsec();
        bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
            nextSourceId(),
            std::move(entries)));

        TransferPresentation presentation;
        presentation.displayName = "Linux clipboard";
        presentation.itemCount = 1;
        presentation.sourceKind =
            hasText && hasImage ? TransferSourceKind::Mixed
            : hasImage       ? TransferSourceKind::Image
                             : TransferSourceKind::Text;
        bundle.presentation = std::move(presentation);

        snapshot.ownerEpoch = bundle.ownerEpoch;
        snapshot.sequence = bundle.sequence;
        snapshot.bundle = std::move(bundle);
        return snapshot;
    }

    ClipboardSnapshot snapshotFromFileList(
        const TransferFileList& fileList,
        std::vector<std::filesystem::path> paths,
        const std::string& nativeTarget,
        std::uint64_t sequence)
    {
        ClipboardSnapshot snapshot;
        if (fileList.files.empty())
            return snapshot;

        const protocol::ByteBuffer encoded = encodeTransferFileList(fileList);
        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = FdclFileListFormat;
        descriptor.nativeFormatName = nativeTarget;
        descriptor.localFormatToken = 0;
        descriptor.formatId = nextFormatId();
        descriptor.itemIndex = 0;
        descriptor.estimatedBytes = encoded.size();
        descriptor.canInline = true;
        descriptor.canStream = paths.size() == fileList.files.size();
        descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

        TransferPresentation presentation;
        presentation.itemCount = static_cast<std::uint32_t>(fileList.files.size());
        presentation.sourceKind = TransferSourceKind::FileList;
        presentation.allowedActions = transfer_action::Copy;
        presentation.preferredAction = TransferAction::Copy;
        presentation.displayName =
            fileList.files.size() == 1
                ? sanitizeTransferFileDisplayName(fileList.files.front().displayName)
                : std::string("files");

        TransferSourceBundle bundle;
        bundle.bundleId = nextBundleId();
        bundle.offerId = nextOfferId();
        bundle.ownerEpoch = sequence == 0 ? 1 : sequence;
        bundle.sequence = sequence;
        bundle.origin = TransferOrigin::Clipboard;
        bundle.side = TransferSide::Local;
        bundle.originSessionId = options_.originSessionId;
        bundle.policyVersion = options_.policyVersion;
        bundle.createdMonotonicUsec = monotonicNowUsec();
        bundle.presentation = std::move(presentation);
        if (descriptor.canStream) {
            bundle.sources.push_back(
                std::make_shared<LocalLinuxFileTransferSource>(
                    nextSourceId(),
                    descriptor,
                    fileList,
                    std::move(paths),
                    options_.maxFileRangeBytes,
                    options_.maxSingleFileBytes));
        } else {
            bundle.sources.push_back(std::make_shared<FileGroupTransferSource>(
                nextSourceId(),
                descriptor,
                fileList));
        }

        snapshot.ownerEpoch = bundle.ownerEpoch;
        snapshot.sequence = bundle.sequence;
        snapshot.bundle = std::move(bundle);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.fileListSnapshots;
        }
        return snapshot;
    }

    TransferReadResult readFileListFromBundle(const TransferSourceBundle& bundle,
                                              TransferReadRequest* listRequest)
    {
        TransferReadResult result;
        for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
            if (source == nullptr)
                continue;

            const std::vector<TransferFormatDescriptor> formats =
                source->formats();
            const auto fileList = std::find_if(formats.begin(),
                                               formats.end(),
                                               descriptorIsFileList);
            if (fileList == formats.end())
                continue;

            TransferReadRequest request;
            request.bundleId = bundle.bundleId;
            request.offerId = bundle.offerId;
            request.ownerEpoch = bundle.ownerEpoch;
            request.sourceId = source->id();
            request.itemIndex = fileList->itemIndex;
            request.formatId = fileList->formatId;
            request.localFormatToken = fileList->localFormatToken;
            request.canonicalFormat = FdclFileListFormat;
            request.acceptedMaxBytes = options_.maxInlineBytes;
            request.streamAccepted = false;
            request.requestedEncoding = TransferEncodingMode::CanonicalBytes;

            result = source->read(request);
            if (result.status == protocol::ResponseStatus::Unsupported &&
                remoteReader_ != nullptr) {
                result = readRemoteFormat(request, options_.delayedReadTimeoutMs);
            }
            if (result.ok() && result.canonicalFormat.empty())
                result.canonicalFormat = FdclFileListFormat;
            if (listRequest != nullptr)
                *listRequest = request;
            return result;
        }

        result.status = protocol::ResponseStatus::NotFound;
        result.message = "linux clipboard file-list format is not found";
        return result;
    }

    protocol::ResponseStatus publishRemoteFileListBundle(
        const TransferSourceBundle& bundle)
    {
        TransferReadRequest listRequest;
        TransferReadResult listRead =
            readFileListFromBundle(bundle, &listRequest);
        if (listRead.status == protocol::ResponseStatus::NotFound)
            return protocol::ResponseStatus::NotFound;
        if (!listRead.ok()) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.readFailures;
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                listRead.message.empty()
                    ? "linux remote file-list read failed"
                    : listRead.message;
            return listRead.status;
        }

        const TransferFileListDecodeResult decoded =
            decodeTransferFileList(listRead.bytes, options_.maxFileCount);
        if (!decoded.ok) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "linux remote file-list decode failed: " + decoded.message;
            return decoded.status;
        }
        if (decoded.fileList.files.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage = "linux remote file-list is empty";
            return protocol::ResponseStatus::NotFound;
        }

        for (const TransferFileDescriptor& file : decoded.fileList.files) {
            if (!file.directory && remoteFileReader_ == nullptr) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++diagnostics_.remoteFilePromiseFailures;
                diagnostics_.lastMessage =
                    "linux remote file publication needs a remote file reader";
                return protocol::ResponseStatus::Failed;
            }
            if (!file.directory &&
                options_.maxSingleFileBytes != 0 &&
                file.sizeBytes > options_.maxSingleFileBytes) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++diagnostics_.remoteFilePromiseFailures;
                diagnostics_.lastMessage =
                    "linux remote file exceeds max single file bytes";
                return protocol::ResponseStatus::TooLarge;
            }
        }

        std::shared_ptr<RemoteFilePromisePublication> publication;
        const protocol::ResponseStatus created =
            createRemoteFilePromisePublication(bundle,
                                               listRequest,
                                               decoded.fileList,
                                               &publication);
        if (created != protocol::ResponseStatus::Ok)
            return created;
        if (publication == nullptr ||
            publication->uriList.empty() ||
            publication->copiedFiles.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage = "linux remote file promise has no URIs";
            return protocol::ResponseStatus::Failed;
        }

        std::vector<std::string> nativeTargets;
        appendUniqueTarget(nativeTargets, LinuxUriListTarget);
        appendUniqueTarget(nativeTargets, LinuxGnomeCopiedFilesTarget);
        appendUniqueTarget(nativeTargets, LinuxMateCopiedFilesTarget);
        appendUniqueTarget(nativeTargets, LinuxXMozUrlTarget);
        appendUniqueTarget(nativeTargets, LinuxUtf8StringTarget);

        std::vector<clipbus_target_offer_t> offers;
        offers.reserve(nativeTargets.size());
        const std::uint64_t maxBytes = std::max<std::uint64_t>(
            std::max<std::uint64_t>(publication->uriList.size(),
                                    publication->copiedFiles.size()),
            publication->nautilusClipboard.size());
        for (const std::string& target : nativeTargets) {
            clipbus_target_offer_t offer = {};
            offer.native_target = target.c_str();
            offer.is_promised = 1;
            offer.max_bytes = maxBytes;
            offers.push_back(offer);
        }

        clipbus_clipboard_t* clipboard = nullptr;
        std::shared_ptr<RemoteFilePromisePublication> oldPublication;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_ || clipboard_ == nullptr) {
                ++diagnostics_.remoteFilePromiseFailures;
                diagnostics_.lastMessage = "linux clipboard backend is not started";
                return protocol::ResponseStatus::Failed;
            }
            oldPublication = std::move(remoteFilePublication_);
            publishedBundle_ = bundle;
            remoteFilePublication_ = publication;
            diagnostics_.fusePromiseActive = true;
            pendingStreams_.clear();
            renderedTargetCache_.clear();
            clipboard = clipboard_;
        }
        destroyRemoteFilePublication(oldPublication);

        const clipbus_status_t status = clipbus_clipboard_publish_targets(
            clipboard,
            offers.data(),
            offers.size());
        if (status != CLIPBUS_OK) {
            std::shared_ptr<RemoteFilePromisePublication> failedPublication;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                failedPublication = std::move(remoteFilePublication_);
                diagnostics_.fusePromiseActive = false;
            }
            destroyRemoteFilePublication(failedPublication);
            setClipbusMessage(status, "linux remote file clipboard publish failed");
            return responseFromClipbus(status);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        diagnostics_.publishedOfferId = bundle.offerId;
        diagnostics_.lastNativeSequence = bundle.sequence;
        diagnostics_.lastClipbusStatus = status;
        diagnostics_.lastMessage = "linux remote file promises published";
        ++diagnostics_.publishes;
        ++diagnostics_.remoteFilePromisePublishes;
        ++diagnostics_.remoteFilePromiseProviders;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus createRemoteFilePromisePublication(
        const TransferSourceBundle& bundle,
        const TransferReadRequest& listRequest,
        const TransferFileList& fileList,
        std::shared_ptr<RemoteFilePromisePublication>* output)
    {
        if (output == nullptr)
            return protocol::ResponseStatus::InvalidArgument;
        output->reset();
        if (!options_.enableFusePromise) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "linux fuse-promise publication is disabled";
            return protocol::ResponseStatus::Unsupported;
        }

#if !defined(FUSIONDESK_HAS_FUSE_PROMISE)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "linux remote file publication needs fuse-promise";
        }
        (void)bundle;
        (void)listRequest;
        (void)fileList;
        return protocol::ResponseStatus::Unsupported;
#else
        auto publication = std::make_shared<RemoteFilePromisePublication>();
        publication->offerId = bundle.offerId;
        publication->bundleId = bundle.bundleId;
        publication->ownerEpoch = bundle.ownerEpoch;

        fp_context_options_t contextOptions = FP_CONTEXT_OPTIONS_INIT;
        contextOptions.runtime_dir = options_.fusePromiseRuntimeDir.empty()
                                         ? nullptr
                                         : options_.fusePromiseRuntimeDir.c_str();
        fp_status_t fpStatus =
            fp_context_open(&contextOptions, &publication->context);
        if (fpStatus != FP_OK) {
            setFusePromiseMessage(fpStatus,
                                  "linux fuse-promise context open failed");
            return protocol::ResponseStatus::Failed;
        }

        fp_provider_ops_t ops = FP_PROVIDER_OPS_INIT(&Impl::onFusePromiseRead);
        fpStatus = fp_provider_register(publication->context,
                                        &ops,
                                        this,
                                        &publication->provider);
        if (fpStatus != FP_OK) {
            setFusePromiseMessage(fpStatus,
                                  "linux fuse-promise provider register failed");
            destroyRemoteFilePublication(publication);
            return protocol::ResponseStatus::Failed;
        }

        fp_promise_builder_t* builder = nullptr;
        fpStatus = fp_promise_builder_new(publication->context,
                                          publication->provider,
                                          &builder);
        if (fpStatus != FP_OK) {
            setFusePromiseMessage(fpStatus,
                                  "linux fuse-promise builder create failed");
            destroyRemoteFilePublication(publication);
            return protocol::ResponseStatus::Failed;
        }

        std::set<std::string> directories;
        std::set<std::string> emittedPromiseRootEntries;
        std::vector<std::string> promiseRootEntries;
        auto freeBuilder = [&builder]() {
            if (builder != nullptr) {
                fp_promise_builder_free(builder);
                builder = nullptr;
            }
        };
        auto addDirectory = [&](const std::string& relativePath) {
            if (relativePath.empty() || !directories.insert(relativePath).second)
                return FP_OK;

            fp_node_attr_t attr = FP_NODE_ATTR_INIT;
            attr.mode = 0755;
            attr.mtime_nsec = 0;
            const std::string nodeId = "dir:" + relativePath;
            return fp_promise_add_dir(builder,
                                      relativePath.c_str(),
                                      &attr,
                                      nodeId.c_str());
        };

        for (std::size_t index = 0; index < fileList.files.size(); ++index) {
            const TransferFileDescriptor& file = fileList.files[index];
            std::string relativePath =
                sanitizeTransferFileRelativePath(
                    !file.relativePath.empty() ? file.relativePath
                                               : file.displayName);
            if (relativePath.empty() || relativePath == "unnamed")
                relativePath = sanitizeTransferFileDisplayName(file.displayName);
            if (relativePath.empty()) {
                freeBuilder();
                destroyRemoteFilePublication(publication);
                return protocol::ResponseStatus::InvalidArgument;
            }

            const std::string promiseRootEntry =
                firstRelativePathSegment(relativePath);
            if (!promiseRootEntry.empty() &&
                emittedPromiseRootEntries.insert(promiseRootEntry).second) {
                promiseRootEntries.push_back(promiseRootEntry);
            }

            std::filesystem::path parentPath;
            const std::filesystem::path relativeFs(relativePath);
            std::vector<std::string> parts;
            for (const std::filesystem::path& part : relativeFs) {
                const std::string value = part.generic_u8string();
                if (!value.empty() && value != ".")
                    parts.push_back(value);
            }
            if (parts.empty()) {
                freeBuilder();
                destroyRemoteFilePublication(publication);
                return protocol::ResponseStatus::InvalidArgument;
            }
            for (std::size_t partIndex = 0;
                 partIndex + 1 < parts.size();
                 ++partIndex) {
                parentPath /= parts[partIndex];
                fpStatus = addDirectory(parentPath.generic_u8string());
                if (fpStatus != FP_OK) {
                    freeBuilder();
                    setFusePromiseMessage(
                        fpStatus,
                        "linux fuse-promise parent directory add failed");
                    destroyRemoteFilePublication(publication);
                    return protocol::ResponseStatus::Failed;
                }
            }

            fp_node_attr_t attr = FP_NODE_ATTR_INIT;
            attr.mode = file.directory ? 0755 : 0644;
            attr.size = file.directory ? 0 : file.sizeBytes;
            attr.mtime_nsec = mtimeNsecFromUnixUsec(file.lastModifiedUnixUsec);

            if (file.directory) {
                fpStatus = addDirectory(relativePath);
            } else {
                const std::string nodeId =
                    "file:" + std::to_string(index) + ":" +
                    std::to_string(file.objectId);
                fpStatus = fp_promise_add_file(builder,
                                               relativePath.c_str(),
                                               &attr,
                                               nodeId.c_str());
                if (fpStatus == FP_OK) {
                    RemoteFilePromiseNode node;
                    node.descriptor = file;
                    node.descriptor.relativePath = relativePath;
                    node.rangeRequest.bundleId = bundle.bundleId;
                    node.rangeRequest.offerId = bundle.offerId;
                    node.rangeRequest.ownerEpoch = bundle.ownerEpoch;
                    node.rangeRequest.sourceId = listRequest.sourceId;
                    node.rangeRequest.objectId = file.objectId;
                    node.rangeRequest.fileIndex =
                        static_cast<std::uint32_t>(index);
                    node.lockRequest.bundleId = bundle.bundleId;
                    node.lockRequest.offerId = bundle.offerId;
                    node.lockRequest.ownerEpoch = bundle.ownerEpoch;
                    node.lockRequest.sourceId = listRequest.sourceId;
                    node.lockRequest.objectId = file.objectId;
                    node.lockRequest.fileIndex =
                        static_cast<std::uint32_t>(index);
                    publication->nodes[nodeId] = std::move(node);
                }
            }

            if (fpStatus != FP_OK) {
                freeBuilder();
                setFusePromiseMessage(
                    fpStatus,
                    "linux fuse-promise node add failed");
                destroyRemoteFilePublication(publication);
                return protocol::ResponseStatus::Failed;
            }
        }

        char promisePath[8192] = {};
        fpStatus = fp_promise_commit(builder, promisePath, sizeof(promisePath));
        freeBuilder();
        if (fpStatus != FP_OK) {
            setFusePromiseMessage(fpStatus,
                                  "linux fuse-promise commit failed");
            destroyRemoteFilePublication(publication);
            return protocol::ResponseStatus::Failed;
        }

        publication->promiseRoot = std::filesystem::path(promisePath);
        const std::vector<std::string> uris =
            promiseClipboardUris(publication->promiseRoot, promiseRootEntries);
        if (uris.empty()) {
            destroyRemoteFilePublication(publication);
            return protocol::ResponseStatus::NotFound;
        }
        publication->uriList = uriListBytes(uris);
        publication->copiedFiles = copiedFilesBytes(uris);
        publication->nautilusClipboard = nautilusClipboardBytes(uris);
        *output = publication;
        return protocol::ResponseStatus::Ok;
#endif
    }

    void buildNativeTargetsForBundle(const TransferSourceBundle& bundle,
                                     std::vector<std::string>& targets,
                                     bool* hasFileList) const
    {
        if (hasFileList != nullptr)
            *hasFileList = false;

        for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
            if (source == nullptr)
                continue;

            for (const TransferFormatDescriptor& descriptor : source->formats()) {
                if (descriptorIsFileList(descriptor)) {
                    if (hasFileList != nullptr)
                        *hasFileList = true;
                    continue;
                }

                if (descriptor.canonicalFormat == TextPlainUtf8Format) {
                    appendUniqueTarget(targets, LinuxTextPlainTarget);
                    appendUniqueTarget(targets, LinuxUtf8StringTarget);
                    appendUniqueTarget(targets, LinuxTextPlainLooseTarget);
                    continue;
                }

                const std::vector<NativeTransferFormatCandidate> candidates =
                    mapper_.nativeCandidates(descriptor.canonicalFormat,
                                             TransferPlatformFamily::Linux);
                for (const NativeTransferFormatCandidate& candidate : candidates)
                    appendUniqueTarget(targets, candidate.native.nativeFormatName);
            }
        }
    }

    std::uint64_t maxBytesForNativeTarget(const TransferSourceBundle& bundle,
                                          const std::string& nativeTarget) const
    {
        const std::optional<std::string> canonical =
            canonicalForNativeTarget(nativeTarget);
        std::uint64_t estimated = 0;
        if (!canonical.has_value())
            return options_.maxInlineBytes;

        for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
            if (source == nullptr)
                continue;
            for (const TransferFormatDescriptor& descriptor : source->formats()) {
                if (descriptor.canonicalFormat == *canonical)
                    estimated = std::max(estimated, descriptor.estimatedBytes);
            }
        }
        return std::max<std::uint64_t>(estimated, options_.maxInlineBytes);
    }

    TransferReadResult readBestFormat(const TransferSourceBundle& bundle,
                                      const std::string& canonicalFormat,
                                      const std::string& nativeTarget)
    {
        TransferReadResult result;
        for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
            if (source == nullptr)
                continue;

            const std::vector<TransferFormatDescriptor> formats =
                source->formats();
            const auto format = std::find_if(
                formats.begin(),
                formats.end(),
                [&canonicalFormat](const TransferFormatDescriptor& descriptor) {
                    return descriptor.canonicalFormat == canonicalFormat;
                });
            if (format == formats.end())
                continue;

            TransferReadRequest request;
            request.bundleId = bundle.bundleId;
            request.offerId = bundle.offerId;
            request.ownerEpoch = bundle.ownerEpoch;
            request.sourceId = source->id();
            request.itemIndex = format->itemIndex;
            request.formatId = format->formatId;
            request.localFormatToken = format->localFormatToken;
            request.canonicalFormat = canonicalFormat;
            request.acceptedMaxBytes = maxBytesForNativeTarget(bundle, nativeTarget);
            request.streamAccepted = true;
            request.requestedEncoding =
                requestedEncodingForTarget(*format,
                                           canonicalFormat,
                                           nativeTarget);

            result = source->read(request);
            if (result.status == protocol::ResponseStatus::Unsupported &&
                remoteReader_ != nullptr) {
                result = readRemoteFormat(request, options_.delayedReadTimeoutMs);
            }
            if (result.ok() && result.canonicalFormat.empty())
                result.canonicalFormat = canonicalFormat;
            if (result.ok()) {
                const protocol::ResponseStatus converted =
                    transcodeReadResultForNativeTarget(*format,
                                                       nativeTarget,
                                                       &result);
                if (converted != protocol::ResponseStatus::Ok) {
                    result.status = converted;
                    result.bytes.clear();
                    if (result.message.empty()) {
                        result.message =
                            "linux clipboard target conversion is unsupported";
                    }
                }
            }
            return result;
        }

        result.status = protocol::ResponseStatus::NotFound;
        result.message = "linux clipboard format is not found";
        return result;
    }

    TransferTranscodeRequest transcodeRequestForTarget(
        const TransferFormatDescriptor& descriptor,
        const std::string& canonicalFormat,
        const std::string& nativeTarget,
        TransferEncodingMode sourceEncoding,
        protocol::ByteBuffer bytes = {}) const
    {
        TransferTranscodeRequest request;
        request.canonicalFormat = canonicalFormat;
        request.sourceNative.platform = TransferPlatformFamily::Unknown;
        request.sourceNative.nativeFormatName = descriptor.nativeFormatName;
        request.sourceNative.localFormatToken = descriptor.localFormatToken;
        request.targetNative.platform = TransferPlatformFamily::Linux;
        request.targetNative.nativeFormatName = nativeTarget;
        request.sourceEncoding = sourceEncoding;
        request.targetEncoding = TransferEncodingMode::NativePassthrough;
        request.bytes = std::move(bytes);
        return request;
    }

    TransferEncodingMode requestedEncodingForTarget(
        const TransferFormatDescriptor& descriptor,
        const std::string& canonicalFormat,
        const std::string& nativeTarget) const
    {
        if (descriptor.preferredEncoding !=
                TransferEncodingMode::NativePassthrough ||
            transcoder_ == nullptr) {
            return TransferEncodingMode::CanonicalBytes;
        }

        const TransferTranscodeRequest request =
            transcodeRequestForTarget(descriptor,
                                      canonicalFormat,
                                      nativeTarget,
                                      TransferEncodingMode::NativePassthrough);
        return transcoder_->canTranscode(request)
                   ? TransferEncodingMode::NativePassthrough
                   : TransferEncodingMode::CanonicalBytes;
    }

    protocol::ResponseStatus transcodeReadResultForNativeTarget(
        const TransferFormatDescriptor& descriptor,
        const std::string& nativeTarget,
        TransferReadResult* result) const
    {
        if (result == nullptr)
            return protocol::ResponseStatus::InvalidArgument;
        if (transcoder_ == nullptr)
            return protocol::ResponseStatus::Unsupported;

        const std::string canonical = result->canonicalFormat.empty()
                                          ? descriptor.canonicalFormat
                                          : result->canonicalFormat;
        TransferTranscodeRequest request =
            transcodeRequestForTarget(descriptor,
                                      canonical,
                                      nativeTarget,
                                      result->encoding,
                                      std::move(result->bytes));
        if (!transcoder_->canTranscode(request)) {
            result->message =
                "linux clipboard common transcoder does not support target";
            return protocol::ResponseStatus::Unsupported;
        }

        const TransferTranscodeResult converted =
            transcoder_->transcode(request);
        if (!converted.ok()) {
            result->message = converted.message;
            return converted.status;
        }

        result->bytes = converted.bytes;
        result->encoding = converted.encoding;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus renderNativeTarget(const std::string& nativeTarget,
                                                protocol::ByteBuffer* bytes)
    {
        if (bytes == nullptr)
            return protocol::ResponseStatus::InvalidArgument;
        if (sameTargetName(nativeTarget, LinuxGnomeCopiedFilesTarget) ||
            sameTargetName(nativeTarget, LinuxMateCopiedFilesTarget) ||
            sameTargetName(nativeTarget, LinuxUriListTarget) ||
            sameTargetName(nativeTarget, LinuxXMozUrlTarget) ||
            sameTargetName(nativeTarget, LinuxUtf8StringTarget)) {
            std::shared_ptr<RemoteFilePromisePublication> publication;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                publication = remoteFilePublication_;
            }
            if (publication != nullptr) {
                if (sameTargetName(nativeTarget, LinuxGnomeCopiedFilesTarget) ||
                    sameTargetName(nativeTarget, LinuxMateCopiedFilesTarget)) {
                    *bytes = publication->copiedFiles;
                } else if (sameTargetName(nativeTarget, LinuxUtf8StringTarget)) {
                    *bytes = publication->nautilusClipboard;
                } else {
                    *bytes = publication->uriList;
                }
                return protocol::ResponseStatus::Ok;
            }
        }
        const std::optional<std::string> canonical =
            canonicalForNativeTarget(nativeTarget);
        if (!canonical.has_value())
            return protocol::ResponseStatus::Unsupported;
        if (*canonical == FdclFileListFormat) {
            std::shared_ptr<RemoteFilePromisePublication> publication;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                publication = remoteFilePublication_;
            }
            if (publication == nullptr) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++diagnostics_.remoteFilePromiseFailures;
                diagnostics_.lastMessage =
                    "linux remote file promise publication is not active";
                return protocol::ResponseStatus::NotFound;
            }
            if (sameTargetName(nativeTarget, LinuxGnomeCopiedFilesTarget) ||
                sameTargetName(nativeTarget, LinuxMateCopiedFilesTarget)) {
                *bytes = publication->copiedFiles;
            } else {
                *bytes = publication->uriList;
            }
            return protocol::ResponseStatus::Ok;
        }

        TransferSourceBundle bundle;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto cached = renderedTargetCache_.find(nativeTarget);
            if (cached != renderedTargetCache_.end()) {
                *bytes = cached->second;
                ++diagnostics_.delayedRenderCacheHits;
                return protocol::ResponseStatus::Ok;
            }
            bundle = publishedBundle_;
        }
        TransferReadResult read =
            readBestFormat(bundle, *canonical, nativeTarget);
        if (!read.ok()) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.readFailures;
            diagnostics_.lastMessage =
                read.message.empty()
                    ? "linux delayed clipboard read failed"
                    : read.message;
            return read.status;
        }
        *bytes = read.bytes;
        if (bytes->size() <= options_.maxInlineBytes) {
            std::lock_guard<std::mutex> lock(mutex_);
            renderedTargetCache_[nativeTarget] = *bytes;
            ++diagnostics_.delayedRenderCacheStores;
        }
        return protocol::ResponseStatus::Ok;
    }

#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
    fp_status_t handleFusePromiseRead(const fp_read_request_t* request,
                                      fp_read_response_t* response)
    {
        if (request == nullptr ||
            response == nullptr ||
            response->buffer == nullptr ||
            request->node_id == nullptr) {
            return FP_ERR_INVALID_ARGUMENT;
        }
        response->bytes_written = 0;
        if (request->length == 0 || response->buffer_len == 0)
            return FP_OK;

        std::shared_ptr<RemoteFilePromisePublication> publication;
        std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader;
        std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker;
        LinuxClipboardEndpointOptions options;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_)
                return FP_ERR_PROVIDER_GONE;
            publication = remoteFilePublication_;
            remoteFileReader = remoteFileReader_;
            remoteObjectLocker = remoteObjectLocker_;
            options = options_;
        }
        if (publication == nullptr || remoteFileReader == nullptr)
            return FP_ERR_PROVIDER_GONE;

        const std::string nodeId(request->node_id);
        const auto node = publication->nodes.find(nodeId);
        if (node == publication->nodes.end())
            return FP_ERR_NOT_FOUND;
        if (request->relative_path != nullptr &&
            node->second.descriptor.relativePath != request->relative_path) {
            return FP_ERR_NOT_FOUND;
        }

        const TransferFileDescriptor& descriptor = node->second.descriptor;
        if (descriptor.directory)
            return FP_ERR_INVALID_ARGUMENT;
        if (options.maxSingleFileBytes != 0 &&
            descriptor.sizeBytes > options.maxSingleFileBytes) {
            return FP_ERR_INVALID_ARGUMENT;
        }
        if (request->offset >= descriptor.sizeBytes)
            return FP_OK;

        const std::uint64_t maxRangeBytes =
            options.maxFileRangeBytes == 0
                ? DefaultTransferFileRangeChunkBytes
                : options.maxFileRangeBytes;
        const std::uint64_t available =
            descriptor.sizeBytes - request->offset;
        const std::uint64_t requested =
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(request->length),
                static_cast<std::uint64_t>(response->buffer_len));
        const std::uint64_t wanted64 =
            std::min<std::uint64_t>(
                std::min<std::uint64_t>(requested, maxRangeBytes),
                available);
        if (wanted64 == 0)
            return FP_OK;

        TransferObjectLockRequest lockRequest = node->second.lockRequest;
        TransferFileRangeRequest rangeRequest = node->second.rangeRequest;
        rangeRequest.offset = request->offset;
        rangeRequest.requestedBytes = wanted64;

        auto taskStatus = std::make_shared<fp_status_t>(FP_OK);
        auto taskBytes = std::make_shared<protocol::ByteBuffer>();
        const bool completed = runCallbackTaskAndWait(
            [this,
             remoteFileReader,
             remoteObjectLocker,
             options,
             lockRequest,
             rangeRequest,
             wanted64,
             bufferLen = response->buffer_len,
             taskStatus,
             taskBytes]() {
                LinuxRemoteObjectLockGuard lockGuard;
                std::string lockMessage;
                const protocol::ResponseStatus locked =
                    lockGuard.lock(remoteObjectLocker.get(),
                                   lockRequest,
                                   options.delayedReadTimeoutMs,
                                   &lockMessage);
                if (locked != protocol::ResponseStatus::Ok) {
                    noteFusePromiseReadFailure(
                        "linux fuse-promise object lock failed: " +
                        lockMessage);
                    *taskStatus = responseToFusePromise(locked);
                    return;
                }

                TransferFileRangeResult range =
                    remoteFileReader->readRemoteFileRange(
                        rangeRequest,
                        options.delayedReadTimeoutMs);
                if (!range.ok()) {
                    noteFusePromiseReadFailure(
                        range.message.empty()
                            ? std::string(
                                  "linux fuse-promise remote file read failed")
                            : range.message);
                    *taskStatus = responseToFusePromise(range.status);
                    return;
                }
                if (range.bytes.size() > bufferLen ||
                    range.bytes.size() > wanted64) {
                    noteFusePromiseReadFailure(
                        "linux fuse-promise remote file read exceeded requested bytes");
                    *taskStatus = FP_ERR_INVALID_ARGUMENT;
                    return;
                }

                std::string unlockMessage;
                const protocol::ResponseStatus unlocked =
                    lockGuard.unlock(&unlockMessage);
                if (unlocked != protocol::ResponseStatus::Ok) {
                    noteFusePromiseReadFailure(
                        "linux fuse-promise object unlock failed: " +
                        unlockMessage);
                    *taskStatus = responseToFusePromise(unlocked);
                    return;
                }

                *taskBytes = std::move(range.bytes);
            },
            options.delayedReadTimeoutMs);
        if (!completed) {
            noteFusePromiseReadFailure(
                "linux fuse-promise remote read dispatcher timed out");
            return FP_ERR_TIMEOUT;
        }
        if (*taskStatus != FP_OK)
            return *taskStatus;

        if (!taskBytes->empty()) {
            std::memcpy(response->buffer,
                        taskBytes->data(),
                        taskBytes->size());
            response->bytes_written = taskBytes->size();
        }
        noteFusePromiseReadOk(response->bytes_written);
        return FP_OK;
    }
#endif

    clipbus_status_t handleTargetRequest(clipbus_request_id_t requestId,
                                         const char* nativeTarget,
                                         std::uint64_t)
    {
        if (nativeTarget == nullptr)
            return CLIPBUS_INVALID_ARGUMENT;

        const std::string target(nativeTarget);
        if (callbackDispatcher_ != nullptr) {
            const bool posted = callbackDispatcher_->postClipboardTask(
                [this, requestId, target]() {
                    completeTargetRequestAsync(requestId, target);
                });
            if (!posted) {
                setClipbusMessage(CLIPBUS_PLATFORM_ERROR,
                                  "linux clipboard dispatcher post failed");
                return CLIPBUS_PLATFORM_ERROR;
            }
            return CLIPBUS_PENDING;
        }

        return renderAndBeginTargetRequest(requestId, target);
    }

    void completeTargetRequestAsync(clipbus_request_id_t requestId,
                                    const std::string& nativeTarget)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_)
                return;
        }
        const clipbus_status_t status =
            renderAndBeginTargetRequest(requestId, nativeTarget);
        if (status == CLIPBUS_PENDING)
            return;

        failPendingTargetRequest(requestId, status);
    }

    clipbus_status_t renderAndBeginTargetRequest(
        clipbus_request_id_t requestId,
        const std::string& nativeTarget)
    {
        protocol::ByteBuffer bytes;
        const protocol::ResponseStatus status =
            renderNativeTarget(nativeTarget, &bytes);
        if (status != protocol::ResponseStatus::Ok)
            return clipbusFromResponse(status);

        clipbus_clipboard_t* clipboard = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_ || clipboard_ == nullptr)
                return CLIPBUS_NOT_STARTED;
            PendingStream stream;
            stream.nativeTarget = nativeTarget;
            stream.bytes = std::move(bytes);
            pendingStreams_[requestId] = std::move(stream);
            clipboard = clipboard_;
        }

        const std::uint64_t estimated = pendingStreamSize(requestId);
        const clipbus_status_t beginStatus =
            clipbus_clipboard_begin_request_stream(clipboard,
                                                   requestId,
                                                   estimated);
        if (beginStatus != CLIPBUS_OK) {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingStreams_.erase(requestId);
            diagnostics_.lastClipbusStatus = beginStatus;
            diagnostics_.lastMessage = "linux clipboard stream begin failed";
            return beginStatus;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.delayedRenders;
        }
        return CLIPBUS_PENDING;
    }

    void failPendingTargetRequest(clipbus_request_id_t requestId,
                                  clipbus_status_t failureStatus)
    {
        clipbus_clipboard_t* clipboard = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clipboard = clipboard_;
        }
        if (clipboard == nullptr)
            return;

        const clipbus_status_t beginStatus =
            clipbus_clipboard_begin_request_stream(clipboard, requestId, 0);
        if (beginStatus != CLIPBUS_OK) {
            setClipbusMessage(beginStatus,
                              "linux clipboard failed stream begin failed");
            return;
        }

        const clipbus_status_t endStatus =
            clipbus_clipboard_end_request_stream(clipboard,
                                                 requestId,
                                                 failureStatus);
        if (endStatus != CLIPBUS_OK) {
            setClipbusMessage(endStatus,
                              "linux clipboard failed stream end failed");
        }
    }

    std::uint64_t pendingStreamSize(clipbus_request_id_t requestId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = pendingStreams_.find(requestId);
        if (it == pendingStreams_.end())
            return 0;
        return static_cast<std::uint64_t>(it->second.bytes.size());
    }

    void handleStreamReady(clipbus_request_id_t requestId,
                           std::uint64_t maxChunkBytes)
    {
        clipbus_clipboard_t* clipboard = nullptr;
        protocol::ByteBuffer chunk;
        bool end = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clipboard = clipboard_;
            auto it = pendingStreams_.find(requestId);
            if (it == pendingStreams_.end()) {
                end = true;
            } else if (it->second.offset >= it->second.bytes.size()) {
                pendingStreams_.erase(it);
                end = true;
            } else {
                const std::uint64_t allowed =
                    maxChunkBytes == 0 ? options_.streamChunkBytes : maxChunkBytes;
                const std::uint64_t configured =
                    options_.streamChunkBytes == 0
                        ? allowed
                        : std::min<std::uint64_t>(allowed,
                                                  options_.streamChunkBytes);
                const std::size_t remaining =
                    it->second.bytes.size() - it->second.offset;
                const std::size_t chunkBytes = static_cast<std::size_t>(
                    std::min<std::uint64_t>(
                        configured,
                        static_cast<std::uint64_t>(remaining)));
                chunk.insert(chunk.end(),
                             it->second.bytes.begin() +
                                 static_cast<std::ptrdiff_t>(it->second.offset),
                             it->second.bytes.begin() +
                                 static_cast<std::ptrdiff_t>(
                                     it->second.offset + chunkBytes));
                it->second.offset += chunkBytes;
                ++diagnostics_.streamChunks;
                diagnostics_.streamBytes += chunkBytes;
            }
        }

        if (clipboard == nullptr)
            return;

        if (end) {
            const clipbus_status_t status =
                clipbus_clipboard_end_request_stream(clipboard,
                                                     requestId,
                                                     CLIPBUS_OK);
            if (status != CLIPBUS_OK)
                setClipbusMessage(status, "linux clipboard stream end failed");
            return;
        }

        const clipbus_status_t status =
            clipbus_clipboard_write_request_stream(clipboard,
                                                   requestId,
                                                   chunk.data(),
                                                   chunk.size());
        if (status != CLIPBUS_OK)
            setClipbusMessage(status, "linux clipboard stream write failed");
    }

    TransferBundleId nextBundleId()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextBundleId_++;
    }

    TransferOfferId nextOfferId()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextOfferId_++;
    }

    TransferSourceId nextSourceId()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextSourceId_++;
    }

    TransferFormatId nextFormatId()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextFormatId_++;
    }

    std::uint64_t nextNativeSequence()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ++diagnostics_.lastNativeSequence;
    }

    void setClipbusMessage(clipbus_status_t status, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_.lastClipbusStatus = status;
        const char* statusName = clipbus_status_name(status);
        diagnostics_.lastMessage =
            message + ": " + (statusName == nullptr ? "unknown" : statusName);
    }

#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
    void setFusePromiseMessage(fp_status_t status, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* statusName = fp_status_string(status);
        diagnostics_.lastMessage =
            message + ": " + (statusName == nullptr ? "unknown" : statusName);
    }

    void noteFusePromiseReadOk(std::size_t bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++diagnostics_.remoteFilePromiseReads;
        diagnostics_.remoteFilePromiseReadBytes += bytes;
    }

    void noteFusePromiseReadFailure(std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++diagnostics_.remoteFilePromiseReadFailures;
        diagnostics_.lastMessage = std::move(message);
    }
#endif

    void destroyRemoteFilePublication(
        std::shared_ptr<RemoteFilePromisePublication> publication)
    {
        if (publication == nullptr)
            return;
#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
        if (publication->provider != nullptr) {
            fp_provider_unregister(publication->provider);
            publication->provider = nullptr;
        }
        if (publication->context != nullptr) {
            fp_context_close(publication->context);
            publication->context = nullptr;
        }
#endif
    }

    void handleTargetsChanged()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++diagnostics_.nativeChangeNotifications;
        if (options_.suppressOwnClipboardUpdates &&
            suppressNextTargetsChanged_) {
            suppressNextTargetsChanged_ = false;
            ++diagnostics_.ownerSuppressions;
            pendingNativeClipboardChange_ = false;
        } else {
            pendingNativeClipboardChange_ = options_.enableChangeMonitor;
        }
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
    }

    void handleOwnerLost()
    {
        std::shared_ptr<RemoteFilePromisePublication> publication;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++diagnostics_.ownerLostNotifications;
            const bool suppress = options_.suppressOwnClipboardUpdates &&
                                  suppressNextOwnerLost_;
            suppressNextOwnerLost_ = false;
            if (suppress) {
                ++diagnostics_.ownerSuppressions;
                pendingNativeClipboardChange_ = false;
            } else {
                pendingNativeClipboardChange_ = options_.enableChangeMonitor;
            }
            diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
            diagnostics_.publishedOfferId = 0;
            diagnostics_.fusePromiseActive = false;
            publishedBundle_ = {};
            publication = std::move(remoteFilePublication_);
            pendingStreams_.clear();
            renderedTargetCache_.clear();
        }
        destroyRemoteFilePublication(publication);
    }

    void handleError(int code, const char* message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_.lastClipbusStatus = static_cast<std::uint32_t>(code);
        diagnostics_.lastMessage =
            message == nullptr ? "linux clipboard backend error" : message;
    }

    void handleTargetList(clipbus_request_id_t requestId,
                          clipbus_status_t status,
                          const char* const* nativeTargets,
                          std::size_t targetCount)
    {
        TargetListResult result;
        result.completed = true;
        result.status = status;
        if (nativeTargets != nullptr) {
            result.targets.reserve(targetCount);
            for (std::size_t index = 0; index < targetCount; ++index) {
                if (nativeTargets[index] != nullptr)
                    result.targets.emplace_back(nativeTargets[index]);
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto pending = pendingTargetLists_.find(requestId);
        if (pending != pendingTargetLists_.end()) {
            pending->second = std::move(result);
        } else {
            completedTargetLists_[requestId] = std::move(result);
        }
        condition_.notify_all();
    }

    void handleTargetData(clipbus_request_id_t requestId,
                          const char* nativeTarget,
                          clipbus_status_t status,
                          const std::uint8_t* data,
                          std::size_t dataLen)
    {
        TargetDataResult result;
        result.completed = true;
        result.status = status;
        if (nativeTarget != nullptr)
            result.nativeTarget = nativeTarget;
        if (data != nullptr && dataLen != 0)
            result.bytes.assign(data, data + dataLen);

        std::lock_guard<std::mutex> lock(mutex_);
        auto pending = pendingTargetData_.find(requestId);
        if (pending != pendingTargetData_.end()) {
            pending->second = std::move(result);
        } else {
            completedTargetData_[requestId] = std::move(result);
        }
        condition_.notify_all();
    }

#if defined(FUSIONDESK_HAS_FUSE_PROMISE)
    static fp_status_t onFusePromiseRead(const fp_read_request_t* request,
                                         fp_read_response_t* response,
                                         void* user)
    {
        auto* self = static_cast<Impl*>(user);
        if (self == nullptr)
            return FP_ERR_INVALID_ARGUMENT;
        return self->handleFusePromiseRead(request, response);
    }
#endif

    static clipbus_status_t onTargetRequest(void* user,
                                            clipbus_request_id_t requestId,
                                            const char* nativeTarget,
                                            std::uint64_t maxInlineBytes,
                                            std::uint64_t)
    {
        auto* self = static_cast<Impl*>(user);
        if (self == nullptr)
            return CLIPBUS_INVALID_ARGUMENT;
        return self->handleTargetRequest(requestId,
                                         nativeTarget,
                                         maxInlineBytes);
    }

    static void onTargetsChanged(void* user)
    {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr)
            self->handleTargetsChanged();
    }

    static void onOwnerLost(void* user)
    {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr)
            self->handleOwnerLost();
    }

    static void onError(void* user, int code, const char* message)
    {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr)
            self->handleError(code, message);
    }

    static void onTargetList(void* user,
                             clipbus_request_id_t requestId,
                             clipbus_status_t status,
                             const char* const* nativeTargets,
                             std::size_t targetCount)
    {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr)
            self->handleTargetList(requestId,
                                   status,
                                   nativeTargets,
                                   targetCount);
    }

    static void onTargetData(void* user,
                             clipbus_request_id_t requestId,
                             const char* nativeTarget,
                             clipbus_status_t status,
                             const std::uint8_t* data,
                             std::size_t dataLen)
    {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr)
            self->handleTargetData(requestId,
                                   nativeTarget,
                                   status,
                                   data,
                                   dataLen);
    }

    static void onStreamReady(void* user,
                              clipbus_request_id_t requestId,
                              const char*,
                              std::uint64_t maxChunkBytes,
                              std::uint64_t)
    {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr)
            self->handleStreamReady(requestId, maxChunkBytes);
    }

private:
    LinuxClipboardEndpointOptions options_;
    std::shared_ptr<IClipboardRemoteReader> remoteReader_;
    std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader_;
    std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker_;
    std::shared_ptr<ITransferTranscoder> transcoder_;
    std::shared_ptr<IClipboardCallbackDispatcher> callbackDispatcher_;
    DefaultTransferFormatMapper mapper_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    clipbus_clipboard_t* clipboard_ = nullptr;
    bool started_ = false;
    bool shuttingDown_ = false;
    bool pendingNativeClipboardChange_ = true;
    bool suppressNextTargetsChanged_ = false;
    bool suppressNextOwnerLost_ = false;
    TransferSourceBundle publishedBundle_;
    std::shared_ptr<RemoteFilePromisePublication> remoteFilePublication_;
    std::unordered_map<clipbus_request_id_t, TargetListResult>
        pendingTargetLists_;
    std::unordered_map<clipbus_request_id_t, TargetListResult>
        completedTargetLists_;
    std::unordered_map<clipbus_request_id_t, TargetDataResult>
        pendingTargetData_;
    std::unordered_map<clipbus_request_id_t, TargetDataResult>
        completedTargetData_;
    std::unordered_map<clipbus_request_id_t, PendingStream> pendingStreams_;
    std::unordered_map<std::string, protocol::ByteBuffer> renderedTargetCache_;
    LinuxClipboardEndpointDiagnostics diagnostics_;
    TransferBundleId nextBundleId_ = 1;
    TransferOfferId nextOfferId_ = 1;
    TransferSourceId nextSourceId_ = 1;
    TransferFormatId nextFormatId_ = 1;
};

LinuxClipboardEndpoint::LinuxClipboardEndpoint(
    LinuxClipboardEndpointOptions options,
    std::shared_ptr<IClipboardRemoteReader> remoteReader,
    std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader,
    std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker,
    std::shared_ptr<ITransferTranscoder> transcoder,
    std::shared_ptr<IClipboardCallbackDispatcher> callbackDispatcher)
    : impl_(std::make_unique<Impl>(std::move(options),
                                   std::move(remoteReader),
                                   std::move(remoteFileReader),
                                   std::move(remoteObjectLocker),
                                   std::move(transcoder),
                                   std::move(callbackDispatcher)))
{
}

LinuxClipboardEndpoint::~LinuxClipboardEndpoint() = default;

ClipboardSnapshot LinuxClipboardEndpoint::snapshot()
{
    return impl_->snapshot();
}

protocol::ResponseStatus LinuxClipboardEndpoint::publishBundle(
    const ClipboardPublishRequest& request)
{
    return impl_->publishBundle(request);
}

protocol::ResponseStatus LinuxClipboardEndpoint::clearPublishedBundle(
    TransferOfferId offerId)
{
    return impl_->clearPublishedBundle(offerId);
}

bool LinuxClipboardEndpoint::hasPendingClipboardChange() const
{
    return impl_->hasPendingClipboardChange();
}

void LinuxClipboardEndpoint::markClipboardChangeConsumed()
{
    impl_->markClipboardChangeConsumed();
}

LinuxClipboardEndpointDiagnostics LinuxClipboardEndpoint::diagnostics() const
{
    return impl_->diagnostics();
}

} // namespace clipboard
} // namespace linux_desktop
} // namespace platform
} // namespace fusiondesk
