#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "windows_clipboard_local_files.h"

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;
using namespace fusiondesk::platform::windows::clipboard;

namespace {

std::filesystem::path makeTempRoot()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("fusiondesk_clipboard_local_files_" +
         std::to_string(GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "nested");
    return root;
}

void writeBytes(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::size_t findByDisplayName(const TransferFileList& list,
                              const std::string& displayName)
{
    for (std::size_t i = 0; i < list.files.size(); ++i) {
        if (list.files[i].displayName == displayName)
            return i;
    }
    assert(false);
    return 0;
}

std::size_t findDirectory(const TransferFileList& list)
{
    for (std::size_t i = 0; i < list.files.size(); ++i) {
        if (list.files[i].directory)
            return i;
    }
    assert(false);
    return 0;
}

void localPathListExpandsDirectoriesAndHonorsLimits()
{
    const std::filesystem::path root = makeTempRoot();
    writeBytes(root / "alpha.txt", "abcdefghij");
    writeBytes(root / "nested" / "beta.txt", "nested-data");

    WindowsHdropReadOptions options;
    options.expandDirectories = true;
    options.maxFileCount = 16;
    options.maxDirectoryDepth = 8;

    TransferFileList list;
    std::vector<std::wstring> paths;
    assert(readWindowsFilePathList(list, &paths, {root.wstring()}, options));
    assert(list.files.size() >= 4);
    assert(paths.size() == list.files.size());

    const std::size_t alphaIndex = findByDisplayName(list, "alpha.txt");
    assert(!list.files[alphaIndex].directory);
    assert(list.files[alphaIndex].sizeBytes == 10);
    assert(!paths[alphaIndex].empty());

    const std::size_t betaIndex = findByDisplayName(list, "beta.txt");
    assert(!list.files[betaIndex].directory);
    assert(list.files[betaIndex].relativePath.find("nested/beta.txt") !=
           std::string::npos);
    assert(!paths[betaIndex].empty());

    const std::size_t directoryIndex = findDirectory(list);
    assert(paths[directoryIndex].empty());

    options.maxDirectoryDepth = 0;
    TransferFileList depthLimited;
    std::vector<std::wstring> depthLimitedPaths;
    assert(readWindowsFilePathList(depthLimited,
                                   &depthLimitedPaths,
                                   {root.wstring()},
                                   options));
    assert(depthLimited.files.size() == 1);
    assert(depthLimited.files.front().directory);

    options.maxDirectoryDepth = 8;
    options.maxFileCount = 2;
    TransferFileList countLimited;
    std::vector<std::wstring> countLimitedPaths;
    assert(readWindowsFilePathList(countLimited,
                                   &countLimitedPaths,
                                   {root.wstring()},
                                   options));
    assert(countLimited.files.size() == 2);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

void localWindowsFileSourceReadsRangesAndRejectsInvalidObjects()
{
    const std::filesystem::path root = makeTempRoot();
    writeBytes(root / "alpha.txt", "abcdefghij");
    writeBytes(root / "nested" / "beta.txt", "nested-data");

    WindowsHdropReadOptions options;
    options.expandDirectories = true;
    options.maxFileCount = 16;
    options.maxDirectoryDepth = 8;

    TransferFileList list;
    std::vector<std::wstring> paths;
    assert(readWindowsFilePathList(list, &paths, {root.wstring()}, options));

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "CF_HDROP";
    descriptor.localFormatToken = CF_HDROP;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.canStream = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    std::shared_ptr<TransferSource> source =
        createLocalWindowsFileTransferSource(77,
                                             descriptor,
                                             list,
                                             paths,
                                             4,
                                             0);
    std::shared_ptr<ITransferFileContentProvider> provider =
        std::dynamic_pointer_cast<ITransferFileContentProvider>(source);
    assert(provider != nullptr);

    const std::size_t alphaIndex = findByDisplayName(list, "alpha.txt");
    TransferFileRangeRequest request;
    request.sourceId = 77;
    request.objectId = list.files[alphaIndex].objectId;
    request.fileIndex = static_cast<std::uint32_t>(alphaIndex);
    request.offset = 2;
    request.requestedBytes = 4;

    TransferFileRangeResult range = provider->readFileRange(request);
    assert(range.ok());
    assert(range.bytes == protocol::ByteBuffer({'c', 'd', 'e', 'f'}));
    assert(!range.endOfFile);

    request.offset = 8;
    range = provider->readFileRange(request);
    assert(range.ok());
    assert(range.bytes == protocol::ByteBuffer({'i', 'j'}));
    assert(range.endOfFile);

    request.requestedBytes = 5;
    range = provider->readFileRange(request);
    assert(range.status == protocol::ResponseStatus::TooLarge);

    const std::size_t directoryIndex = findDirectory(list);
    request.fileIndex = static_cast<std::uint32_t>(directoryIndex);
    request.objectId = list.files[directoryIndex].objectId;
    request.offset = 0;
    request.requestedBytes = 4;
    range = provider->readFileRange(request);
    assert(range.status == protocol::ResponseStatus::Unsupported);

    std::shared_ptr<TransferSource> cappedSource =
        createLocalWindowsFileTransferSource(77,
                                             descriptor,
                                             list,
                                             paths,
                                             4,
                                             5);
    provider = std::dynamic_pointer_cast<ITransferFileContentProvider>(
        cappedSource);
    request.fileIndex = static_cast<std::uint32_t>(alphaIndex);
    request.objectId = list.files[alphaIndex].objectId;
    range = provider->readFileRange(request);
    assert(range.status == protocol::ResponseStatus::TooLarge);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main()
{
    localPathListExpandsDirectoriesAndHonorsLimits();
    localWindowsFileSourceReadsRangesAndRejectsInvalidObjects();
    return 0;
}
