#ifndef FUSIONDESK_PLATFORM_MACOS_CLIPBOARD_MAC_CLIPBOARD_LOCAL_FILES_H
#define FUSIONDESK_PLATFORM_MACOS_CLIPBOARD_MAC_CLIPBOARD_LOCAL_FILES_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

namespace fusiondesk {
namespace platform {
namespace macos {
namespace clipboard {

struct MacClipboardLocalFileOptions
{
    bool expandDirectories = true;
    std::uint32_t maxFileCount = 1024;
    std::uint32_t maxDirectoryDepth = 32;
    std::uint64_t maxFileRangeBytes = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t maxSingleFileBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
};

bool readMacLocalFileList(
    modules::clipboard::TransferFileList& output,
    std::vector<std::filesystem::path>* paths,
    const std::vector<std::filesystem::path>& inputPaths,
    const MacClipboardLocalFileOptions& options);

std::shared_ptr<modules::clipboard::TransferSource>
createLocalMacFileTransferSource(
    modules::clipboard::TransferSourceId sourceId,
    modules::clipboard::TransferFormatDescriptor descriptor,
    modules::clipboard::TransferFileList fileList,
    std::vector<std::filesystem::path> paths,
    std::uint64_t maxRangeBytes,
    std::uint64_t maxSingleFileBytes);

} // namespace clipboard
} // namespace macos
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_MACOS_CLIPBOARD_MAC_CLIPBOARD_LOCAL_FILES_H
