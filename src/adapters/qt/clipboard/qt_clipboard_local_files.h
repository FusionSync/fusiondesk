#ifndef FUSIONDESK_ADAPTERS_QT_CLIPBOARD_QT_CLIPBOARD_LOCAL_FILES_H
#define FUSIONDESK_ADAPTERS_QT_CLIPBOARD_QT_CLIPBOARD_LOCAL_FILES_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <QList>
#include <QUrl>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace clipboard {

struct QtClipboardLocalFileOptions
{
    bool expandDirectories = true;
    std::uint32_t maxFileCount = 1024;
    std::uint32_t maxDirectoryDepth = 32;
    std::uint64_t maxFileRangeBytes = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t maxSingleFileBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
};

bool readQtLocalFileUrlList(
    modules::clipboard::TransferFileList& output,
    std::vector<std::filesystem::path>* paths,
    const QList<QUrl>& urls,
    const QtClipboardLocalFileOptions& options);

std::shared_ptr<modules::clipboard::TransferSource>
createLocalQtFileTransferSource(
    modules::clipboard::TransferSourceId sourceId,
    modules::clipboard::TransferFormatDescriptor descriptor,
    modules::clipboard::TransferFileList fileList,
    std::vector<std::filesystem::path> paths,
    std::uint64_t maxRangeBytes,
    std::uint64_t maxSingleFileBytes);

} // namespace clipboard
} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_CLIPBOARD_QT_CLIPBOARD_LOCAL_FILES_H
