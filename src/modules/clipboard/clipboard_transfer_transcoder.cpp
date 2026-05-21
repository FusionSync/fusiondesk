#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

constexpr const char* WindowsUnicodeTextName = "CF_UNICODETEXT";
constexpr const char* WindowsHtmlName = "HTML Format";
constexpr const char* WindowsRtfName = "Rich Text Format";
constexpr const char* WindowsPngName = "PNG";
constexpr const char* WindowsDibName = "CF_DIB";
constexpr const char* WindowsDibV5Name = "CF_DIBV5";

constexpr const char* LinuxUtf8StringName = "UTF8_STRING";
constexpr const char* LinuxTextPlainName = "text/plain";
constexpr const char* LinuxTextPlainUtf8Name = "text/plain;charset=utf-8";
constexpr const char* LinuxTextPlainUtf8LooseName = "text/plain;charset=utf8";
constexpr const char* LinuxHtmlName = "text/html";
constexpr const char* LinuxRtfName = "text/rtf";
constexpr const char* LinuxApplicationRtfName = "application/rtf";
constexpr const char* LinuxPngName = "image/png";

constexpr const char* MacUtf8TextName = "public.utf8-plain-text";
constexpr const char* MacUtf16TextName = "public.utf16-plain-text";
constexpr const char* MacHtmlName = "public.html";
constexpr const char* MacRtfName = "public.rtf";
constexpr const char* MacPngName = "public.png";

std::string lowered(std::string value)
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

bool sameName(const std::string& left, const std::string& right)
{
    return lowered(left) == lowered(right);
}

bool nativeNameIs(const NativeTransferFormat& native,
                  const char* expected)
{
    return sameName(native.nativeFormatName, expected);
}

bool sameNativeFormat(const NativeTransferFormat& left,
                      const NativeTransferFormat& right)
{
    if (left.nativeFormatName.empty() &&
        right.nativeFormatName.empty() &&
        left.localFormatToken == 0 &&
        right.localFormatToken == 0) {
        return false;
    }

    if (!left.nativeFormatName.empty() || !right.nativeFormatName.empty()) {
        if (!sameName(left.nativeFormatName, right.nativeFormatName))
            return false;
    }

    if (left.localFormatToken != 0 || right.localFormatToken != 0) {
        if (left.localFormatToken != right.localFormatToken)
            return false;
    }

    return left.platform == TransferPlatformFamily::Unknown ||
           right.platform == TransferPlatformFamily::Unknown ||
           left.platform == right.platform;
}

bool isWindowsUnicodeText(const NativeTransferFormat& native)
{
    return nativeNameIs(native, WindowsUnicodeTextName) ||
           ((native.platform == TransferPlatformFamily::Unknown ||
             native.platform == TransferPlatformFamily::Windows) &&
            native.localFormatToken == 13);
}

bool isUtf8TextNative(const NativeTransferFormat& native)
{
    return nativeNameIs(native, LinuxUtf8StringName) ||
           nativeNameIs(native, LinuxTextPlainName) ||
           nativeNameIs(native, LinuxTextPlainUtf8Name) ||
           nativeNameIs(native, LinuxTextPlainUtf8LooseName) ||
           nativeNameIs(native, MacUtf8TextName);
}

bool isUtf16TextNative(const NativeTransferFormat& native)
{
    return isWindowsUnicodeText(native) || nativeNameIs(native, MacUtf16TextName);
}

bool isHtmlNative(const NativeTransferFormat& native)
{
    return nativeNameIs(native, WindowsHtmlName) ||
           nativeNameIs(native, LinuxHtmlName) ||
           nativeNameIs(native, MacHtmlName);
}

bool isRtfNative(const NativeTransferFormat& native)
{
    return nativeNameIs(native, WindowsRtfName) ||
           nativeNameIs(native, LinuxRtfName) ||
           nativeNameIs(native, LinuxApplicationRtfName) ||
           nativeNameIs(native, MacRtfName);
}

bool isPngNative(const NativeTransferFormat& native)
{
    return nativeNameIs(native, WindowsPngName) ||
           nativeNameIs(native, LinuxPngName) ||
           nativeNameIs(native, MacPngName);
}

bool isDibNative(const NativeTransferFormat& native)
{
    return nativeNameIs(native, WindowsDibName) ||
           nativeNameIs(native, WindowsDibV5Name);
}

protocol::ByteBuffer bytesFromString(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

std::string stringFromBytes(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

std::string normalizeCrlfToLf(std::string value)
{
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\r' &&
            index + 1 < value.size() &&
            value[index + 1] == '\n') {
            result.push_back('\n');
            ++index;
            continue;
        }
        result.push_back(value[index]);
    }
    return result;
}

std::string normalizeLfToCrlf(const std::string& value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '\r') {
            result.push_back('\r');
            if (index + 1 < value.size() && value[index + 1] == '\n') {
                result.push_back('\n');
                ++index;
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

void appendUtf8CodePoint(std::string& output, std::uint32_t codePoint)
{
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(
            static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

std::uint32_t nextUtf8CodePoint(const std::string& value,
                                std::size_t* index)
{
    if (index == nullptr || *index >= value.size())
        return 0;

    const auto byteAt = [&value](std::size_t pos) -> std::uint8_t {
        return static_cast<std::uint8_t>(value[pos]);
    };
    const std::uint8_t first = byteAt(*index);
    if (first < 0x80U) {
        ++*index;
        return first;
    }

    std::uint32_t codePoint = 0;
    std::size_t needed = 0;
    std::uint32_t minValue = 0;
    if ((first & 0xe0U) == 0xc0U) {
        codePoint = first & 0x1fU;
        needed = 1;
        minValue = 0x80U;
    } else if ((first & 0xf0U) == 0xe0U) {
        codePoint = first & 0x0fU;
        needed = 2;
        minValue = 0x800U;
    } else if ((first & 0xf8U) == 0xf0U) {
        codePoint = first & 0x07U;
        needed = 3;
        minValue = 0x10000U;
    } else {
        ++*index;
        return 0xfffdU;
    }

    if (*index + needed >= value.size()) {
        ++*index;
        return 0xfffdU;
    }
    for (std::size_t offset = 1; offset <= needed; ++offset) {
        const std::uint8_t next = byteAt(*index + offset);
        if ((next & 0xc0U) != 0x80U) {
            ++*index;
            return 0xfffdU;
        }
        codePoint = (codePoint << 6U) | (next & 0x3fU);
    }
    *index += needed + 1;
    if (codePoint < minValue ||
        codePoint > 0x10ffffU ||
        (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
        return 0xfffdU;
    }
    return codePoint;
}

void appendUtf16LeUnit(protocol::ByteBuffer& output, std::uint16_t unit)
{
    output.push_back(static_cast<std::uint8_t>(unit & 0xffU));
    output.push_back(static_cast<std::uint8_t>((unit >> 8U) & 0xffU));
}

protocol::ByteBuffer utf16LeFromUtf8Text(const protocol::ByteBuffer& bytes)
{
    const std::string normalized = normalizeLfToCrlf(stringFromBytes(bytes));
    protocol::ByteBuffer result;
    result.reserve((normalized.size() + 1) * 2);
    std::size_t index = 0;
    while (index < normalized.size()) {
        const std::uint32_t codePoint =
            nextUtf8CodePoint(normalized, &index);
        if (codePoint <= 0xffffU) {
            appendUtf16LeUnit(result, static_cast<std::uint16_t>(codePoint));
        } else {
            const std::uint32_t scalar = codePoint - 0x10000U;
            appendUtf16LeUnit(
                result,
                static_cast<std::uint16_t>(0xd800U + (scalar >> 10U)));
            appendUtf16LeUnit(
                result,
                static_cast<std::uint16_t>(0xdc00U + (scalar & 0x3ffU)));
        }
    }
    appendUtf16LeUnit(result, 0);
    return result;
}

protocol::ByteBuffer utf8FromUtf16LeText(const protocol::ByteBuffer& bytes)
{
    std::string value;
    value.reserve(bytes.size() / 2);
    for (std::size_t index = 0; index + 1 < bytes.size(); index += 2) {
        const std::uint16_t unit =
            static_cast<std::uint16_t>(bytes[index]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[index + 1]) << 8U);
        if (unit == 0)
            break;

        std::uint32_t codePoint = unit;
        if (unit >= 0xd800U && unit <= 0xdbffU && index + 3 < bytes.size()) {
            const std::uint16_t low =
                static_cast<std::uint16_t>(bytes[index + 2]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(bytes[index + 3]) << 8U);
            if (low >= 0xdc00U && low <= 0xdfffU) {
                codePoint = 0x10000U +
                            (((static_cast<std::uint32_t>(unit) - 0xd800U)
                              << 10U) |
                             (static_cast<std::uint32_t>(low) - 0xdc00U));
                index += 2;
            } else {
                codePoint = 0xfffdU;
            }
        } else if (unit >= 0xdc00U && unit <= 0xdfffU) {
            codePoint = 0xfffdU;
        }
        appendUtf8CodePoint(value, codePoint);
    }
    return bytesFromString(normalizeCrlfToLf(std::move(value)));
}

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
        result = result * 10U +
                 static_cast<std::uint64_t>(value[index] - '0');
        ++index;
    }
    return found ? result : fallback;
}

protocol::ByteBuffer windowsHtmlFromCanonicalHtml(
    const protocol::ByteBuffer& canonicalHtml)
{
    const std::string fragment = stringFromBytes(canonicalHtml);
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

    return bytesFromString(header + html);
}

protocol::ByteBuffer canonicalHtmlFromWindowsHtml(
    const protocol::ByteBuffer& windowsHtml)
{
    const std::string value = stringFromBytes(windowsHtml);
    if (value.empty())
        return {};

    const bool hasStartField =
        value.find("StartFragment:") != std::string::npos;
    const bool hasEndField =
        value.find("EndFragment:") != std::string::npos;
    if (hasStartField && hasEndField) {
        const std::uint64_t start =
            decimalField(value, "StartFragment:", 0);
        const std::uint64_t end =
            decimalField(value,
                         "EndFragment:",
                         static_cast<std::uint64_t>(value.size()));
        if (start < end && end <= value.size()) {
            return bytesFromString(
                value.substr(static_cast<std::size_t>(start),
                             static_cast<std::size_t>(end - start)));
        }
    }

    const std::string startMarker = "<!--StartFragment-->";
    const std::string endMarker = "<!--EndFragment-->";
    const std::size_t startMarkerPos = value.find(startMarker);
    const std::size_t endMarkerPos = value.find(endMarker);
    if (startMarkerPos != std::string::npos &&
        endMarkerPos != std::string::npos &&
        startMarkerPos + startMarker.size() <= endMarkerPos) {
        const std::size_t fragmentStart = startMarkerPos + startMarker.size();
        return bytesFromString(
            value.substr(fragmentStart, endMarkerPos - fragmentStart));
    }

    return windowsHtml;
}

TransferTranscodeResult okResult(TransferEncodingMode encoding,
                                 protocol::ByteBuffer bytes)
{
    TransferTranscodeResult result;
    result.status = protocol::ResponseStatus::Ok;
    result.encoding = encoding;
    result.bytes = std::move(bytes);
    return result;
}

TransferTranscodeResult unsupportedResult(const char* message)
{
    TransferTranscodeResult result;
    result.status = protocol::ResponseStatus::Unsupported;
    result.message = message == nullptr ? "clipboard transcode is unsupported"
                                        : message;
    return result;
}

bool isCanonicalLike(TransferEncodingMode encoding)
{
    return encoding == TransferEncodingMode::CanonicalBytes ||
           encoding == TransferEncodingMode::Transcoded;
}

bool canSourceNativeToCanonical(const TransferTranscodeRequest& request)
{
    if (request.canonicalFormat == TextPlainUtf8Format)
        return isUtf8TextNative(request.sourceNative) ||
               isUtf16TextNative(request.sourceNative);
    if (request.canonicalFormat == TextHtmlFormat)
        return isHtmlNative(request.sourceNative);
    if (request.canonicalFormat == TextRtfFormat)
        return isRtfNative(request.sourceNative);
    if (request.canonicalFormat == ImagePngFormat)
        return isPngNative(request.sourceNative);
    if (request.canonicalFormat == ImageDibFormat)
        return isDibNative(request.sourceNative);
    return false;
}

bool canCanonicalToTargetNative(const TransferTranscodeRequest& request)
{
    if (request.canonicalFormat == TextPlainUtf8Format)
        return isUtf8TextNative(request.targetNative) ||
               isUtf16TextNative(request.targetNative);
    if (request.canonicalFormat == TextHtmlFormat)
        return isHtmlNative(request.targetNative);
    if (request.canonicalFormat == TextRtfFormat)
        return isRtfNative(request.targetNative);
    if (request.canonicalFormat == ImagePngFormat)
        return isPngNative(request.targetNative);
    if (request.canonicalFormat == ImageDibFormat)
        return isDibNative(request.targetNative);
    return false;
}

TransferTranscodeResult sourceNativeToCanonical(
    const TransferTranscodeRequest& request)
{
    if (!canSourceNativeToCanonical(request))
        return unsupportedResult("source native clipboard format is unsupported");

    if (request.canonicalFormat == TextPlainUtf8Format) {
        if (isUtf16TextNative(request.sourceNative)) {
            return okResult(TransferEncodingMode::CanonicalBytes,
                            utf8FromUtf16LeText(request.bytes));
        }
        return okResult(TransferEncodingMode::CanonicalBytes, request.bytes);
    }

    if (request.canonicalFormat == TextHtmlFormat) {
        if (nativeNameIs(request.sourceNative, WindowsHtmlName)) {
            return okResult(TransferEncodingMode::CanonicalBytes,
                            canonicalHtmlFromWindowsHtml(request.bytes));
        }
        return okResult(TransferEncodingMode::CanonicalBytes, request.bytes);
    }

    return okResult(TransferEncodingMode::CanonicalBytes, request.bytes);
}

TransferTranscodeResult canonicalToTargetNative(
    const TransferTranscodeRequest& request,
    protocol::ByteBuffer canonicalBytes)
{
    if (!canCanonicalToTargetNative(request))
        return unsupportedResult("target native clipboard format is unsupported");

    if (request.canonicalFormat == TextPlainUtf8Format) {
        if (isUtf16TextNative(request.targetNative)) {
            return okResult(TransferEncodingMode::NativePassthrough,
                            utf16LeFromUtf8Text(canonicalBytes));
        }
        return okResult(TransferEncodingMode::NativePassthrough,
                        std::move(canonicalBytes));
    }

    if (request.canonicalFormat == TextHtmlFormat) {
        if (nativeNameIs(request.targetNative, WindowsHtmlName)) {
            return okResult(TransferEncodingMode::NativePassthrough,
                            windowsHtmlFromCanonicalHtml(canonicalBytes));
        }
        return okResult(TransferEncodingMode::NativePassthrough,
                        std::move(canonicalBytes));
    }

    return okResult(TransferEncodingMode::NativePassthrough,
                    std::move(canonicalBytes));
}

} // namespace

bool DefaultTransferTranscoder::canTranscode(
    const TransferTranscodeRequest& request) const
{
    if (request.canonicalFormat.empty())
        return false;

    if (request.sourceEncoding == TransferEncodingMode::NativePassthrough &&
        request.targetEncoding == TransferEncodingMode::NativePassthrough) {
        if (sameNativeFormat(request.sourceNative, request.targetNative))
            return true;
        return canSourceNativeToCanonical(request) &&
               canCanonicalToTargetNative(request);
    }

    if (request.sourceEncoding == request.targetEncoding)
        return true;

    if (request.targetEncoding == TransferEncodingMode::CanonicalBytes) {
        return isCanonicalLike(request.sourceEncoding) ||
               (request.sourceEncoding ==
                    TransferEncodingMode::NativePassthrough &&
                canSourceNativeToCanonical(request));
    }

    if (request.targetEncoding == TransferEncodingMode::NativePassthrough) {
        return (isCanonicalLike(request.sourceEncoding) &&
                canCanonicalToTargetNative(request)) ||
               (request.sourceEncoding ==
                    TransferEncodingMode::NativePassthrough &&
                canSourceNativeToCanonical(request) &&
                canCanonicalToTargetNative(request));
    }

    return false;
}

TransferTranscodeResult DefaultTransferTranscoder::transcode(
    const TransferTranscodeRequest& request) const
{
    if (request.canonicalFormat.empty())
        return unsupportedResult("clipboard canonical format is empty");

    if (request.sourceEncoding == TransferEncodingMode::NativePassthrough &&
        request.targetEncoding == TransferEncodingMode::NativePassthrough &&
        sameNativeFormat(request.sourceNative, request.targetNative)) {
        return okResult(TransferEncodingMode::NativePassthrough,
                        request.bytes);
    }

    if (request.sourceEncoding == request.targetEncoding &&
        request.sourceEncoding != TransferEncodingMode::NativePassthrough) {
        return okResult(request.targetEncoding, request.bytes);
    }

    if (request.targetEncoding == TransferEncodingMode::CanonicalBytes) {
        if (isCanonicalLike(request.sourceEncoding)) {
            return okResult(TransferEncodingMode::CanonicalBytes,
                            request.bytes);
        }
        if (request.sourceEncoding == TransferEncodingMode::NativePassthrough)
            return sourceNativeToCanonical(request);
    }

    if (request.targetEncoding == TransferEncodingMode::NativePassthrough) {
        if (isCanonicalLike(request.sourceEncoding)) {
            return canonicalToTargetNative(request, request.bytes);
        }
        if (request.sourceEncoding == TransferEncodingMode::NativePassthrough) {
            TransferTranscodeResult canonical =
                sourceNativeToCanonical(request);
            if (!canonical.ok())
                return canonical;
            return canonicalToTargetNative(request,
                                           std::move(canonical.bytes));
        }
    }

    return unsupportedResult("clipboard transcode path is unsupported");
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
