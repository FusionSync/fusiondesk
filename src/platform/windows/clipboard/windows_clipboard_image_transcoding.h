#ifndef FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_IMAGE_TRANSCODING_H
#define FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_IMAGE_TRANSCODING_H

#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

protocol::ByteBuffer windowsPngFromDibBytes(
    const protocol::ByteBuffer& dibBytes);
protocol::ByteBuffer windowsDibFromPngBytes(
    const protocol::ByteBuffer& pngBytes);
protocol::ByteBuffer windowsDibV5FromPngBytes(
    const protocol::ByteBuffer& pngBytes);

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_CLIPBOARD_WINDOWS_CLIPBOARD_IMAGE_TRANSCODING_H
