#ifndef FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_LOCAL_FILES_H
#define FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_LOCAL_FILES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

struct WindowsHdropReadOptions
{
    bool expandDirectories = true;
    std::uint32_t maxFileCount = 1024;
    std::uint32_t maxDirectoryDepth = 32;
};

#if defined(_WIN32)
bool readWindowsFilePathList(
    modules::clipboard::TransferFileList& output,
    std::vector<std::wstring>* paths,
    const std::vector<std::wstring>& nativePaths,
    const WindowsHdropReadOptions& options);

bool readWindowsHdropFileList(
    modules::clipboard::TransferFileList& output,
    std::vector<std::wstring>* paths,
    const WindowsHdropReadOptions& options);

std::shared_ptr<modules::clipboard::TransferSource>
createLocalWindowsFileTransferSource(
    modules::clipboard::TransferSourceId sourceId,
    modules::clipboard::TransferFormatDescriptor descriptor,
    modules::clipboard::TransferFileList fileList,
    std::vector<std::wstring> paths,
    std::uint64_t maxRangeBytes,
    std::uint64_t maxSingleFileBytes);
#endif

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_LOCAL_FILES_H
