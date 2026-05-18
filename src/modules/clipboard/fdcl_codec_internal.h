#ifndef FUSIONDESK_MODULES_CLIPBOARD_FDCL_CODEC_INTERNAL_H
#define FUSIONDESK_MODULES_CLIPBOARD_FDCL_CODEC_INTERNAL_H

#include "fusiondesk/modules/clipboard/fdcl_codec.h"

namespace fusiondesk {
namespace modules {
namespace clipboard {

FdclDecodeResult decodeFdclObjectLockBody(
    const protocol::ByteBuffer& body,
    FdclOperation operation);

} // namespace clipboard
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_CLIPBOARD_FDCL_CODEC_INTERNAL_H
