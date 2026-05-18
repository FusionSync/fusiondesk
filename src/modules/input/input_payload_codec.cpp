#include "fusiondesk/modules/input/input_payload_codec.h"

namespace fusiondesk {
namespace modules {
namespace input {

namespace {

constexpr std::uint32_t InputMagic = 0x4644494e; // "FDIN"
constexpr std::uint16_t InputVersion = 1;
constexpr std::uint32_t ModifierShift = 0x00000001;
constexpr std::uint32_t ModifierControl = 0x00000002;
constexpr std::uint32_t ModifierAlt = 0x00000004;
constexpr std::uint32_t ModifierMeta = 0x00000008;

void appendU16(protocol::ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendU32(protocol::ByteBuffer& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendI32(protocol::ByteBuffer& output, std::int32_t value)
{
    appendU32(output, static_cast<std::uint32_t>(value));
}

void appendU64(protocol::ByteBuffer& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

bool readU16(const protocol::ByteBuffer& input, std::size_t& offset, std::uint16_t& value)
{
    if (offset + 2 > input.size())
        return false;
    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[offset]) << 8) |
                                       static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

bool readU32(const protocol::ByteBuffer& input, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4 > input.size())
        return false;
    value = (static_cast<std::uint32_t>(input[offset]) << 24) |
            (static_cast<std::uint32_t>(input[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(input[offset + 2]) << 8) |
            static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool readI32(const protocol::ByteBuffer& input, std::size_t& offset, std::int32_t& value)
{
    std::uint32_t raw = 0;
    if (!readU32(input, offset, raw))
        return false;
    value = static_cast<std::int32_t>(raw);
    return true;
}

bool readU64(const protocol::ByteBuffer& input, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8 > input.size())
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + i]);
    offset += 8;
    return true;
}

std::uint32_t encodeModifiers(const InputModifierState& modifiers)
{
    std::uint32_t flags = 0;
    if (modifiers.shift)
        flags |= ModifierShift;
    if (modifiers.control)
        flags |= ModifierControl;
    if (modifiers.alt)
        flags |= ModifierAlt;
    if (modifiers.meta)
        flags |= ModifierMeta;
    return flags;
}

InputModifierState decodeModifiers(std::uint32_t flags)
{
    InputModifierState modifiers;
    modifiers.shift = (flags & ModifierShift) != 0;
    modifiers.control = (flags & ModifierControl) != 0;
    modifiers.alt = (flags & ModifierAlt) != 0;
    modifiers.meta = (flags & ModifierMeta) != 0;
    return modifiers;
}

protocol::ByteBuffer encodeHeader(InputModuleKind kind,
                                   std::uint64_t sequence,
                                   std::uint64_t timestampUsec,
                                   std::uint32_t modifiers)
{
    protocol::ByteBuffer output;
    output.reserve(64);
    appendU32(output, InputMagic);
    appendU16(output, InputVersion);
    appendU16(output, static_cast<std::uint16_t>(kind));
    appendU64(output, sequence);
    appendU64(output, timestampUsec);
    appendU32(output, modifiers);
    return output;
}

} // namespace

protocol::ByteBuffer encodeMouseInputPayload(const MouseInputEvent& event)
{
    protocol::ByteBuffer output = encodeHeader(InputModuleKind::Mouse,
                                               event.sequence,
                                               event.monotonicTimestampUsec,
                                               encodeModifiers(event.modifiers));
    appendU16(output, static_cast<std::uint16_t>(event.action));
    appendU16(output, static_cast<std::uint16_t>(event.button));
    appendU16(output, static_cast<std::uint16_t>(event.coordinateSpace));
    appendU16(output, 0);
    appendI32(output, event.x);
    appendI32(output, event.y);
    appendI32(output, event.wheelDeltaX);
    appendI32(output, event.wheelDeltaY);
    return output;
}

protocol::ByteBuffer encodeKeyboardInputPayload(const KeyboardInputEvent& event)
{
    protocol::ByteBuffer output = encodeHeader(InputModuleKind::Keyboard,
                                               event.sequence,
                                               event.monotonicTimestampUsec,
                                               encodeModifiers(event.modifiers));
    appendU16(output, static_cast<std::uint16_t>(event.action));
    appendU16(output, 0);
    appendU32(output, event.nativeScanCode);
    appendU32(output, event.virtualKey);
    appendU32(output, event.unicodeCodepoint);
    return output;
}

InputDecodeResult decodeInputPayload(const protocol::ByteBuffer& payload)
{
    InputDecodeResult result;
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t kind = 0;
    std::uint64_t sequence = 0;
    std::uint64_t timestampUsec = 0;
    std::uint32_t modifierFlags = 0;

    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, version) ||
        !readU16(payload, offset, kind) ||
        !readU64(payload, offset, sequence) ||
        !readU64(payload, offset, timestampUsec) ||
        !readU32(payload, offset, modifierFlags)) {
        result.error = "input payload header is truncated";
        return result;
    }

    if (magic != InputMagic) {
        result.error = "input payload magic is invalid";
        return result;
    }
    if (version != InputVersion) {
        result.error = "input payload version is unsupported";
        return result;
    }

    result.kind = static_cast<InputModuleKind>(kind);
    const InputModifierState modifiers = decodeModifiers(modifierFlags);
    if (result.kind == InputModuleKind::Mouse) {
        std::uint16_t action = 0;
        std::uint16_t button = 0;
        std::uint16_t coordinateSpace = 0;
        std::uint16_t reserved = 0;
        result.mouse.sequence = sequence;
        result.mouse.monotonicTimestampUsec = timestampUsec;
        result.mouse.modifiers = modifiers;
        if (!readU16(payload, offset, action) ||
            !readU16(payload, offset, button) ||
            !readU16(payload, offset, coordinateSpace) ||
            !readU16(payload, offset, reserved) ||
            !readI32(payload, offset, result.mouse.x) ||
            !readI32(payload, offset, result.mouse.y) ||
            !readI32(payload, offset, result.mouse.wheelDeltaX) ||
            !readI32(payload, offset, result.mouse.wheelDeltaY)) {
            result.error = "mouse input payload body is truncated";
            return result;
        }
        (void)reserved;
        result.mouse.action = static_cast<MouseAction>(action);
        result.mouse.button = static_cast<MouseButton>(button);
        result.mouse.coordinateSpace = static_cast<InputCoordinateSpace>(coordinateSpace);
        result.ok = true;
        return result;
    }

    if (result.kind == InputModuleKind::Keyboard) {
        std::uint16_t action = 0;
        std::uint16_t reserved = 0;
        result.keyboard.sequence = sequence;
        result.keyboard.monotonicTimestampUsec = timestampUsec;
        result.keyboard.modifiers = modifiers;
        if (!readU16(payload, offset, action) ||
            !readU16(payload, offset, reserved) ||
            !readU32(payload, offset, result.keyboard.nativeScanCode) ||
            !readU32(payload, offset, result.keyboard.virtualKey) ||
            !readU32(payload, offset, result.keyboard.unicodeCodepoint)) {
            result.error = "keyboard input payload body is truncated";
            return result;
        }
        (void)reserved;
        result.keyboard.action = static_cast<KeyboardAction>(action);
        result.ok = true;
        return result;
    }

    result.error = "input payload kind is unsupported";
    return result;
}

} // namespace input
} // namespace modules
} // namespace fusiondesk
