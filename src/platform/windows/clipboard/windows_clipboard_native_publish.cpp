#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_descriptors.h"
#include "windows_clipboard_native_helpers.h"
#include "windows_clipboard_ole_data_object.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

std::string bytesToString(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

protocol::ResponseStatus WindowsClipboardEndpoint::nativePublishText(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence,
    const protocol::ByteBuffer& canonicalUtf8)
{
#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while publishing clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }

    if (!EmptyClipboard()) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "EmptyClipboard failed while publishing clipboard");
        return protocol::ResponseStatus::Failed;
    }

    HANDLE textHandle =
        allocMoveableBytes(windowsCfUnicodeTextFromCanonicalUtf8(canonicalUtf8));
    if (textHandle == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GlobalAlloc failed for CF_UNICODETEXT");
        return protocol::ResponseStatus::Failed;
    }
    if (SetClipboardData(CF_UNICODETEXT, textHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(textHandle);
        CloseClipboard();
        recordNativeFailure(error, "SetClipboardData(CF_UNICODETEXT) failed");
        return protocol::ResponseStatus::Failed;
    }

    if (options_.writeOwnerMarker) {
        HANDLE markerHandle = allocOwnerMarker(offerId, ownerEpoch, sequence);
        const UINT markerFormat = ownerMarkerFormat();
        if (markerHandle != nullptr && markerFormat != 0) {
            if (SetClipboardData(markerFormat, markerHandle) == nullptr)
                GlobalFree(markerHandle);
        }
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)offerId;
    (void)ownerEpoch;
    (void)sequence;
    (void)canonicalUtf8;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativePublishRtf(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence,
    const protocol::ByteBuffer& canonicalRtf)
{
#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while publishing RTF clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }

    if (!EmptyClipboard()) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "EmptyClipboard failed while publishing RTF clipboard");
        return protocol::ResponseStatus::Failed;
    }

    const UINT token = rtfFormat();
    if (token == 0) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "RegisterClipboardFormat(Rich Text Format) failed");
        return protocol::ResponseStatus::Failed;
    }

    HANDLE rtfHandle = allocMoveableBytes(canonicalRtf);
    if (rtfHandle == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GlobalAlloc failed for Rich Text Format");
        return protocol::ResponseStatus::Failed;
    }
    if (SetClipboardData(token, rtfHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(rtfHandle);
        CloseClipboard();
        recordNativeFailure(error, "SetClipboardData(Rich Text Format) failed");
        return protocol::ResponseStatus::Failed;
    }

    if (options_.writeOwnerMarker) {
        HANDLE markerHandle = allocOwnerMarker(offerId, ownerEpoch, sequence);
        const UINT markerFormat = ownerMarkerFormat();
        if (markerHandle != nullptr && markerFormat != 0) {
            if (SetClipboardData(markerFormat, markerHandle) == nullptr)
                GlobalFree(markerHandle);
        }
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)offerId;
    (void)ownerEpoch;
    (void)sequence;
    (void)canonicalRtf;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativePublishDelayedTextOffer(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence)
{
    if (options_.dryRun)
        return protocol::ResponseStatus::Ok;

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }

    SetWindowLongPtrW(owner,
                      GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while publishing delayed clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }

    if (!EmptyClipboard()) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "EmptyClipboard failed while publishing delayed clipboard");
        return protocol::ResponseStatus::Failed;
    }

    if (hasTextFormat(publishedBundle_)) {
        SetLastError(ERROR_SUCCESS);
        SetClipboardData(CF_UNICODETEXT, nullptr);
        const DWORD delayedError = GetLastError();
        if (delayedError != ERROR_SUCCESS) {
            CloseClipboard();
            recordNativeFailure(delayedError, "SetClipboardData delayed CF_UNICODETEXT failed");
            return protocol::ResponseStatus::Failed;
        }
    }

    if (hasHtmlFormat(publishedBundle_)) {
        const UINT token = htmlFormat();
        if (token == 0) {
            CloseClipboard();
            recordNativeFailure(GetLastError(), "RegisterClipboardFormat(HTML Format) failed");
            return protocol::ResponseStatus::Failed;
        }
        SetLastError(ERROR_SUCCESS);
        SetClipboardData(token, nullptr);
        const DWORD delayedError = GetLastError();
        if (delayedError != ERROR_SUCCESS) {
            CloseClipboard();
            recordNativeFailure(delayedError, "SetClipboardData delayed HTML Format failed");
            return protocol::ResponseStatus::Failed;
        }
    }

    if (hasRtfFormat(publishedBundle_)) {
        const UINT token = rtfFormat();
        if (token == 0) {
            CloseClipboard();
            recordNativeFailure(GetLastError(), "RegisterClipboardFormat(Rich Text Format) failed");
            return protocol::ResponseStatus::Failed;
        }
        SetLastError(ERROR_SUCCESS);
        SetClipboardData(token, nullptr);
        const DWORD delayedError = GetLastError();
        if (delayedError != ERROR_SUCCESS) {
            CloseClipboard();
            recordNativeFailure(delayedError, "SetClipboardData delayed Rich Text Format failed");
            return protocol::ResponseStatus::Failed;
        }
    }

    const bool hasImagePng = hasImagePngFormat(publishedBundle_);
    const bool hasImageDib = hasImageDibFormat(publishedBundle_);
    if (hasImagePng || hasImageDib) {
        if (hasImagePng) {
            const UINT token = pngFormat();
            if (token == 0) {
                CloseClipboard();
                recordNativeFailure(GetLastError(), "RegisterClipboardFormat(PNG) failed");
                return protocol::ResponseStatus::Failed;
            }
            SetLastError(ERROR_SUCCESS);
            SetClipboardData(token, nullptr);
            const DWORD delayedError = GetLastError();
            if (delayedError != ERROR_SUCCESS) {
                CloseClipboard();
                recordNativeFailure(delayedError, "SetClipboardData delayed PNG failed");
                return protocol::ResponseStatus::Failed;
            }
        }
        SetLastError(ERROR_SUCCESS);
        SetClipboardData(CF_DIBV5, nullptr);
        const DWORD dibV5Error = GetLastError();
        if (dibV5Error != ERROR_SUCCESS) {
            CloseClipboard();
            recordNativeFailure(dibV5Error, "SetClipboardData delayed CF_DIBV5 failed");
            return protocol::ResponseStatus::Failed;
        }
        SetLastError(ERROR_SUCCESS);
        SetClipboardData(CF_DIB, nullptr);
        const DWORD dibError = GetLastError();
        if (dibError != ERROR_SUCCESS) {
            CloseClipboard();
            recordNativeFailure(dibError, "SetClipboardData delayed CF_DIB failed");
            return protocol::ResponseStatus::Failed;
        }
    }

    if (options_.writeOwnerMarker) {
        HANDLE markerHandle = allocOwnerMarker(offerId, ownerEpoch, sequence);
        const UINT markerFormat = ownerMarkerFormat();
        if (markerHandle != nullptr && markerFormat != 0) {
            if (SetClipboardData(markerFormat, markerHandle) == nullptr)
                GlobalFree(markerHandle);
        }
    }

    CloseClipboard();
    ++diagnostics_.delayedPublishes;
    return protocol::ResponseStatus::Ok;
#else
    (void)offerId;
    (void)ownerEpoch;
    (void)sequence;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativePublishFileList(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence,
    const protocol::ByteBuffer& canonicalFileList)
{
    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(canonicalFileList,
                               options_.maxFileCount,
                               255);
    if (!decoded.ok) {
        diagnostics_.lastMessage = decoded.message;
        return decoded.status;
    }

    dryRunFileList_ = decoded.fileList;
    dryRunFilePaths_.clear();
    dryRunFileGroupDescriptor_ =
        windowsFileGroupDescriptorFromTransferFileList(dryRunFileList_);

    if (options_.dryRun) {
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        ++dryRunSequence_;
        ++diagnostics_.fileListRenders;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    if (dryRunFileGroupDescriptor_.empty()) {
        diagnostics_.lastMessage = "windows file group descriptor is empty";
        return protocol::ResponseStatus::InvalidArgument;
    }
    if (remoteFileReader_ == nullptr) {
        diagnostics_.lastMessage =
            "windows file clipboard publishing requires remote file reader";
        return protocol::ResponseStatus::InvalidArgument;
    }

    TransferSourceId sourceId = 0;
    for (const std::shared_ptr<TransferSource>& source : publishedBundle_.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(),
                        formats.end(),
                        windowsFileListDescriptorMatches)) {
            sourceId = source->id();
            break;
        }
    }
    if (sourceId == 0) {
        diagnostics_.lastMessage = "windows file clipboard source is missing";
        return protocol::ResponseStatus::NotFound;
    }

    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole) && ole != RPC_E_CHANGED_MODE) {
        recordNativeFailure(static_cast<std::uint32_t>(ole),
                            "OleInitialize failed for file clipboard");
        return protocol::ResponseStatus::Failed;
    }

    auto* dataObject = createRemoteFileDataObject(
        publishedBundle_,
        dryRunFileList_,
        dryRunFileGroupDescriptor_,
        sourceId,
        options_.maxFileRangeBytes,
        options_.delayedReadTimeoutMs,
        remoteFileReader_,
        remoteObjectLocker_);
    const HRESULT hr = OleSetClipboard(dataObject);
    dataObject->Release();
    if (FAILED(hr)) {
        recordNativeFailure(static_cast<std::uint32_t>(hr),
                            "OleSetClipboard failed for file clipboard");
        return protocol::ResponseStatus::Failed;
    }

    (void)offerId;
    (void)ownerEpoch;
    (void)sequence;
    ++diagnostics_.fileListRenders;
    return protocol::ResponseStatus::Ok;
#else
    (void)offerId;
    (void)ownerEpoch;
    (void)sequence;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedText(
    const protocol::ByteBuffer& canonicalUtf8)
{
    if (options_.dryRun) {
        dryRunText_ = bytesToString(canonicalUtf8);
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    HANDLE textHandle =
        allocMoveableBytes(windowsCfUnicodeTextFromCanonicalUtf8(canonicalUtf8));
    if (textHandle == nullptr) {
        recordNativeFailure(GetLastError(), "GlobalAlloc failed for delayed CF_UNICODETEXT");
        return protocol::ResponseStatus::Failed;
    }

    if (SetClipboardData(CF_UNICODETEXT, textHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(textHandle);
        recordNativeFailure(error, "SetClipboardData delayed CF_UNICODETEXT render failed");
        return protocol::ResponseStatus::Failed;
    }

    return protocol::ResponseStatus::Ok;
#else
    (void)canonicalUtf8;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus
WindowsClipboardEndpoint::nativeSetRenderedTextWithOpenClipboard(
    const protocol::ByteBuffer& canonicalUtf8)
{
    if (options_.dryRun) {
        dryRunText_ = bytesToString(canonicalUtf8);
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }
    if (GetClipboardOwner() != owner)
        return protocol::ResponseStatus::Conflict;

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while rendering all delayed clipboard formats");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    if (GetClipboardOwner() != owner) {
        CloseClipboard();
        return protocol::ResponseStatus::Conflict;
    }

    HANDLE textHandle =
        allocMoveableBytes(windowsCfUnicodeTextFromCanonicalUtf8(canonicalUtf8));
    if (textHandle == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GlobalAlloc failed for WM_RENDERALLFORMATS CF_UNICODETEXT");
        return protocol::ResponseStatus::Failed;
    }

    if (SetClipboardData(CF_UNICODETEXT, textHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(textHandle);
        CloseClipboard();
        recordNativeFailure(error, "SetClipboardData WM_RENDERALLFORMATS CF_UNICODETEXT failed");
        return protocol::ResponseStatus::Failed;
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)canonicalUtf8;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedHtml(
    const protocol::ByteBuffer& canonicalHtml)
{
    if (options_.dryRun) {
        dryRunHtml_ = bytesToString(canonicalHtml);
        dryRunText_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const UINT token = htmlFormat();
    if (token == 0) {
        recordNativeFailure(GetLastError(), "RegisterClipboardFormat(HTML Format) failed");
        return protocol::ResponseStatus::Failed;
    }

    HANDLE htmlHandle = allocMoveableBytes(windowsHtmlFromCanonicalHtml(canonicalHtml));
    if (htmlHandle == nullptr) {
        recordNativeFailure(GetLastError(), "GlobalAlloc failed for delayed HTML Format");
        return protocol::ResponseStatus::Failed;
    }

    if (SetClipboardData(token, htmlHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(htmlHandle);
        recordNativeFailure(error, "SetClipboardData delayed HTML Format render failed");
        return protocol::ResponseStatus::Failed;
    }

    return protocol::ResponseStatus::Ok;
#else
    (void)canonicalHtml;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus
WindowsClipboardEndpoint::nativeSetRenderedHtmlWithOpenClipboard(
    const protocol::ByteBuffer& canonicalHtml)
{
    if (options_.dryRun) {
        dryRunHtml_ = bytesToString(canonicalHtml);
        dryRunText_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }
    if (GetClipboardOwner() != owner)
        return protocol::ResponseStatus::Conflict;

    const UINT token = htmlFormat();
    if (token == 0) {
        recordNativeFailure(GetLastError(), "RegisterClipboardFormat(HTML Format) failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while rendering all delayed HTML formats");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    if (GetClipboardOwner() != owner) {
        CloseClipboard();
        return protocol::ResponseStatus::Conflict;
    }

    HANDLE htmlHandle = allocMoveableBytes(windowsHtmlFromCanonicalHtml(canonicalHtml));
    if (htmlHandle == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GlobalAlloc failed for WM_RENDERALLFORMATS HTML Format");
        return protocol::ResponseStatus::Failed;
    }

    if (SetClipboardData(token, htmlHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(htmlHandle);
        CloseClipboard();
        recordNativeFailure(error, "SetClipboardData WM_RENDERALLFORMATS HTML Format failed");
        return protocol::ResponseStatus::Failed;
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)canonicalHtml;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedRtf(
    const protocol::ByteBuffer& canonicalRtf)
{
    if (options_.dryRun) {
        dryRunRtf_ = bytesToString(canonicalRtf);
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const UINT token = rtfFormat();
    if (token == 0) {
        recordNativeFailure(GetLastError(), "RegisterClipboardFormat(Rich Text Format) failed");
        return protocol::ResponseStatus::Failed;
    }

    HANDLE rtfHandle = allocMoveableBytes(canonicalRtf);
    if (rtfHandle == nullptr) {
        recordNativeFailure(GetLastError(), "GlobalAlloc failed for delayed Rich Text Format");
        return protocol::ResponseStatus::Failed;
    }

    if (SetClipboardData(token, rtfHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(rtfHandle);
        recordNativeFailure(error, "SetClipboardData delayed Rich Text Format render failed");
        return protocol::ResponseStatus::Failed;
    }

    return protocol::ResponseStatus::Ok;
#else
    (void)canonicalRtf;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus
WindowsClipboardEndpoint::nativeSetRenderedRtfWithOpenClipboard(
    const protocol::ByteBuffer& canonicalRtf)
{
    if (options_.dryRun) {
        dryRunRtf_ = bytesToString(canonicalRtf);
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }
    if (GetClipboardOwner() != owner)
        return protocol::ResponseStatus::Conflict;

    const UINT token = rtfFormat();
    if (token == 0) {
        recordNativeFailure(GetLastError(), "RegisterClipboardFormat(Rich Text Format) failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while rendering all Rich Text formats");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    if (GetClipboardOwner() != owner) {
        CloseClipboard();
        return protocol::ResponseStatus::Conflict;
    }

    HANDLE rtfHandle = allocMoveableBytes(canonicalRtf);
    if (rtfHandle == nullptr) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "GlobalAlloc failed for WM_RENDERALLFORMATS Rich Text Format");
        return protocol::ResponseStatus::Failed;
    }

    if (SetClipboardData(token, rtfHandle) == nullptr) {
        const DWORD error = GetLastError();
        GlobalFree(rtfHandle);
        CloseClipboard();
        recordNativeFailure(error, "SetClipboardData WM_RENDERALLFORMATS Rich Text Format failed");
        return protocol::ResponseStatus::Failed;
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)canonicalRtf;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedFileList(
    const protocol::ByteBuffer& canonicalFileList)
{
    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(canonicalFileList,
                               options_.maxFileCount,
                               255);
    if (!decoded.ok) {
        diagnostics_.lastMessage = decoded.message;
        return decoded.status;
    }

    if (options_.dryRun) {
        dryRunFileList_ = decoded.fileList;
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_ =
            windowsFileGroupDescriptorFromTransferFileList(dryRunFileList_);
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunImagePng_.clear();
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        ++dryRunSequence_;
        ++diagnostics_.fileListRenders;
        return protocol::ResponseStatus::Ok;
    }

    return nativePublishFileList(publishedBundle_.offerId,
                                 publishedBundle_.ownerEpoch,
                                 publishedBundle_.sequence,
                                 canonicalFileList);
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
