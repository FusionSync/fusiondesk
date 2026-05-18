#ifndef FUSIONDESK_MODULES_INPUT_INPUT_TYPES_H
#define FUSIONDESK_MODULES_INPUT_INPUT_TYPES_H

#include <cstdint>

namespace fusiondesk {
namespace modules {
namespace input {

enum class InputModuleKind : std::uint16_t
{
    Mouse = 1,
    Keyboard = 2
};

enum class MouseAction : std::uint16_t
{
    Move = 1,
    ButtonDown = 2,
    ButtonUp = 3,
    Wheel = 4
};

enum class MouseButton : std::uint16_t
{
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3,
    X1 = 4,
    X2 = 5
};

enum class KeyboardAction : std::uint16_t
{
    KeyDown = 1,
    KeyUp = 2,
    Text = 3
};

enum class InputCoordinateSpace : std::uint16_t
{
    Normalized = 1,
    Pixel = 2
};

struct InputModifierState
{
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool meta = false;
};

struct MouseInputEvent
{
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    MouseAction action = MouseAction::Move;
    MouseButton button = MouseButton::None;
    InputCoordinateSpace coordinateSpace = InputCoordinateSpace::Normalized;
    InputModifierState modifiers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t wheelDeltaX = 0;
    std::int32_t wheelDeltaY = 0;
};

struct KeyboardInputEvent
{
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    KeyboardAction action = KeyboardAction::KeyDown;
    InputModifierState modifiers;
    std::uint32_t nativeScanCode = 0;
    std::uint32_t virtualKey = 0;
    std::uint32_t unicodeCodepoint = 0;
};

} // namespace input
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_INPUT_INPUT_TYPES_H
