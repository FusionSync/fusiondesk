#ifndef FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_OLE_DATA_OBJECT_H
#define FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_OLE_DATA_OBJECT_H

#include <cstdint>
#include <memory>

#include "fusiondesk/modules/clipboard/clipboard_transfer.h"
#include "fusiondesk/modules/clipboard/clipboard_types.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#endif

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

#if defined(_WIN32)
IDataObject* createRemoteFileDataObject(
    modules::clipboard::TransferSourceBundle bundle,
    modules::clipboard::TransferFileList fileList,
    protocol::ByteBuffer groupDescriptor,
    modules::clipboard::TransferSourceId sourceId,
    std::uint64_t maxChunkBytes,
    std::uint32_t timeoutMs,
    std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader> reader,
    std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker> locker = {});

IDropSource* createRemoteFileDropSource();
#endif

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_OLE_DATA_OBJECT_H
