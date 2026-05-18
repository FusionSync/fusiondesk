#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include "windows_clipboard_image_transcoding.h"
#include "windows_clipboard_local_files.h"

#include <string>
#include <utility>

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

using namespace fusiondesk::modules::clipboard;

protocol::ResponseStatus WindowsClipboardEndpoint::renderDelayedFormatForNative(
    std::uint32_t nativeFormatToken)
{
    const std::uint32_t htmlToken = windowsHtmlFormatToken();
    const std::uint32_t rtfToken = windowsRtfFormatToken();
    const std::uint32_t pngToken = windowsPngFormatToken();
    const std::uint32_t fileListToken = windowsFileGroupDescriptorFormatToken();
#if defined(_WIN32)
    const std::uint32_t dibV5Token = CF_DIBV5;
    const std::uint32_t dibToken = CF_DIB;
#else
    const std::uint32_t dibV5Token = 0;
    const std::uint32_t dibToken = 0;
#endif
    if (nativeFormatToken != 13) {
        const bool htmlTokenMatches =
            htmlToken != 0 && nativeFormatToken == htmlToken;
        const bool rtfTokenMatches =
            rtfToken != 0 && nativeFormatToken == rtfToken;
        const bool pngTokenMatches =
            pngToken != 0 && nativeFormatToken == pngToken;
        const bool dibV5TokenMatches =
            dibV5Token != 0 && nativeFormatToken == dibV5Token;
        const bool dibTokenMatches =
            dibToken != 0 && nativeFormatToken == dibToken;
        const bool fileListTokenMatches =
            fileListToken != 0 && nativeFormatToken == fileListToken;
        if (!htmlTokenMatches && !rtfTokenMatches && !pngTokenMatches &&
            !dibV5TokenMatches && !dibTokenMatches && !fileListTokenMatches)
            return protocol::ResponseStatus::Unsupported;
    }
    if (publishedBundle_.offerId == 0)
        return protocol::ResponseStatus::NotFound;

    const bool html = htmlToken != 0 && nativeFormatToken == htmlToken;
    const bool rtf = rtfToken != 0 && nativeFormatToken == rtfToken;
    const bool png = pngToken != 0 && nativeFormatToken == pngToken;
    const bool dibV5 = dibV5Token != 0 && nativeFormatToken == dibV5Token;
    const bool dib = dibToken != 0 && nativeFormatToken == dibToken;
    const bool fileList =
        fileListToken != 0 && nativeFormatToken == fileListToken;
    TransferReadResult result;
    if (fileList) {
        result = readBestFileList(publishedBundle_);
    } else if (png) {
        result = readBestImagePng(publishedBundle_);
        if (!result.ok()) {
            result = readBestImageDib(publishedBundle_);
            if (result.ok()) {
                result.bytes = windowsPngFromDibBytes(result.bytes);
                result.canonicalFormat = ImagePngFormat;
                result.encoding = TransferEncodingMode::Transcoded;
                if (result.bytes.empty()) {
                    result.status = protocol::ResponseStatus::Failed;
                    result.message = "windows DIB to PNG render conversion failed";
                }
            }
        }
    } else if (dibV5 || dib) {
        result = readBestImageDib(publishedBundle_);
        if (!result.ok())
            result = readBestImagePng(publishedBundle_);
    } else if (rtf) {
        result = readBestRtf(publishedBundle_);
    } else if (html) {
        result = readBestHtml(publishedBundle_);
    } else {
        result = readBestText(publishedBundle_);
    }
    if (!result.ok()) {
        ++diagnostics_.readFailures;
        diagnostics_.lastMessage = result.message;
        return result.status;
    }

    ++diagnostics_.delayedRenders;
    if (html) {
        ++diagnostics_.htmlRenders;
        return nativeSetRenderedHtml(result.bytes);
    }
    if (rtf) {
        ++diagnostics_.rtfRenders;
        return nativeSetRenderedRtf(result.bytes);
    }
    if (png) {
        ++diagnostics_.imagePngRenders;
        return nativeSetRenderedImagePng(result.bytes);
    }
    if (dibV5 || dib) {
        ++diagnostics_.imageDibRenders;
        if (result.canonicalFormat == ImageDibFormat)
            return nativeSetRenderedImageDibBytes(result.bytes,
                                                  nativeFormatToken);
        return nativeSetRenderedImageDib(result.bytes, nativeFormatToken);
    }
    if (fileList)
        return nativeSetRenderedFileList(result.bytes);
    return nativeSetRenderedText(result.bytes);
}

protocol::ResponseStatus WindowsClipboardEndpoint::renderAllDelayedFormatsForNative()
{
    if (publishedBundle_.offerId == 0)
        return protocol::ResponseStatus::NotFound;

    int rendered = 0;
    protocol::ResponseStatus lastStatus = protocol::ResponseStatus::NotFound;

    TransferReadResult text = readBestText(publishedBundle_);
    if (text.ok() &&
        nativeSetRenderedTextWithOpenClipboard(text.bytes) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
    } else if (!text.ok()) {
        lastStatus = text.status;
    }

    TransferReadResult html = readBestHtml(publishedBundle_);
    if (html.ok() &&
        nativeSetRenderedHtmlWithOpenClipboard(html.bytes) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.htmlRenders;
    } else if (!html.ok()) {
        lastStatus = html.status;
    }

    TransferReadResult rtf = readBestRtf(publishedBundle_);
    if (rtf.ok() &&
        nativeSetRenderedRtfWithOpenClipboard(rtf.bytes) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.rtfRenders;
    } else if (!rtf.ok()) {
        lastStatus = rtf.status;
    }

    TransferReadResult png = readBestImagePng(publishedBundle_);
    TransferReadResult rawDib = readBestImageDib(publishedBundle_);
    if (!png.ok() && rawDib.ok()) {
        png = rawDib;
        png.bytes = windowsPngFromDibBytes(rawDib.bytes);
        png.canonicalFormat = ImagePngFormat;
        png.encoding = TransferEncodingMode::Transcoded;
        if (png.bytes.empty()) {
            png.status = protocol::ResponseStatus::Failed;
            png.message = "windows DIB to PNG render-all conversion failed";
        }
    }
    if (png.ok() &&
        nativeSetRenderedImagePngWithOpenClipboard(png.bytes) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.imagePngRenders;
    } else if (!png.ok()) {
        lastStatus = png.status;
    }
#if defined(_WIN32)
    if (rawDib.ok() &&
        nativeSetRenderedImageDibBytesWithOpenClipboard(rawDib.bytes, CF_DIBV5) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.imageDibRenders;
    } else if (png.ok() &&
        nativeSetRenderedImageDibWithOpenClipboard(png.bytes, CF_DIBV5) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.imageDibRenders;
    }
    if (rawDib.ok() &&
        nativeSetRenderedImageDibBytesWithOpenClipboard(rawDib.bytes, CF_DIB) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.imageDibRenders;
    } else if (png.ok() &&
        nativeSetRenderedImageDibWithOpenClipboard(png.bytes, CF_DIB) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
        ++diagnostics_.imageDibRenders;
    }
#endif

    TransferReadResult fileList = readBestFileList(publishedBundle_);
    if (fileList.ok() &&
        nativeSetRenderedFileList(fileList.bytes) ==
            protocol::ResponseStatus::Ok) {
        ++rendered;
    } else if (!fileList.ok()) {
        lastStatus = fileList.status;
    }

    if (rendered > 0) {
        ++diagnostics_.delayedRenderAlls;
        diagnostics_.delayedRenders += rendered;
        return protocol::ResponseStatus::Ok;
    }
    ++diagnostics_.readFailures;
    diagnostics_.lastMessage =
        "windows delayed render all found no supported content";
    return lastStatus;
}

void WindowsClipboardEndpoint::setDryRunClipboardText(std::string text)
{
    dryRunText_ = std::move(text);
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
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
}

void WindowsClipboardEndpoint::setDryRunClipboardRtf(std::string rtf)
{
    dryRunText_.clear();
    dryRunHtml_.clear();
    dryRunRtf_ = std::move(rtf);
    dryRunImagePng_.clear();
    dryRunImageDib_.clear();
    dryRunImageDibNativeFormatName_.clear();
    dryRunImageDibFormatToken_ = 0;
    dryRunFileList_ = {};
    dryRunFilePaths_.clear();
    dryRunFileGroupDescriptor_.clear();
    ++dryRunSequence_;
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
}

void WindowsClipboardEndpoint::setDryRunClipboardImagePng(
    protocol::ByteBuffer png)
{
    dryRunText_.clear();
    dryRunHtml_.clear();
    dryRunRtf_.clear();
    dryRunImagePng_ = std::move(png);
    dryRunImageDib_.clear();
    dryRunImageDibNativeFormatName_.clear();
    dryRunImageDibFormatToken_ = 0;
    dryRunFileList_ = {};
    dryRunFilePaths_.clear();
    dryRunFileGroupDescriptor_.clear();
    ++dryRunSequence_;
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
}

void WindowsClipboardEndpoint::setDryRunClipboardImageDib(
    protocol::ByteBuffer dib,
    std::string nativeFormatName,
    std::uint32_t localFormatToken)
{
    dryRunText_.clear();
    dryRunHtml_.clear();
    dryRunRtf_.clear();
    dryRunImagePng_.clear();
    dryRunImageDib_ = std::move(dib);
    dryRunImageDibNativeFormatName_ = std::move(nativeFormatName);
    dryRunImageDibFormatToken_ = localFormatToken;
    dryRunFileList_ = {};
    dryRunFilePaths_.clear();
    dryRunFileGroupDescriptor_.clear();
    ++dryRunSequence_;
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
}

void WindowsClipboardEndpoint::setDryRunClipboardFileList(
    TransferFileList fileList)
{
    for (TransferFileDescriptor& file : fileList.files)
        file.displayName = sanitizeTransferFileDisplayName(file.displayName);

    dryRunText_.clear();
    dryRunHtml_.clear();
    dryRunRtf_.clear();
    dryRunImagePng_.clear();
    dryRunImageDib_.clear();
    dryRunImageDibNativeFormatName_.clear();
    dryRunImageDibFormatToken_ = 0;
    dryRunFileList_ = std::move(fileList);
    dryRunFilePaths_.clear();
    dryRunFileGroupDescriptor_ =
        windowsFileGroupDescriptorFromTransferFileList(dryRunFileList_);
    ++dryRunSequence_;
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
}

#if defined(_WIN32)
protocol::ResponseStatus WindowsClipboardEndpoint::setDryRunClipboardLocalFiles(
    std::vector<std::wstring> nativePaths)
{
    if (!options_.dryRun) {
        diagnostics_.lastMessage =
            "windows local file seed is only available for dry-run clipboard";
        return protocol::ResponseStatus::Unsupported;
    }

    WindowsHdropReadOptions hdropOptions;
    hdropOptions.expandDirectories = options_.expandDroppedDirectories;
    hdropOptions.maxFileCount = options_.maxFileCount;
    hdropOptions.maxDirectoryDepth = options_.maxDirectoryDepth;

    TransferFileList fileList;
    std::vector<std::wstring> filePaths;
    if (!readWindowsFilePathList(fileList,
                                 &filePaths,
                                 nativePaths,
                                 hdropOptions)) {
        diagnostics_.lastMessage =
            "windows local file seed did not produce a file list";
        return protocol::ResponseStatus::NotFound;
    }

    dryRunText_.clear();
    dryRunHtml_.clear();
    dryRunRtf_.clear();
    dryRunImagePng_.clear();
    dryRunImageDib_.clear();
    dryRunImageDibNativeFormatName_.clear();
    dryRunImageDibFormatToken_ = 0;
    dryRunFileList_ = std::move(fileList);
    dryRunFilePaths_ = std::move(filePaths);
    dryRunFileGroupDescriptor_ =
        windowsFileGroupDescriptorFromTransferFileList(dryRunFileList_);
    ++dryRunSequence_;
    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = true;
    return protocol::ResponseStatus::Ok;
}
#endif

std::string WindowsClipboardEndpoint::dryRunClipboardText() const
{
    return dryRunText_;
}

std::string WindowsClipboardEndpoint::dryRunClipboardHtml() const
{
    return dryRunHtml_;
}

std::string WindowsClipboardEndpoint::dryRunClipboardRtf() const
{
    return dryRunRtf_;
}

protocol::ByteBuffer WindowsClipboardEndpoint::dryRunClipboardImagePng() const
{
    return dryRunImagePng_;
}

protocol::ByteBuffer WindowsClipboardEndpoint::dryRunClipboardImageDib() const
{
    return dryRunImageDib_;
}

TransferFileList WindowsClipboardEndpoint::dryRunClipboardFileList() const
{
    return dryRunFileList_;
}

protocol::ByteBuffer WindowsClipboardEndpoint::dryRunFileGroupDescriptor() const
{
    return dryRunFileGroupDescriptor_;
}

WindowsClipboardEndpointDiagnostics
WindowsClipboardEndpoint::diagnostics() const
{
    return diagnostics_;
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
