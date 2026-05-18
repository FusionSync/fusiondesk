#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

constexpr const char* WindowsHtmlName = "HTML Format";
constexpr const char* WindowsRtfName = "Rich Text Format";
constexpr const char* WindowsPngName = "PNG";
constexpr const wchar_t* WindowsFileGroupDescriptorWideName =
    L"FileGroupDescriptorW";

std::string normalizeCrlfToLf(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r' && i + 1 < value.size() &&
            value[i + 1] == '\n') {
            result.push_back('\n');
            ++i;
            continue;
        }
        result.push_back(value[i]);
    }
    return result;
}

std::string normalizeLfToCrlf(const std::string& value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\r') {
            result.push_back('\r');
            if (i + 1 < value.size() && value[i + 1] == '\n') {
                result.push_back('\n');
                ++i;
            } else {
                result.push_back('\n');
            }
            continue;
        }
        if (ch == '\n') {
            result.push_back('\r');
            result.push_back('\n');
            continue;
        }
        result.push_back(ch);
    }
    return result;
}

std::string bytesToString(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

protocol::ByteBuffer stringToBytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

bool splitFileSize(std::uint64_t sizeBytes,
                   std::uint32_t& high,
                   std::uint32_t& low)
{
    high = static_cast<std::uint32_t>((sizeBytes >> 32U) & 0xffffffffULL);
    low = static_cast<std::uint32_t>(sizeBytes & 0xffffffffULL);
    return true;
}

#if defined(_WIN32)
std::wstring utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    int chars = MultiByteToWideChar(CP_UTF8,
                                    MB_ERR_INVALID_CHARS,
                                    value.data(),
                                    static_cast<int>(value.size()),
                                    nullptr,
                                    0);
    if (chars <= 0) {
        chars = MultiByteToWideChar(CP_UTF8,
                                    0,
                                    value.data(),
                                    static_cast<int>(value.size()),
                                    nullptr,
                                    0);
    }
    if (chars <= 0)
        return {};

    std::wstring result(static_cast<std::size_t>(chars), L'\0');
    MultiByteToWideChar(CP_UTF8,
                        0,
                        value.data(),
                        static_cast<int>(value.size()),
                        &result[0],
                        chars);
    return result;
}

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

FILETIME fileTimeFromUnixUsec(std::uint64_t unixUsec)
{
    FILETIME result = {};
    if (unixUsec == 0)
        return result;

    constexpr std::uint64_t windowsToUnixEpoch100Ns = 116444736000000000ULL;
    ULARGE_INTEGER ticks;
    ticks.QuadPart = windowsToUnixEpoch100Ns + unixUsec * 10ULL;
    result.dwLowDateTime = ticks.LowPart;
    result.dwHighDateTime = ticks.HighPart;
    return result;
}

std::string displayNameFromRelativePath(const std::string& relativePath)
{
    const std::size_t slash = relativePath.find_last_of("/\\");
    const std::string leaf =
        slash == std::string::npos ? relativePath : relativePath.substr(slash + 1);
    return sanitizeTransferFileDisplayName(leaf);
}

void copyRelativePathToFileDescriptor(const TransferFileDescriptor& file,
                                      FILEDESCRIPTORW& descriptor)
{
    std::string relativePath = sanitizeTransferFileRelativePath(file.relativePath);
    if (relativePath.empty() || relativePath == "unnamed")
        relativePath = sanitizeTransferFileDisplayName(file.displayName);
    std::replace(relativePath.begin(), relativePath.end(), '/', '\\');

    std::wstring wideName = utf8ToWide(relativePath);
    if (wideName.empty())
        wideName = L"unnamed";
    if (wideName.size() >= MAX_PATH)
        wideName.resize(MAX_PATH - 1);

    std::wmemset(descriptor.cFileName, 0, MAX_PATH);
    std::wmemcpy(descriptor.cFileName, wideName.data(), wideName.size());
}

std::string relativePathFromFileDescriptor(const FILEDESCRIPTORW& descriptor)
{
    std::size_t characters = 0;
    while (characters < MAX_PATH && descriptor.cFileName[characters] != L'\0')
        ++characters;

    std::string path = wideToUtf8(descriptor.cFileName, characters);
    std::replace(path.begin(), path.end(), '\\', '/');
    return sanitizeTransferFileRelativePath(path);
}

UINT htmlFormat()
{
    return RegisterClipboardFormatA(WindowsHtmlName);
}

UINT rtfFormat()
{
    return RegisterClipboardFormatA(WindowsRtfName);
}

UINT pngFormat()
{
    return RegisterClipboardFormatA(WindowsPngName);
}

UINT fileGroupDescriptorFormat()
{
    return RegisterClipboardFormatW(WindowsFileGroupDescriptorWideName);
}
#endif

std::uint64_t decimalField(const std::string& value,
                           const std::string& name,
                           std::uint64_t fallback)
{
    const std::size_t pos = value.find(name);
    if (pos == std::string::npos)
        return fallback;

    std::size_t index = pos + name.size();
    while (index < value.size() && value[index] == ' ')
        ++index;

    std::uint64_t result = 0;
    bool found = false;
    while (index < value.size() && value[index] >= '0' &&
           value[index] <= '9') {
        found = true;
        result = result * 10U + static_cast<std::uint64_t>(value[index] - '0');
        ++index;
    }
    return found ? result : fallback;
}

} // namespace

protocol::ByteBuffer windowsCfUnicodeTextFromCanonicalUtf8(
    const protocol::ByteBuffer& canonicalUtf8)
{
    const std::string normalized =
        normalizeLfToCrlf(bytesToString(canonicalUtf8));

#if defined(_WIN32)
    const std::wstring wide = utf8ToWide(normalized);
    protocol::ByteBuffer result((wide.size() + 1) * sizeof(wchar_t), 0);
    if (!wide.empty()) {
        std::memcpy(result.data(),
                    wide.data(),
                    wide.size() * sizeof(wchar_t));
    }
    return result;
#else
    protocol::ByteBuffer result;
    result.reserve((normalized.size() + 1) * 2);
    for (char ch : normalized) {
        result.push_back(static_cast<std::uint8_t>(ch));
        result.push_back(0);
    }
    result.push_back(0);
    result.push_back(0);
    return result;
#endif
}

protocol::ByteBuffer canonicalUtf8FromWindowsCfUnicodeText(
    const protocol::ByteBuffer& cfUnicodeText)
{
    if (cfUnicodeText.empty())
        return {};

#if defined(_WIN32)
    const auto* wide =
        reinterpret_cast<const wchar_t*>(cfUnicodeText.data());
    const std::size_t maxCharacters = cfUnicodeText.size() / sizeof(wchar_t);
    std::size_t characters = 0;
    while (characters < maxCharacters && wide[characters] != L'\0')
        ++characters;
    return stringToBytes(normalizeCrlfToLf(wideToUtf8(wide, characters)));
#else
    std::string value;
    value.reserve(cfUnicodeText.size() / 2);
    for (std::size_t i = 0; i + 1 < cfUnicodeText.size(); i += 2) {
        if (cfUnicodeText[i] == 0 && cfUnicodeText[i + 1] == 0)
            break;
        value.push_back(static_cast<char>(cfUnicodeText[i]));
    }
    return stringToBytes(normalizeCrlfToLf(value));
#endif
}

protocol::ByteBuffer windowsHtmlFromCanonicalHtml(
    const protocol::ByteBuffer& canonicalHtml)
{
    const std::string fragment = bytesToString(canonicalHtml);
    const std::string startMarker = "<!--StartFragment-->";
    const std::string endMarker = "<!--EndFragment-->";
    const std::string html =
        "<html><body>" + startMarker + fragment + endMarker + "</body></html>";
    std::string header =
        "Version:1.0\r\n"
        "StartHTML:0000000000\r\n"
        "EndHTML:0000000000\r\n"
        "StartFragment:0000000000\r\n"
        "EndFragment:0000000000\r\n";

    const std::size_t startHtml = header.size();
    const std::size_t startFragment =
        startHtml + html.find(startMarker) + startMarker.size();
    const std::size_t endFragment = startHtml + html.find(endMarker);
    const std::size_t endHtml = startHtml + html.size();

    auto patchField = [&header](const char* name, std::size_t value) {
        const std::size_t pos = header.find(name);
        if (pos == std::string::npos)
            return;
        char buffer[11] = {};
        std::snprintf(buffer, sizeof(buffer), "%010zu", value);
        header.replace(pos + std::strlen(name), 10, buffer);
    };
    patchField("StartHTML:", startHtml);
    patchField("EndHTML:", endHtml);
    patchField("StartFragment:", startFragment);
    patchField("EndFragment:", endFragment);

    return stringToBytes(header + html);
}

protocol::ByteBuffer canonicalHtmlFromWindowsHtml(
    const protocol::ByteBuffer& windowsHtml)
{
    const std::string value = bytesToString(windowsHtml);
    if (value.empty())
        return {};

    const std::uint64_t start = decimalField(value, "StartFragment:", 0);
    const std::uint64_t end =
        decimalField(value,
                     "EndFragment:",
                     static_cast<std::uint64_t>(value.size()));
    if (start < end && end <= value.size()) {
        return stringToBytes(value.substr(static_cast<std::size_t>(start),
                                          static_cast<std::size_t>(end - start)));
    }

    const std::string startMarker = "<!--StartFragment-->";
    const std::string endMarker = "<!--EndFragment-->";
    const std::size_t startMarkerPos = value.find(startMarker);
    const std::size_t endMarkerPos = value.find(endMarker);
    if (startMarkerPos != std::string::npos &&
        endMarkerPos != std::string::npos &&
        startMarkerPos + startMarker.size() <= endMarkerPos) {
        const std::size_t fragmentStart = startMarkerPos + startMarker.size();
        return stringToBytes(value.substr(fragmentStart,
                                          endMarkerPos - fragmentStart));
    }

    return windowsHtml;
}

protocol::ByteBuffer windowsFileGroupDescriptorFromTransferFileList(
    const TransferFileList& fileList)
{
#if defined(_WIN32)
    if (fileList.files.empty())
        return {};

    const std::size_t headerBytes = offsetof(FILEGROUPDESCRIPTORW, fgd);
    const std::size_t descriptorBytes =
        sizeof(FILEDESCRIPTORW) * fileList.files.size();
    protocol::ByteBuffer bytes(headerBytes + descriptorBytes, 0);

    const UINT count = static_cast<UINT>(fileList.files.size());
    std::memcpy(bytes.data(), &count, sizeof(count));

    for (std::size_t index = 0; index < fileList.files.size(); ++index) {
        const TransferFileDescriptor& file = fileList.files[index];
        FILEDESCRIPTORW descriptor = {};
        descriptor.dwFlags = FD_UNICODE | FD_ATTRIBUTES | FD_FILESIZE;
        descriptor.dwFileAttributes =
            file.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        std::uint32_t high = 0;
        std::uint32_t low = 0;
        splitFileSize(file.directory ? 0 : file.sizeBytes, high, low);
        descriptor.nFileSizeHigh = high;
        descriptor.nFileSizeLow = low;
        if (file.lastModifiedUnixUsec != 0) {
            descriptor.dwFlags |= FD_WRITESTIME;
            descriptor.ftLastWriteTime =
                fileTimeFromUnixUsec(file.lastModifiedUnixUsec);
        }
        copyRelativePathToFileDescriptor(file, descriptor);
        std::memcpy(bytes.data() + headerBytes + index * sizeof(FILEDESCRIPTORW),
                    &descriptor,
                    sizeof(descriptor));
    }
    return bytes;
#else
    (void)fileList;
    return {};
#endif
}

TransferFileListDecodeResult transferFileListFromWindowsFileGroupDescriptor(
    const protocol::ByteBuffer& fileGroupDescriptor)
{
    TransferFileListDecodeResult result;
#if defined(_WIN32)
    const std::size_t headerBytes = offsetof(FILEGROUPDESCRIPTORW, fgd);
    if (fileGroupDescriptor.size() < headerBytes) {
        result.status = protocol::ResponseStatus::InvalidArgument;
        result.message = "windows file group descriptor is truncated";
        return result;
    }

    UINT rawCount = 0;
    std::memcpy(&rawCount, fileGroupDescriptor.data(), sizeof(rawCount));
    const std::size_t count = static_cast<std::size_t>(rawCount);
    if (count > 1024) {
        result.status = protocol::ResponseStatus::TooLarge;
        result.message = "windows file group descriptor has too many files";
        return result;
    }
    const std::size_t requiredBytes =
        headerBytes + count * sizeof(FILEDESCRIPTORW);
    if (fileGroupDescriptor.size() < requiredBytes) {
        result.status = protocol::ResponseStatus::InvalidArgument;
        result.message = "windows file group descriptor is truncated";
        return result;
    }

    result.fileList.files.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        FILEDESCRIPTORW descriptor = {};
        std::memcpy(&descriptor,
                    fileGroupDescriptor.data() + headerBytes +
                        index * sizeof(FILEDESCRIPTORW),
                    sizeof(descriptor));
        TransferFileDescriptor file;
        file.objectId = static_cast<TransferObjectId>(index + 1);
        file.relativePath = relativePathFromFileDescriptor(descriptor);
        file.displayName = displayNameFromRelativePath(file.relativePath);
        file.directory =
            (descriptor.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if ((descriptor.dwFlags & FD_FILESIZE) != 0 && !file.directory) {
            file.sizeBytes =
                (static_cast<std::uint64_t>(descriptor.nFileSizeHigh) << 32U) |
                descriptor.nFileSizeLow;
        }
        if ((descriptor.dwFlags & FD_WRITESTIME) != 0)
            file.lastModifiedUnixUsec =
                unixUsecFromFileTime(descriptor.ftLastWriteTime);
        result.fileList.files.push_back(std::move(file));
    }

    result.ok = true;
    result.status = protocol::ResponseStatus::Ok;
    return result;
#else
    (void)fileGroupDescriptor;
    result.status = protocol::ResponseStatus::Unsupported;
    result.message = "windows file group descriptor conversion is unsupported";
    return result;
#endif
}

std::uint32_t windowsHtmlFormatToken()
{
#if defined(_WIN32)
    return htmlFormat();
#else
    return 0;
#endif
}

std::uint32_t windowsRtfFormatToken()
{
#if defined(_WIN32)
    return rtfFormat();
#else
    return 0;
#endif
}

std::uint32_t windowsPngFormatToken()
{
#if defined(_WIN32)
    return pngFormat();
#else
    return 0;
#endif
}

std::uint32_t windowsFileGroupDescriptorFormatToken()
{
#if defined(_WIN32)
    return fileGroupDescriptorFormat();
#else
    return 0;
#endif
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
