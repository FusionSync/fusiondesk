#ifndef FUSIONDESK_MODULES_INPUT_INPUT_PAYLOAD_CODEC_H
#define FUSIONDESK_MODULES_INPUT_INPUT_PAYLOAD_CODEC_H

#include <string>

#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/modules/input/input_types.h"

namespace fusiondesk {
namespace modules {
namespace input {

struct InputDecodeResult
{
    bool ok = false;
    std::string error;
    InputModuleKind kind = InputModuleKind::Mouse;
    MouseInputEvent mouse;
    KeyboardInputEvent keyboard;
};

protocol::ByteBuffer encodeMouseInputPayload(const MouseInputEvent& event);
protocol::ByteBuffer encodeKeyboardInputPayload(const KeyboardInputEvent& event);
InputDecodeResult decodeInputPayload(const protocol::ByteBuffer& payload);

} // namespace input
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_INPUT_INPUT_PAYLOAD_CODEC_H
