#include "windows_clipboard_local_files.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <limits>
#include <utility>

#if defined(_WIN32)

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

std::string wideToUtf8(const wchar_t* value, std::size_t characters)
{
    if (value == nullptr || characters == 0)
        return {};

    const int bytes = WideCharToMultiByte(CP_UTF8,
                                          0,
                                          value,
                                          static_cast<int>(characters),
                                          nullptr,
                                          0,
                                          nullptr,
                                          nullptr);
    if (bytes <= 0)
        return {};

    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        value,
                        static_cast<int>(characters),
                        &result[0],
                        bytes,
                        nullptr,
                        nullptr);
    return result;
}

std::uint64_t unixUsecFromFileTime(const FILETIME& value)
{
    ULARGE_INTEGER ticks;
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;

    constexpr std::uint64_t windowsToUnixEpoch100Ns = 116444736000000000ULL;
    if (ticks.QuadPart <= windowsToUnixEpoch100Ns)
        return 0;
    return (ticks.QuadPart - windowsToUnixEpoch100Ns) / 10ULL;
}

std::uint64_t fileSizeFromAttributes(const WIN32_FILE_ATTRIBUTE_DATA& data)
{
    ULARGE_INTEGER size;
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    return size.QuadPart;
}

std::string displayNameFromPath(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring leaf =
        slash == std::wstring::npos ? path : path.substr(slash + 1);
    return sanitizeTransferFileDisplayName(
        wideToUtf8(leaf.c_str(), leaf.size()));
}

std::wstring appendNativePathComponent(const std::wstring& base,
                                       const std::wstring& child)
{
    if (base.empty())
        return child;
    const wchar_t tail = base.back();
    if (tail == L'\\' || tail == L'/')
        return base + child;
    return base + L"\\" + child;
}

std::string relativePathForChild(const std::string& parent,
                                 const std::wstring& childName)
{
    const std::string child =
        sanitizeTransferFileDisplayName(
            wideToUtf8(childName.c_str(), childName.size()));
    if (parent.empty())
        return child;
    return sanitizeTransferFileRelativePath(parent + "/" + child);
}

void appendPathToHdropFileList(TransferFileList& output,
                               std::vector<std::wstring>* paths,
                               const std::wstring& path,
                               const std::string& relativePath,
                               const WindowsHdropReadOptions& options,
                               std::uint32_t depth)
{
    const std::uint32_t maxFileCount =
        options.maxFileCount == 0 ? 1 : options.maxFileCount;
    if (output.files.size() >= maxFileCount)
        return;

    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return;

    TransferFileDescriptor descriptor;
    descriptor.objectId =
        static_cast<TransferObjectId>(output.files.size() + 1);
    descriptor.displayName = displayNameFromPath(path);
    descriptor.relativePath = sanitizeTransferFileRelativePath(relativePath);
    if (descriptor.relativePath.empty() || descriptor.relativePath == "unnamed")
        descriptor.relativePath = descriptor.displayName;
    descriptor.directory =
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    descriptor.sizeBytes =
        descriptor.directory ? 0 : fileSizeFromAttributes(attributes);
    descriptor.lastModifiedUnixUsec =
        unixUsecFromFileTime(attributes.ftLastWriteTime);

    output.files.push_back(std::move(descriptor));
    if (paths != nullptr)
        paths->push_back(output.files.back().directory ? std::wstring() : path);

    if (!output.files.back().directory ||
        !options.expandDirectories ||
        depth >= options.maxDirectoryDepth ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return;
    }

    const std::string parentRelative = output.files.back().relativePath;
    const std::wstring searchPath = appendNativePathComponent(path, L"*");
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(searchPath.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do {
        if (output.files.size() >= maxFileCount)
            break;
        if (std::wcscmp(data.cFileName, L".") == 0 ||
            std::wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }

        const std::wstring childPath =
            appendNativePathComponent(path, data.cFileName);
        const std::string childRelative =
            relativePathForChild(parentRelative, data.cFileName);
        appendPathToHdropFileList(output,
                                  paths,
                                  childPath,
                                  childRelative,
                                  options,
                                  depth + 1);
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

class LocalWindowsFileTransferSource final
    : public TransferSource,
      public ITransferFileContentProvider
{
public:
    LocalWindowsFileTransferSource(TransferSourceId sourceId,
                                   TransferFormatDescriptor descriptor,
                                   TransferFileList fileList,
                                   std::vector<std::wstring> paths,
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
            result.message = "windows file source id is not found";
            return result;
        }
        if (request.fileIndex >= paths_.size() ||
            request.fileIndex >= fileList_.files.size()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "windows file index is not found";
            return result;
        }

        const TransferFileDescriptor& descriptor =
            fileList_.files[request.fileIndex];
        if (request.objectId != descriptor.objectId) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "windows file object is not found";
            return result;
        }
        if (descriptor.directory) {
            result.status = protocol::ResponseStatus::Unsupported;
            result.message = "directory file contents are unsupported";
            return result;
        }
        if (paths_[request.fileIndex].empty()) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "windows file path is not available";
            return result;
        }
        if (maxSingleFileBytes_ != 0 &&
            descriptor.sizeBytes > maxSingleFileBytes_) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "windows file exceeds max single file bytes";
            return result;
        }
        if (request.requestedBytes == 0 ||
            request.requestedBytes > maxRangeBytes_) {
            result.status = protocol::ResponseStatus::TooLarge;
            result.message = "windows file range request is too large";
            return result;
        }

        HANDLE file = CreateFileW(paths_[request.fileIndex].c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE |
                                      FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            result.status = protocol::ResponseStatus::NotFound;
            result.message = "windows file cannot be opened";
            return result;
        }

        LARGE_INTEGER distance;
        distance.QuadPart = static_cast<LONGLONG>(request.offset);
        if (!SetFilePointerEx(file, distance, nullptr, FILE_BEGIN)) {
            CloseHandle(file);
            result.status = protocol::ResponseStatus::InvalidArgument;
            result.message = "windows file range offset is invalid";
            return result;
        }

        const DWORD wanted = static_cast<DWORD>(
            std::min<std::uint64_t>(
                std::min<std::uint64_t>(request.requestedBytes,
                                         maxRangeBytes_),
                std::numeric_limits<DWORD>::max()));
        protocol::ByteBuffer buffer(wanted);
        DWORD readBytes = 0;
        const BOOL ok = ReadFile(file,
                                 buffer.data(),
                                 wanted,
                                 &readBytes,
                                 nullptr);
        CloseHandle(file);
        if (!ok) {
            result.status = protocol::ResponseStatus::Failed;
            result.message = "windows file read failed";
            return result;
        }

        buffer.resize(readBytes);
        result.status = protocol::ResponseStatus::Ok;
        result.bytes = std::move(buffer);
        result.endOfFile =
            readBytes < wanted ||
            request.offset + readBytes >= descriptor.sizeBytes;
        return result;
    }

private:
    TransferSourceId sourceId_ = 0;
    TransferFormatDescriptor descriptor_;
    TransferFileList fileList_;
    std::vector<std::wstring> paths_;
    std::uint64_t maxRangeBytes_ = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t maxSingleFileBytes_ = 0;
};

} // namespace

bool readWindowsHdropFileList(TransferFileList& output,
                              std::vector<std::wstring>* paths,
                              const WindowsHdropReadOptions& options)
{
    output.files.clear();
    if (paths != nullptr)
        paths->clear();
    if (options.maxFileCount == 0)
        return false;

    HANDLE handle = GetClipboardData(CF_HDROP);
    if (handle == nullptr)
        return false;

    const HDROP drop = static_cast<HDROP>(handle);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
    if (count == 0)
        return false;

    std::vector<std::wstring> nativePaths;
    nativePaths.reserve(std::min<UINT>(count, options.maxFileCount));
    for (UINT index = 0; index < count; ++index) {
        if (nativePaths.size() >= options.maxFileCount)
            break;

        const UINT pathChars = DragQueryFileW(drop, index, nullptr, 0);
        if (pathChars == 0)
            continue;

        std::vector<wchar_t> path(pathChars + 1, L'\0');
        if (DragQueryFileW(drop,
                           index,
                           path.data(),
                           static_cast<UINT>(path.size())) == 0)
            continue;

        nativePaths.emplace_back(path.data());
    }

    return readWindowsFilePathList(output, paths, nativePaths, options);
}

bool readWindowsFilePathList(TransferFileList& output,
                             std::vector<std::wstring>* paths,
                             const std::vector<std::wstring>& nativePaths,
                             const WindowsHdropReadOptions& options)
{
    output.files.clear();
    if (paths != nullptr)
        paths->clear();
    if (options.maxFileCount == 0 || nativePaths.empty())
        return false;

    output.files.reserve(
        std::min<std::size_t>(nativePaths.size(), options.maxFileCount));
    for (const std::wstring& nativePath : nativePaths) {
        if (output.files.size() >= options.maxFileCount)
            break;
        if (nativePath.empty())
            continue;

        const std::string rootRelative = displayNameFromPath(nativePath);
        appendPathToHdropFileList(output,
                                  paths,
                                  nativePath,
                                  rootRelative,
                                  options,
                                  0);
    }

    return !output.files.empty();
}

std::shared_ptr<TransferSource> createLocalWindowsFileTransferSource(
    TransferSourceId sourceId,
    TransferFormatDescriptor descriptor,
    TransferFileList fileList,
    std::vector<std::wstring> paths,
    std::uint64_t maxRangeBytes,
    std::uint64_t maxSingleFileBytes)
{
    return std::make_shared<LocalWindowsFileTransferSource>(
        sourceId,
        std::move(descriptor),
        std::move(fileList),
        std::move(paths),
        maxRangeBytes,
        maxSingleFileBytes);
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif
