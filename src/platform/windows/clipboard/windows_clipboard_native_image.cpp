#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_image_transcoding.h"
#include "windows_clipboard_native_helpers.h"

#include <cstring>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

#if defined(_WIN32)
namespace {

bool setClipboardBytes(UINT format,
                       const protocol::ByteBuffer& bytes,
                       DWORD& error)
{
    HANDLE handle = allocMoveableBytes(bytes);
    if (handle == nullptr) {
        error = GetLastError();
        return false;
    }
    if (SetClipboardData(format, handle) == nullptr) {
        error = GetLastError();
        GlobalFree(handle);
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

protocol::ByteBuffer dibBytesForToken(const protocol::ByteBuffer& png,
                                      std::uint32_t nativeFormatToken)
{
    if (nativeFormatToken == CF_DIBV5)
        return windowsDibV5FromPngBytes(png);
    if (nativeFormatToken == CF_DIB)
        return windowsDibFromPngBytes(png);
    return {};
}

std::uint32_t nativeDibTokenForBytes(const protocol::ByteBuffer& dib)
{
    std::uint32_t headerSize = 0;
    if (dib.size() >= sizeof(headerSize))
        std::memcpy(&headerSize, dib.data(), sizeof(headerSize));
    return headerSize == sizeof(BITMAPV5HEADER) ? CF_DIBV5 : CF_DIB;
}

protocol::ByteBuffer dibBytesForTokenFromDib(
    const protocol::ByteBuffer& dib,
    std::uint32_t nativeFormatToken)
{
    if (nativeDibTokenForBytes(dib) == nativeFormatToken)
        return dib;
    const protocol::ByteBuffer png = windowsPngFromDibBytes(dib);
    if (png.empty())
        return {};
    return dibBytesForToken(png, nativeFormatToken);
}

} // namespace
#endif

protocol::ResponseStatus WindowsClipboardEndpoint::nativePublishImagePng(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence,
    const protocol::ByteBuffer& png)
{
    if (png.empty())
        return protocol::ResponseStatus::InvalidArgument;

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while publishing PNG clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }

    if (!EmptyClipboard()) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "EmptyClipboard failed while publishing PNG clipboard");
        return protocol::ResponseStatus::Failed;
    }

    const UINT token = pngFormat();
    if (token == 0) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "RegisterClipboardFormat(PNG) failed");
        return protocol::ResponseStatus::Failed;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(token, png, setError)) {
        CloseClipboard();
        recordNativeFailure(setError, "SetClipboardData(PNG) failed");
        return protocol::ResponseStatus::Failed;
    }

    const protocol::ByteBuffer dibV5 = windowsDibV5FromPngBytes(png);
    if (!dibV5.empty() &&
        !setClipboardBytes(CF_DIBV5, dibV5, setError)) {
        CloseClipboard();
        recordNativeFailure(setError, "SetClipboardData(CF_DIBV5) failed");
        return protocol::ResponseStatus::Failed;
    }

    const protocol::ByteBuffer dib = windowsDibFromPngBytes(png);
    if (!dib.empty() &&
        !setClipboardBytes(CF_DIB, dib, setError)) {
        CloseClipboard();
        recordNativeFailure(setError, "SetClipboardData(CF_DIB) failed");
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
    (void)png;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedImagePng(
    const protocol::ByteBuffer& png)
{
    if (png.empty())
        return protocol::ResponseStatus::InvalidArgument;

    if (options_.dryRun) {
        dryRunImagePng_ = png;
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const UINT token = pngFormat();
    if (token == 0) {
        recordNativeFailure(GetLastError(), "RegisterClipboardFormat(PNG) failed");
        return protocol::ResponseStatus::Failed;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(token, png, setError)) {
        recordNativeFailure(setError, "SetClipboardData delayed PNG render failed");
        return protocol::ResponseStatus::Failed;
    }

    return protocol::ResponseStatus::Ok;
#else
    (void)png;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus
WindowsClipboardEndpoint::nativeSetRenderedImagePngWithOpenClipboard(
    const protocol::ByteBuffer& png)
{
    if (png.empty())
        return protocol::ResponseStatus::InvalidArgument;

    if (options_.dryRun) {
        dryRunImagePng_ = png;
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
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

    const UINT token = pngFormat();
    if (token == 0) {
        recordNativeFailure(GetLastError(), "RegisterClipboardFormat(PNG) failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while rendering all PNG formats");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    if (GetClipboardOwner() != owner) {
        CloseClipboard();
        return protocol::ResponseStatus::Conflict;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(token, png, setError)) {
        CloseClipboard();
        recordNativeFailure(setError, "SetClipboardData WM_RENDERALLFORMATS PNG failed");
        return protocol::ResponseStatus::Failed;
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)png;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativePublishImageDib(
    TransferOfferId offerId,
    std::uint64_t ownerEpoch,
    std::uint64_t sequence,
    const protocol::ByteBuffer& dib)
{
    if (dib.empty())
        return protocol::ResponseStatus::InvalidArgument;

#if defined(_WIN32)
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr) {
        recordNativeFailure(GetLastError(), "clipboard owner window creation failed");
        return protocol::ResponseStatus::Failed;
    }

    if (!tryOpenClipboard(owner,
                          options_.openRetryCount,
                          options_.openRetryDelayMs)) {
        recordNativeFailure(GetLastError(), "OpenClipboard failed while publishing DIB clipboard");
        return protocol::ResponseStatus::ChannelUnavailable;
    }

    if (!EmptyClipboard()) {
        const DWORD error = GetLastError();
        CloseClipboard();
        recordNativeFailure(error, "EmptyClipboard failed while publishing DIB clipboard");
        return protocol::ResponseStatus::Failed;
    }

    DWORD setError = ERROR_SUCCESS;
    const std::uint32_t dibToken = nativeDibTokenForBytes(dib);
    if (!setClipboardBytes(static_cast<UINT>(dibToken), dib, setError)) {
        CloseClipboard();
        recordNativeFailure(setError,
                            dibToken == CF_DIBV5 ?
                                "SetClipboardData(CF_DIBV5) failed" :
                                "SetClipboardData(CF_DIB) failed");
        return protocol::ResponseStatus::Failed;
    }

    const protocol::ByteBuffer png = windowsPngFromDibBytes(dib);
    if (!png.empty()) {
        const UINT pngToken = pngFormat();
        if (pngToken != 0 &&
            !setClipboardBytes(pngToken, png, setError)) {
            CloseClipboard();
            recordNativeFailure(setError, "SetClipboardData(PNG) failed");
            return protocol::ResponseStatus::Failed;
        }
        const std::uint32_t companionToken =
            dibToken == CF_DIBV5 ? CF_DIB : CF_DIBV5;
        const protocol::ByteBuffer companion =
            dibBytesForToken(png, companionToken);
        if (!companion.empty() &&
            !setClipboardBytes(static_cast<UINT>(companionToken),
                               companion,
                               setError)) {
            CloseClipboard();
            recordNativeFailure(setError,
                                companionToken == CF_DIBV5 ?
                                    "SetClipboardData(CF_DIBV5) failed" :
                                    "SetClipboardData(CF_DIB) failed");
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
    return protocol::ResponseStatus::Ok;
#else
    (void)offerId;
    (void)ownerEpoch;
    (void)sequence;
    (void)dib;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedImageDib(
    const protocol::ByteBuffer& png,
    std::uint32_t nativeFormatToken)
{
    if (png.empty())
        return protocol::ResponseStatus::InvalidArgument;

    if (options_.dryRun) {
        dryRunImagePng_ = png;
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const protocol::ByteBuffer dib =
        dibBytesForToken(png, nativeFormatToken);
    if (dib.empty()) {
        recordNativeFailure(ERROR_INVALID_DATA,
                            nativeFormatToken == CF_DIBV5 ?
                                "PNG to CF_DIBV5 conversion failed" :
                                "PNG to CF_DIB conversion failed");
        return protocol::ResponseStatus::Failed;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(static_cast<UINT>(nativeFormatToken),
                           dib,
                           setError)) {
        recordNativeFailure(setError,
                            nativeFormatToken == CF_DIBV5 ?
                                "SetClipboardData delayed CF_DIBV5 render failed" :
                                "SetClipboardData delayed CF_DIB render failed");
        return protocol::ResponseStatus::Failed;
    }

    return protocol::ResponseStatus::Ok;
#else
    (void)nativeFormatToken;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus WindowsClipboardEndpoint::nativeSetRenderedImageDibBytes(
    const protocol::ByteBuffer& dib,
    std::uint32_t nativeFormatToken)
{
    if (dib.empty())
        return protocol::ResponseStatus::InvalidArgument;

    if (options_.dryRun) {
        dryRunImageDib_ = dib;
        dryRunImageDibNativeFormatName_ =
            nativeFormatToken == 17 ? WindowsDibV5Name : WindowsDibName;
        dryRunImageDibFormatToken_ = nativeFormatToken;
        dryRunImagePng_.clear();
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const protocol::ByteBuffer rendered =
        dibBytesForTokenFromDib(dib, nativeFormatToken);
    if (rendered.empty()) {
        recordNativeFailure(ERROR_INVALID_DATA,
                            nativeFormatToken == CF_DIBV5 ?
                                "CF_DIB to CF_DIBV5 conversion failed" :
                                "CF_DIBV5 to CF_DIB conversion failed");
        return protocol::ResponseStatus::Failed;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(static_cast<UINT>(nativeFormatToken),
                           rendered,
                           setError)) {
        recordNativeFailure(setError,
                            nativeFormatToken == CF_DIBV5 ?
                                "SetClipboardData delayed CF_DIBV5 render failed" :
                                "SetClipboardData delayed CF_DIB render failed");
        return protocol::ResponseStatus::Failed;
    }

    return protocol::ResponseStatus::Ok;
#else
    (void)nativeFormatToken;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus
WindowsClipboardEndpoint::nativeSetRenderedImageDibWithOpenClipboard(
    const protocol::ByteBuffer& png,
    std::uint32_t nativeFormatToken)
{
    if (png.empty())
        return protocol::ResponseStatus::InvalidArgument;

    if (options_.dryRun) {
        dryRunImagePng_ = png;
        dryRunImageDib_.clear();
        dryRunImageDibNativeFormatName_.clear();
        dryRunImageDibFormatToken_ = 0;
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const protocol::ByteBuffer dib =
        dibBytesForToken(png, nativeFormatToken);
    if (dib.empty()) {
        recordNativeFailure(ERROR_INVALID_DATA,
                            nativeFormatToken == CF_DIBV5 ?
                                "PNG to CF_DIBV5 conversion failed" :
                                "PNG to CF_DIB conversion failed");
        return protocol::ResponseStatus::Failed;
    }

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
        recordNativeFailure(GetLastError(), "OpenClipboard failed while rendering all DIB formats");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    if (GetClipboardOwner() != owner) {
        CloseClipboard();
        return protocol::ResponseStatus::Conflict;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(static_cast<UINT>(nativeFormatToken),
                           dib,
                           setError)) {
        CloseClipboard();
        recordNativeFailure(setError,
                            nativeFormatToken == CF_DIBV5 ?
                                "SetClipboardData WM_RENDERALLFORMATS CF_DIBV5 failed" :
                                "SetClipboardData WM_RENDERALLFORMATS CF_DIB failed");
        return protocol::ResponseStatus::Failed;
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)nativeFormatToken;
    return protocol::ResponseStatus::Unsupported;
#endif
}

protocol::ResponseStatus
WindowsClipboardEndpoint::nativeSetRenderedImageDibBytesWithOpenClipboard(
    const protocol::ByteBuffer& dib,
    std::uint32_t nativeFormatToken)
{
    if (dib.empty())
        return protocol::ResponseStatus::InvalidArgument;

    if (options_.dryRun) {
        dryRunImageDib_ = dib;
        dryRunImageDibNativeFormatName_ =
            nativeFormatToken == 17 ? WindowsDibV5Name : WindowsDibName;
        dryRunImageDibFormatToken_ = nativeFormatToken;
        dryRunImagePng_.clear();
        dryRunText_.clear();
        dryRunHtml_.clear();
        dryRunRtf_.clear();
        dryRunFileList_ = {};
        dryRunFilePaths_.clear();
        dryRunFileGroupDescriptor_.clear();
        ++dryRunSequence_;
        return protocol::ResponseStatus::Ok;
    }

#if defined(_WIN32)
    const protocol::ByteBuffer rendered =
        dibBytesForTokenFromDib(dib, nativeFormatToken);
    if (rendered.empty()) {
        recordNativeFailure(ERROR_INVALID_DATA,
                            nativeFormatToken == CF_DIBV5 ?
                                "CF_DIB to CF_DIBV5 conversion failed" :
                                "CF_DIBV5 to CF_DIB conversion failed");
        return protocol::ResponseStatus::Failed;
    }

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
        recordNativeFailure(GetLastError(), "OpenClipboard failed while rendering all raw DIB formats");
        return protocol::ResponseStatus::ChannelUnavailable;
    }
    if (GetClipboardOwner() != owner) {
        CloseClipboard();
        return protocol::ResponseStatus::Conflict;
    }

    DWORD setError = ERROR_SUCCESS;
    if (!setClipboardBytes(static_cast<UINT>(nativeFormatToken),
                           rendered,
                           setError)) {
        CloseClipboard();
        recordNativeFailure(setError,
                            nativeFormatToken == CF_DIBV5 ?
                                "SetClipboardData WM_RENDERALLFORMATS CF_DIBV5 failed" :
                                "SetClipboardData WM_RENDERALLFORMATS CF_DIB failed");
        return protocol::ResponseStatus::Failed;
    }

    CloseClipboard();
    return protocol::ResponseStatus::Ok;
#else
    (void)nativeFormatToken;
    return protocol::ResponseStatus::Unsupported;
#endif
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
