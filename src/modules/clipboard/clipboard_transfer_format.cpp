#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace clipboard {

namespace {

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

TransferFormatClass classForCanonical(const std::string& canonical)
{
    const std::string value = lowered(canonical);
    if (value == TextPlainUtf8Format)
        return TransferFormatClass::PlainText;
    if (value == TextHtmlFormat)
        return TransferFormatClass::Html;
    if (value == TextRtfFormat)
        return TransferFormatClass::Rtf;
    if (value == ImagePngFormat || value == ImageDibFormat)
        return TransferFormatClass::Image;
    if (value == FdclFileListFormat)
        return TransferFormatClass::FileList;
    if (value == FdclOwnerMarkerFormat)
        return TransferFormatClass::OwnerMarker;
    if (!value.empty())
        return TransferFormatClass::Custom;
    return TransferFormatClass::Unknown;
}

std::string canonicalForNative(TransferPlatformFamily platform,
                               const std::string& nativeName)
{
    const std::string name = lowered(nativeName);
    if (name.empty())
        return {};

    switch (platform) {
    case TransferPlatformFamily::Windows:
        if (name == "cf_unicodetext")
            return TextPlainUtf8Format;
        if (name == "html format")
            return TextHtmlFormat;
        if (name == "rich text format")
            return TextRtfFormat;
        if (name == "png")
            return ImagePngFormat;
        if (name == "cf_dib" || name == "cf_dibv5")
            return ImageDibFormat;
        if (name == "filegroupdescriptorw" ||
            name == "filecontents" ||
            name == "cf_hdrop")
            return FdclFileListFormat;
        break;
    case TransferPlatformFamily::Linux:
    case TransferPlatformFamily::Qt:
    case TransferPlatformFamily::Wayland:
    case TransferPlatformFamily::Gtk:
        if (name == "utf8_string" ||
            name == "text/plain" ||
            name == "text/plain;charset=utf-8" ||
            name == "text/plain;charset=utf8")
            return TextPlainUtf8Format;
        if (name == "text/html")
            return TextHtmlFormat;
        if (name == "text/rtf" || name == "application/rtf")
            return TextRtfFormat;
        if (name == "image/png")
            return ImagePngFormat;
        if (name == "text/uri-list" ||
            name == "x-special/gnome-copied-files" ||
            name == "x-special/mate-copied-files" ||
            name == "text/x-moz-url")
            return FdclFileListFormat;
        break;
    case TransferPlatformFamily::MacOS:
    case TransferPlatformFamily::Ios:
        if (name == "public.utf8-plain-text" ||
            name == "public.utf16-plain-text")
            return TextPlainUtf8Format;
        if (name == "public.html")
            return TextHtmlFormat;
        if (name == "public.rtf")
            return TextRtfFormat;
        if (name == "public.png")
            return ImagePngFormat;
        if (name == "public.file-url")
            return FdclFileListFormat;
        break;
    case TransferPlatformFamily::Android:
        if (name == "text/plain")
            return TextPlainUtf8Format;
        if (name == "text/html")
            return TextHtmlFormat;
        if (name == "image/png")
            return ImagePngFormat;
        if (name == "text/uri-list")
            return FdclFileListFormat;
        break;
    case TransferPlatformFamily::Unknown:
        break;
    }

    return {};
}

NativeTransferFormatCandidate candidate(TransferPlatformFamily platform,
                                        std::string nativeName,
                                        TransferEncodingMode encoding,
                                        int priority)
{
    NativeTransferFormatCandidate result;
    result.native.platform = platform;
    result.native.nativeFormatName = std::move(nativeName);
    result.encoding = encoding;
    result.priority = priority;
    return result;
}

bool sameNativeFormat(const NativeTransferFormat& left,
                      const NativeTransferFormat& right)
{
    return left.platform == right.platform &&
           lowered(left.nativeFormatName) == lowered(right.nativeFormatName);
}

} // namespace

std::vector<TransferCanonicalFormatSpec> defaultCanonicalTransferFormats()
{
    return {
        {TextPlainUtf8Format,
         TransferFormatClass::PlainText,
         TransferCanonicalFormatScope::CrossOs,
         TransferEncodingMode::CanonicalBytes,
         true},
        {TextHtmlFormat,
         TransferFormatClass::Html,
         TransferCanonicalFormatScope::CrossOs,
         TransferEncodingMode::CanonicalBytes,
         true},
        {TextRtfFormat,
         TransferFormatClass::Rtf,
         TransferCanonicalFormatScope::CrossOs,
         TransferEncodingMode::CanonicalBytes,
         true},
        {ImagePngFormat,
         TransferFormatClass::Image,
         TransferCanonicalFormatScope::CrossOs,
         TransferEncodingMode::CanonicalBytes,
         true},
        {FdclFileListFormat,
         TransferFormatClass::FileList,
         TransferCanonicalFormatScope::CrossOs,
         TransferEncodingMode::CanonicalBytes,
         true},
        {ImageDibFormat,
         TransferFormatClass::Image,
         TransferCanonicalFormatScope::SamePlatform,
         TransferEncodingMode::NativePassthrough,
         true},
        {FdclOwnerMarkerFormat,
         TransferFormatClass::OwnerMarker,
         TransferCanonicalFormatScope::Internal,
         TransferEncodingMode::CanonicalBytes,
         false},
    };
}

std::optional<TransferCanonicalFormatSpec> canonicalTransferFormatSpec(
    const std::string& canonicalFormat)
{
    const std::string canonical = lowered(canonicalFormat);
    for (TransferCanonicalFormatSpec spec :
         defaultCanonicalTransferFormats()) {
        if (lowered(spec.canonicalFormat) == canonical)
            return spec;
    }
    return std::nullopt;
}

bool isKnownCanonicalTransferFormat(const std::string& canonicalFormat)
{
    return canonicalTransferFormatSpec(canonicalFormat).has_value();
}

bool isCrossOsCanonicalTransferFormat(const std::string& canonicalFormat)
{
    const std::optional<TransferCanonicalFormatSpec> spec =
        canonicalTransferFormatSpec(canonicalFormat);
    return spec.has_value() &&
           spec->scope == TransferCanonicalFormatScope::CrossOs;
}

TransferFormatMappingResult DefaultTransferFormatMapper::mapNativeToCanonical(
    const TransferFormatMappingRequest& request) const
{
    TransferFormatMappingResult result;
    const std::string canonical = canonicalForNative(
        request.native.platform,
        request.native.nativeFormatName);
    if (canonical.empty()) {
        result.reason = "native clipboard format is not mapped";
        return result;
    }

    result.mapped = true;
    result.formatClass = classForCanonical(canonical);
    result.descriptor.canonicalFormat = canonical;
    result.descriptor.nativeFormatName = request.native.nativeFormatName;
    result.descriptor.localFormatToken = request.native.localFormatToken;
    result.descriptor.formatId = request.formatId;
    result.descriptor.itemIndex = request.itemIndex;
    result.descriptor.estimatedBytes = request.estimatedBytes;
    result.descriptor.canInline = request.canInline;
    result.descriptor.canStream = request.canStream;
    result.descriptor.preferredEncoding =
        TransferEncodingMode::NativePassthrough;
    return result;
}

std::vector<NativeTransferFormatCandidate>
DefaultTransferFormatMapper::nativeCandidates(
    const std::string& canonicalFormat,
    TransferPlatformFamily targetPlatform) const
{
    std::vector<NativeTransferFormatCandidate> result;
    const std::string canonical = lowered(canonicalFormat);

    switch (targetPlatform) {
    case TransferPlatformFamily::Windows:
        if (canonical == TextPlainUtf8Format) {
            result.push_back(candidate(targetPlatform,
                                       "CF_UNICODETEXT",
                                       TransferEncodingMode::Transcoded,
                                       100));
        } else if (canonical == TextHtmlFormat) {
            result.push_back(candidate(targetPlatform,
                                       "HTML Format",
                                       TransferEncodingMode::Transcoded,
                                       100));
        } else if (canonical == TextRtfFormat) {
            result.push_back(candidate(targetPlatform,
                                       "Rich Text Format",
                                       TransferEncodingMode::NativePassthrough,
                                       100));
        } else if (canonical == ImagePngFormat) {
            result.push_back(candidate(targetPlatform,
                                       "PNG",
                                       TransferEncodingMode::NativePassthrough,
                                       100));
            result.push_back(candidate(targetPlatform,
                                       "CF_DIBV5",
                                       TransferEncodingMode::Transcoded,
                                       90));
            result.push_back(candidate(targetPlatform,
                                       "CF_DIB",
                                       TransferEncodingMode::Transcoded,
                                       80));
        } else if (canonical == ImageDibFormat) {
            result.push_back(candidate(targetPlatform,
                                       "CF_DIBV5",
                                       TransferEncodingMode::NativePassthrough,
                                       100));
            result.push_back(candidate(targetPlatform,
                                       "CF_DIB",
                                       TransferEncodingMode::NativePassthrough,
                                       90));
        } else if (canonical == FdclFileListFormat) {
            result.push_back(candidate(targetPlatform,
                                       "FileGroupDescriptorW",
                                       TransferEncodingMode::Transcoded,
                                       100));
            result.push_back(candidate(targetPlatform,
                                       "FileContents",
                                       TransferEncodingMode::Transcoded,
                                       90));
        }
        break;
    case TransferPlatformFamily::Linux:
    case TransferPlatformFamily::Qt:
    case TransferPlatformFamily::Wayland:
    case TransferPlatformFamily::Gtk:
        if (canonical == TextPlainUtf8Format) {
            result.push_back(candidate(targetPlatform,
                                       "text/plain;charset=utf-8",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
            result.push_back(candidate(targetPlatform,
                                       "UTF8_STRING",
                                       TransferEncodingMode::CanonicalBytes,
                                       90));
        } else if (canonical == TextHtmlFormat) {
            result.push_back(candidate(targetPlatform,
                                       "text/html",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == TextRtfFormat) {
            result.push_back(candidate(targetPlatform,
                                       "text/rtf",
                                       TransferEncodingMode::NativePassthrough,
                                       100));
        } else if (canonical == ImagePngFormat) {
            result.push_back(candidate(targetPlatform,
                                       "image/png",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == FdclFileListFormat) {
            result.push_back(candidate(targetPlatform,
                                       "text/uri-list",
                                       TransferEncodingMode::Transcoded,
                                       100));
            result.push_back(candidate(targetPlatform,
                                       "x-special/gnome-copied-files",
                                       TransferEncodingMode::Transcoded,
                                       95));
            result.push_back(candidate(targetPlatform,
                                       "x-special/mate-copied-files",
                                       TransferEncodingMode::Transcoded,
                                       90));
            result.push_back(candidate(targetPlatform,
                                       "text/x-moz-url",
                                       TransferEncodingMode::Transcoded,
                                       80));
        }
        break;
    case TransferPlatformFamily::MacOS:
    case TransferPlatformFamily::Ios:
        if (canonical == TextPlainUtf8Format) {
            result.push_back(candidate(targetPlatform,
                                       "public.utf8-plain-text",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == TextHtmlFormat) {
            result.push_back(candidate(targetPlatform,
                                       "public.html",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == TextRtfFormat) {
            result.push_back(candidate(targetPlatform,
                                       "public.rtf",
                                       TransferEncodingMode::NativePassthrough,
                                       100));
        } else if (canonical == ImagePngFormat) {
            result.push_back(candidate(targetPlatform,
                                       "public.png",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == FdclFileListFormat) {
            result.push_back(candidate(targetPlatform,
                                       "public.file-url",
                                       TransferEncodingMode::Transcoded,
                                       100));
        }
        break;
    case TransferPlatformFamily::Android:
        if (canonical == TextPlainUtf8Format) {
            result.push_back(candidate(targetPlatform,
                                       "text/plain",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == TextHtmlFormat) {
            result.push_back(candidate(targetPlatform,
                                       "text/html",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == ImagePngFormat) {
            result.push_back(candidate(targetPlatform,
                                       "image/png",
                                       TransferEncodingMode::CanonicalBytes,
                                       100));
        } else if (canonical == FdclFileListFormat) {
            result.push_back(candidate(targetPlatform,
                                       "text/uri-list",
                                       TransferEncodingMode::Transcoded,
                                       100));
        }
        break;
    case TransferPlatformFamily::Unknown:
        break;
    }

    return result;
}

TransferFormatClass DefaultTransferFormatMapper::formatClass(
    const std::string& canonicalFormat) const
{
    return classForCanonical(canonicalFormat);
}

bool IdentityTransferTranscoder::canTranscode(
    const TransferTranscodeRequest& request) const
{
    if (request.canonicalFormat.empty())
        return false;

    if (request.sourceEncoding == TransferEncodingMode::NativePassthrough &&
        request.targetEncoding == TransferEncodingMode::NativePassthrough)
        return sameNativeFormat(request.sourceNative, request.targetNative);

    if (request.sourceEncoding == request.targetEncoding)
        return true;

    if (request.sourceEncoding == TransferEncodingMode::CanonicalBytes &&
        request.targetEncoding == TransferEncodingMode::CanonicalBytes)
        return true;

    return false;
}

TransferTranscodeResult IdentityTransferTranscoder::transcode(
    const TransferTranscodeRequest& request) const
{
    TransferTranscodeResult result;
    if (!canTranscode(request)) {
        result.status = protocol::ResponseStatus::Unsupported;
        result.message = "identity transcoder cannot convert requested format";
        return result;
    }

    result.status = protocol::ResponseStatus::Ok;
    result.encoding = request.targetEncoding;
    result.bytes = request.bytes;
    return result;
}

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk
