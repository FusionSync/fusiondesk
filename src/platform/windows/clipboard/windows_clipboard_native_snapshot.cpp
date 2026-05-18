#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_descriptors.h"
#include "windows_clipboard_image_transcoding.h"
#include "windows_clipboard_local_files.h"
#include "windows_clipboard_native_helpers.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

#if defined(_WIN32)
namespace {

bool readClipboardFormatBytes(UINT format,
                              protocol::ByteBuffer& output,
                              DWORD& error)
{
    HANDLE handle = GetClipboardData(format);
    if (handle == nullptr) {
        error = GetLastError();
        return false;
    }

    const void* locked = GlobalLock(handle);
    if (locked == nullptr) {
        error = GetLastError();
        return false;
    }

    const SIZE_T size = GlobalSize(handle);
    output.assign(static_cast<std::size_t>(size), 0);
    if (size > 0) {
        std::memcpy(output.data(),
                    locked,
                    static_cast<std::size_t>(size));
    }
    GlobalUnlock(handle);
    error = ERROR_SUCCESS;
    return true;
}

} // namespace
#endif

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSnapshot(
    ClipboardSnapshot& output)
{
#if defined(_WIN32)
    if (!tryOpenClipboard(nullptr,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while reading clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }

    if (options_.suppressOwnClipboardUpdates && options_.writeOwnerMarker) {
        OwnerMarker marker;
        if (readOwnerMarker(marker) && marker.pid == GetCurrentProcessId()) {
            CloseClipboard();
            ++diagnostics_.ownerSuppressions;
            return protocol::ResponseStatus::NotFound;
        }
    }

    const UINT htmlToken = htmlFormat();
    const UINT rtfToken = rtfFormat();
    const UINT pngToken = pngFormat();
    const bool hasUnicodeText = IsClipboardFormatAvailable(CF_UNICODETEXT);
    const bool hasHtml = htmlToken != 0 && IsClipboardFormatAvailable(htmlToken);
    const bool hasRtf = rtfToken != 0 && IsClipboardFormatAvailable(rtfToken);
    const bool hasPng = pngToken != 0 && IsClipboardFormatAvailable(pngToken);
    const bool hasDibV5 = IsClipboardFormatAvailable(CF_DIBV5);
    const bool hasDib = IsClipboardFormatAvailable(CF_DIB);
    const bool hasFileDrop = IsClipboardFormatAvailable(CF_HDROP);
    if (!hasUnicodeText && !hasHtml && !hasRtf && !hasPng && !hasDibV5 &&
        !hasDib && !hasFileDrop) {
        CloseClipboard();
        return protocol::ResponseStatus::NotFound;
    }

    if (!hasUnicodeText && hasHtml) {
        HANDLE htmlHandle = GetClipboardData(htmlToken);
        if (htmlHandle == nullptr) {
            const DWORD error = GetLastError();
            CloseClipboard();
            recordNativeFailure(error, "GetClipboardData(HTML Format) failed");
            return protocol::ResponseStatus::Failed;
        }
        const void* htmlLocked = GlobalLock(htmlHandle);
        if (htmlLocked == nullptr) {
            const DWORD error = GetLastError();
            CloseClipboard();
            recordNativeFailure(error, "GlobalLock(HTML Format) failed");
            return protocol::ResponseStatus::Failed;
        }
        const SIZE_T htmlSize = GlobalSize(htmlHandle);
        protocol::ByteBuffer htmlBytes(static_cast<std::size_t>(htmlSize));
        if (htmlSize > 0) {
            std::memcpy(htmlBytes.data(),
                        htmlLocked,
                        static_cast<std::size_t>(htmlSize));
        }
        GlobalUnlock(htmlHandle);

        const std::uint64_t sequence = GetClipboardSequenceNumber();
        CloseClipboard();

        output = snapshotFromCanonicalHtml(
            canonicalHtmlFromWindowsHtml(htmlBytes),
            sequence);
        diagnostics_.lastNativeSequence = sequence;
        ++diagnostics_.nativeSnapshots;
        return output.bundle.offerId == 0 ?
            protocol::ResponseStatus::NotFound :
            protocol::ResponseStatus::Ok;
    }

    if (!hasUnicodeText && !hasHtml && hasRtf) {
        HANDLE rtfHandle = GetClipboardData(rtfToken);
        if (rtfHandle == nullptr) {
            const DWORD error = GetLastError();
            CloseClipboard();
            recordNativeFailure(error, "GetClipboardData(Rich Text Format) failed");
            return protocol::ResponseStatus::Failed;
        }
        const void* rtfLocked = GlobalLock(rtfHandle);
        if (rtfLocked == nullptr) {
            const DWORD error = GetLastError();
            CloseClipboard();
            recordNativeFailure(error, "GlobalLock(Rich Text Format) failed");
            return protocol::ResponseStatus::Failed;
        }
        const SIZE_T rtfSize = GlobalSize(rtfHandle);
        protocol::ByteBuffer rtfBytes(static_cast<std::size_t>(rtfSize));
        if (rtfSize > 0) {
            std::memcpy(rtfBytes.data(),
                        rtfLocked,
                        static_cast<std::size_t>(rtfSize));
        }
        GlobalUnlock(rtfHandle);

        const std::uint64_t sequence = GetClipboardSequenceNumber();
        CloseClipboard();

        output = snapshotFromCanonicalRtf(rtfBytes, sequence);
        diagnostics_.lastNativeSequence = sequence;
        ++diagnostics_.nativeSnapshots;
        return output.bundle.offerId == 0 ?
            protocol::ResponseStatus::NotFound :
            protocol::ResponseStatus::Ok;
    }

    if (!hasUnicodeText && !hasHtml && !hasRtf && hasPng) {
        protocol::ByteBuffer pngBytes;
        DWORD error = ERROR_SUCCESS;
        if (!readClipboardFormatBytes(pngToken, pngBytes, error)) {
            CloseClipboard();
            recordNativeFailure(error, "GetClipboardData(PNG) failed");
            return protocol::ResponseStatus::Failed;
        }

        const std::uint64_t sequence = GetClipboardSequenceNumber();
        CloseClipboard();

        output = snapshotFromImagePng(pngBytes, sequence);
        diagnostics_.lastNativeSequence = sequence;
        ++diagnostics_.nativeSnapshots;
        return output.bundle.offerId == 0 ?
            protocol::ResponseStatus::NotFound :
            protocol::ResponseStatus::Ok;
    }

    if (!hasUnicodeText && !hasHtml && !hasRtf && !hasPng &&
        (hasDibV5 || hasDib)) {
        const UINT dibFormat = hasDibV5 ? CF_DIBV5 : CF_DIB;
        protocol::ByteBuffer dibBytes;
        DWORD error = ERROR_SUCCESS;
        if (!readClipboardFormatBytes(dibFormat, dibBytes, error)) {
            CloseClipboard();
            recordNativeFailure(error,
                                hasDibV5 ?
                                    "GetClipboardData(CF_DIBV5) failed" :
                                    "GetClipboardData(CF_DIB) failed");
            return protocol::ResponseStatus::Failed;
        }

        const std::uint64_t sequence = GetClipboardSequenceNumber();
        CloseClipboard();

        const protocol::ByteBuffer pngBytes =
            windowsPngFromDibBytes(dibBytes);
        if (pngBytes.empty()) {
            recordNativeFailure(ERROR_INVALID_DATA,
                                hasDibV5 ?
                                    "CF_DIBV5 to PNG conversion failed" :
                                    "CF_DIB to PNG conversion failed");
            return protocol::ResponseStatus::Failed;
        }

        output = snapshotFromImageDib(
            dibBytes,
            pngBytes,
            sequence,
            hasDibV5 ? WindowsDibV5Name : WindowsDibName,
            dibFormat);
        diagnostics_.lastNativeSequence = sequence;
        ++diagnostics_.nativeSnapshots;
        return output.bundle.offerId == 0 ?
            protocol::ResponseStatus::NotFound :
            protocol::ResponseStatus::Ok;
    }

    if (!hasUnicodeText && !hasHtml && !hasRtf && !hasPng && !hasDibV5 &&
        !hasDib && hasFileDrop) {
        TransferFileList fileList;
        std::vector<std::wstring> filePaths;
        WindowsHdropReadOptions hdropOptions;
        hdropOptions.expandDirectories = options_.expandDroppedDirectories;
        hdropOptions.maxFileCount = options_.maxFileCount;
        hdropOptions.maxDirectoryDepth = options_.maxDirectoryDepth;
        if (!readWindowsHdropFileList(fileList, &filePaths, hdropOptions)) {
            const DWORD error = GetLastError();
            CloseClipboard();
            recordNativeFailure(error, "GetClipboardData(CF_HDROP) failed");
            return protocol::ResponseStatus::Failed;
        }

        const std::uint64_t sequence = GetClipboardSequenceNumber();
        CloseClipboard();

        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = FdclFileListFormat;
        descriptor.nativeFormatName = WindowsFileDropName;
        descriptor.localFormatToken = CF_HDROP;
        descriptor.formatId = nextFormatId_++;
        descriptor.itemIndex = 0;
        descriptor.estimatedBytes = encodeTransferFileList(fileList).size();
        descriptor.canInline = true;
        descriptor.canStream = true;
        descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

        TransferPresentation presentation;
        presentation.itemCount =
            static_cast<std::uint32_t>(fileList.files.size());
        presentation.sourceKind = TransferSourceKind::FileList;
        presentation.allowedActions = transfer_action::Copy;
        presentation.preferredAction = TransferAction::Copy;
        presentation.displayName =
            fileList.files.size() == 1
                ? sanitizeTransferFileDisplayName(
                      fileList.files.front().displayName)
                : std::string("files");

        TransferSourceBundle bundle;
        bundle.bundleId = nextBundleId_++;
        bundle.offerId = nextOfferId_++;
        bundle.ownerEpoch =
            sequence == 0 ? windowsClipboardMonotonicNowUsec() : sequence;
        bundle.sequence = sequence;
        bundle.origin = TransferOrigin::Clipboard;
        bundle.side = TransferSide::Local;
        bundle.originSessionId = options_.originSessionId;
        bundle.policyVersion = options_.policyVersion;
        bundle.createdMonotonicUsec = windowsClipboardMonotonicNowUsec();
        bundle.presentation = presentation;
        bundle.sources.push_back(createLocalWindowsFileTransferSource(
            nextSourceId_++,
            descriptor,
            fileList,
            filePaths,
            options_.maxFileRangeBytes,
            options_.maxSingleFileBytes));

        output.ownerEpoch = bundle.ownerEpoch;
        output.sequence = bundle.sequence;
        output.bundle = std::move(bundle);
        ++diagnostics_.fileListSnapshots;
        diagnostics_.lastNativeSequence = sequence;
        ++diagnostics_.nativeSnapshots;
        return output.bundle.offerId == 0 ?
            protocol::ResponseStatus::NotFound :
            protocol::ResponseStatus::Ok;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GetClipboardData(CF_UNICODETEXT) failed");
        return protocol::ResponseStatus::Failed;
    }

    const void* locked = GlobalLock(handle);
    if (locked == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GlobalLock(CF_UNICODETEXT) failed");
        return protocol::ResponseStatus::Failed;
    }

    const SIZE_T size = GlobalSize(handle);
    protocol::ByteBuffer nativeBytes(static_cast<std::size_t>(size));
    if (size > 0)
        std::memcpy(nativeBytes.data(), locked, static_cast<std::size_t>(size));
    GlobalUnlock(handle);

    const std::uint64_t sequence = GetClipboardSequenceNumber();
    CloseClipboard();

    output = snapshotFromCanonicalText(
        canonicalUtf8FromWindowsCfUnicodeText(nativeBytes),
        sequence);
    diagnostics_.lastNativeSequence = sequence;
    ++diagnostics_.nativeSnapshots;
    return output.bundle.offerId == 0 ?
        protocol::ResponseStatus::NotFound :
        protocol::ResponseStatus::Ok;
#else
    (void)output;
    return protocol::ResponseStatus::Unsupported;
#endif
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
