#ifndef FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_DESCRIPTORS_H
#define FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_DESCRIPTORS_H

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

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

constexpr const char* WindowsUnicodeTextName = "CF_UNICODETEXT";
constexpr const char* WindowsHtmlName = "HTML Format";
constexpr const char* WindowsRtfName = "Rich Text Format";
constexpr const char* WindowsPngName = "PNG";
constexpr const char* WindowsDibName = "CF_DIB";
constexpr const char* WindowsDibV5Name = "CF_DIBV5";
constexpr const char* WindowsFileDropName = "CF_HDROP";
constexpr const char* WindowsFileGroupDescriptorName = "FileGroupDescriptorW";

inline bool windowsTextDescriptorMatches(
    const modules::clipboard::TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat ==
               modules::clipboard::TextPlainUtf8Format ||
           descriptor.nativeFormatName == WindowsUnicodeTextName ||
           descriptor.localFormatToken == 13;
}

inline bool windowsHtmlDescriptorMatches(
    const modules::clipboard::TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == modules::clipboard::TextHtmlFormat ||
           descriptor.nativeFormatName == WindowsHtmlName
#if defined(_WIN32)
           || descriptor.localFormatToken == windowsHtmlFormatToken()
#endif
        ;
}

inline bool windowsRtfDescriptorMatches(
    const modules::clipboard::TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == modules::clipboard::TextRtfFormat ||
           descriptor.nativeFormatName == WindowsRtfName
#if defined(_WIN32)
           || descriptor.localFormatToken == windowsRtfFormatToken()
#endif
        ;
}

inline bool windowsPngDescriptorMatches(
    const modules::clipboard::TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == modules::clipboard::ImagePngFormat ||
           descriptor.nativeFormatName == WindowsPngName
#if defined(_WIN32)
           || descriptor.localFormatToken == windowsPngFormatToken()
#endif
        ;
}

inline bool windowsDibDescriptorMatches(
    const modules::clipboard::TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == modules::clipboard::ImageDibFormat ||
           descriptor.nativeFormatName == WindowsDibName ||
           descriptor.nativeFormatName == WindowsDibV5Name
#if defined(_WIN32)
           || descriptor.localFormatToken == CF_DIB ||
           descriptor.localFormatToken == CF_DIBV5
#endif
        ;
}

inline bool windowsFileListDescriptorMatches(
    const modules::clipboard::TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat ==
               modules::clipboard::FdclFileListFormat ||
           descriptor.nativeFormatName == WindowsFileDropName ||
           descriptor.nativeFormatName == WindowsFileGroupDescriptorName
#if defined(_WIN32)
           || descriptor.localFormatToken == CF_HDROP ||
           descriptor.localFormatToken == windowsFileGroupDescriptorFormatToken()
#endif
        ;
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_DESCRIPTORS_H
