#include "fusiondesk/runtime/display/display_codec_peer_profile.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

constexpr std::uint32_t DisplayCodecPeerProfileMagic = 0x46444350; // "FDCP"
constexpr std::uint16_t DisplayCodecPeerProfileVersion = 1;

void appendU8(protocol::ByteBuffer& output, std::uint8_t value)
{
    output.push_back(value);
}

void appendU16(protocol::ByteBuffer& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU32(protocol::ByteBuffer& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendI32(protocol::ByteBuffer& output, std::int32_t value)
{
    appendU32(output, static_cast<std::uint32_t>(value));
}

bool appendString(protocol::ByteBuffer& output, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    appendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool appendCount(protocol::ByteBuffer& output, std::size_t value)
{
    if (value > std::numeric_limits<std::uint16_t>::max())
        return false;
    appendU16(output, static_cast<std::uint16_t>(value));
    return true;
}

bool readU8(const protocol::ByteBuffer& input, std::size_t& offset, std::uint8_t& value)
{
    if (offset + 1 > input.size())
        return false;
    value = input[offset++];
    return true;
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

bool readString(const protocol::ByteBuffer& input, std::size_t& offset, std::string& value)
{
    std::uint32_t size = 0;
    if (!readU32(input, offset, size))
        return false;
    if (offset + size > input.size())
        return false;
    value.assign(input.begin() + static_cast<std::ptrdiff_t>(offset),
                 input.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return true;
}

bool knownPixelFormat(modules::display::DisplayPixelFormat value)
{
    switch (value) {
    case modules::display::DisplayPixelFormat::Unknown:
    case modules::display::DisplayPixelFormat::Bgra32:
    case modules::display::DisplayPixelFormat::Rgba32:
    case modules::display::DisplayPixelFormat::Rgb24:
    case modules::display::DisplayPixelFormat::Gray8:
        return true;
    }
    return false;
}

bool knownPlatform(DisplayPlatformFamily value)
{
    switch (value) {
    case DisplayPlatformFamily::Unknown:
    case DisplayPlatformFamily::WindowsDesktop:
    case DisplayPlatformFamily::LinuxX11:
    case DisplayPlatformFamily::LinuxWayland:
    case DisplayPlatformFamily::LinuxEmbedded:
    case DisplayPlatformFamily::MacOS:
    case DisplayPlatformFamily::AndroidClient:
    case DisplayPlatformFamily::AndroidAgent:
    case DisplayPlatformFamily::HarmonyOS:
    case DisplayPlatformFamily::OpenHarmony:
    case DisplayPlatformFamily::RockchipLinux:
    case DisplayPlatformFamily::RockchipAndroid:
        return true;
    }
    return false;
}

bool knownMemoryType(DisplayCodecMemoryType value)
{
    switch (value) {
    case DisplayCodecMemoryType::Unknown:
    case DisplayCodecMemoryType::CpuBuffer:
    case DisplayCodecMemoryType::D3DTexture:
    case DisplayCodecMemoryType::DmaBuf:
    case DisplayCodecMemoryType::CVPixelBuffer:
    case DisplayCodecMemoryType::AndroidHardwareBuffer:
        return true;
    }
    return false;
}

bool knownArchitecture(DisplayTargetArchitecture value)
{
    switch (value) {
    case DisplayTargetArchitecture::Unknown:
    case DisplayTargetArchitecture::X86:
    case DisplayTargetArchitecture::X86_64:
    case DisplayTargetArchitecture::Arm32:
    case DisplayTargetArchitecture::Arm64:
    case DisplayTargetArchitecture::LoongArch64:
    case DisplayTargetArchitecture::Mips64El:
        return true;
    }
    return false;
}

bool knownSocProfile(DisplayTargetSocProfile value)
{
    switch (value) {
    case DisplayTargetSocProfile::Unknown:
    case DisplayTargetSocProfile::Generic:
    case DisplayTargetSocProfile::Rockchip3568:
    case DisplayTargetSocProfile::Rockchip3588:
        return true;
    }
    return false;
}

bool knownCodecId(DisplayCodecId value)
{
    switch (value) {
    case DisplayCodecId::Unknown:
    case DisplayCodecId::RawBgra:
    case DisplayCodecId::H264:
    case DisplayCodecId::H265:
    case DisplayCodecId::Av1:
        return true;
    }
    return false;
}

bool knownCodecDirection(DisplayCodecDirection value)
{
    switch (value) {
    case DisplayCodecDirection::Unknown:
    case DisplayCodecDirection::Encode:
    case DisplayCodecDirection::Decode:
        return true;
    }
    return false;
}

bool knownCodecBackend(DisplayCodecBackendKind value)
{
    switch (value) {
    case DisplayCodecBackendKind::Unknown:
    case DisplayCodecBackendKind::RawFrame:
    case DisplayCodecBackendKind::Software:
    case DisplayCodecBackendKind::FFmpegSoftware:
    case DisplayCodecBackendKind::WindowsMediaFoundation:
    case DisplayCodecBackendKind::LinuxVaapi:
    case DisplayCodecBackendKind::LinuxV4L2M2M:
    case DisplayCodecBackendKind::MacOSVideoToolbox:
    case DisplayCodecBackendKind::AndroidMediaCodec:
    case DisplayCodecBackendKind::HarmonySystemCodec:
    case DisplayCodecBackendKind::RockchipMpp:
        return true;
    }
    return false;
}

template <typename T>
bool appendEnumVector(protocol::ByteBuffer& output, const std::vector<T>& values)
{
    if (!appendCount(output, values.size()))
        return false;
    for (T value : values)
        appendU16(output, static_cast<std::uint16_t>(value));
    return true;
}

template <typename T, typename Validator>
bool readEnumVector(const protocol::ByteBuffer& input,
                    std::size_t& offset,
                    std::vector<T>& values,
                    Validator validator)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    values.clear();
    for (std::uint16_t index = 0; index < count; ++index) {
        std::uint16_t raw = 0;
        if (!readU16(input, offset, raw))
            return false;
        const T value = static_cast<T>(raw);
        if (!validator(value))
            return false;
        values.push_back(value);
    }
    return true;
}

bool appendCapability(protocol::ByteBuffer& output,
                      const DisplayCodecCapability& capability)
{
    appendU16(output, static_cast<std::uint16_t>(capability.platform));
    appendU16(output, static_cast<std::uint16_t>(capability.backend));
    appendU16(output, static_cast<std::uint16_t>(capability.codec));
    if (!appendString(output, capability.adapterId) ||
        !appendEnumVector(output, capability.pixelFormats) ||
        !appendEnumVector(output, capability.inputMemoryTypes) ||
        !appendEnumVector(output, capability.outputMemoryTypes) ||
        !appendEnumVector(output, capability.architectures) ||
        !appendEnumVector(output, capability.socProfiles) ||
        !appendString(output, capability.unavailableReason)) {
        return false;
    }
    appendU8(output, capability.supportsEncode ? 1U : 0U);
    appendU8(output, capability.supportsDecode ? 1U : 0U);
    appendU8(output, capability.available ? 1U : 0U);
    appendU8(output, capability.fallback ? 1U : 0U);
    appendU8(output, capability.hardwareAccelerated ? 1U : 0U);
    appendU8(output, capability.zeroCopy ? 1U : 0U);
    appendU8(output, capability.lowLatency ? 1U : 0U);
    appendU8(output, capability.requiresHardwareDevice ? 1U : 0U);
    appendU32(output, capability.maxWidth);
    appendU32(output, capability.maxHeight);
    appendI32(output, static_cast<std::int32_t>(capability.priority));
    return true;
}

bool readCapability(const protocol::ByteBuffer& input,
                    std::size_t& offset,
                    DisplayCodecCapability& capability)
{
    std::uint16_t platform = 0;
    std::uint16_t backend = 0;
    std::uint16_t codec = 0;
    std::uint8_t supportsEncode = 0;
    std::uint8_t supportsDecode = 0;
    std::uint8_t available = 0;
    std::uint8_t fallback = 0;
    std::uint8_t hardwareAccelerated = 0;
    std::uint8_t zeroCopy = 0;
    std::uint8_t lowLatency = 0;
    std::uint8_t requiresHardwareDevice = 0;
    std::int32_t priority = 0;
    if (!readU16(input, offset, platform) ||
        !readU16(input, offset, backend) ||
        !readU16(input, offset, codec) ||
        !readString(input, offset, capability.adapterId) ||
        !readEnumVector(input, offset, capability.pixelFormats, knownPixelFormat) ||
        !readEnumVector(input, offset, capability.inputMemoryTypes, knownMemoryType) ||
        !readEnumVector(input, offset, capability.outputMemoryTypes, knownMemoryType) ||
        !readEnumVector(input, offset, capability.architectures, knownArchitecture) ||
        !readEnumVector(input, offset, capability.socProfiles, knownSocProfile) ||
        !readString(input, offset, capability.unavailableReason) ||
        !readU8(input, offset, supportsEncode) ||
        !readU8(input, offset, supportsDecode) ||
        !readU8(input, offset, available) ||
        !readU8(input, offset, fallback) ||
        !readU8(input, offset, hardwareAccelerated) ||
        !readU8(input, offset, zeroCopy) ||
        !readU8(input, offset, lowLatency) ||
        !readU8(input, offset, requiresHardwareDevice) ||
        !readU32(input, offset, capability.maxWidth) ||
        !readU32(input, offset, capability.maxHeight) ||
        !readI32(input, offset, priority)) {
        return false;
    }
    capability.platform = static_cast<DisplayPlatformFamily>(platform);
    capability.backend = static_cast<DisplayCodecBackendKind>(backend);
    capability.codec = static_cast<DisplayCodecId>(codec);
    if (!knownPlatform(capability.platform) ||
        !knownCodecBackend(capability.backend) ||
        !knownCodecId(capability.codec)) {
        return false;
    }
    capability.supportsEncode = supportsEncode != 0;
    capability.supportsDecode = supportsDecode != 0;
    capability.available = available != 0;
    capability.fallback = fallback != 0;
    capability.hardwareAccelerated = hardwareAccelerated != 0;
    capability.zeroCopy = zeroCopy != 0;
    capability.lowLatency = lowLatency != 0;
    capability.requiresHardwareDevice = requiresHardwareDevice != 0;
    capability.priority = static_cast<int>(priority);
    return true;
}

bool appendCapabilities(protocol::ByteBuffer& output,
                        const std::vector<DisplayCodecCapability>& capabilities)
{
    if (!appendCount(output, capabilities.size()))
        return false;
    for (const DisplayCodecCapability& capability : capabilities) {
        if (!appendCapability(output, capability))
            return false;
    }
    return true;
}

bool readCapabilities(const protocol::ByteBuffer& input,
                      std::size_t& offset,
                      std::vector<DisplayCodecCapability>& capabilities)
{
    std::uint16_t count = 0;
    if (!readU16(input, offset, count))
        return false;
    capabilities.clear();
    for (std::uint16_t index = 0; index < count; ++index) {
        DisplayCodecCapability capability;
        if (!readCapability(input, offset, capability))
            return false;
        capabilities.push_back(std::move(capability));
    }
    return true;
}

bool appendSelectionRequest(protocol::ByteBuffer& output,
                            const DisplayCodecSelectionRequest& request)
{
    appendU16(output, static_cast<std::uint16_t>(request.platform));
    appendU16(output, static_cast<std::uint16_t>(request.direction));
    if (!appendEnumVector(output, request.codecPreference) ||
        !appendEnumVector(output, request.acceptedPixelFormats) ||
        !appendEnumVector(output, request.acceptedInputMemoryTypes) ||
        !appendEnumVector(output, request.acceptedOutputMemoryTypes)) {
        return false;
    }
    appendU16(output, static_cast<std::uint16_t>(request.architecture));
    appendU16(output, static_cast<std::uint16_t>(request.socProfile));
    if (!appendString(output, request.requestedAdapterId))
        return false;
    appendU8(output, request.allowHardware ? 1U : 0U);
    appendU8(output, request.allowSoftware ? 1U : 0U);
    appendU8(output, request.preferHardware ? 1U : 0U);
    appendU8(output, request.preferZeroCopy ? 1U : 0U);
    appendU8(output, request.requireLowLatency ? 1U : 0U);
    appendU32(output, request.width);
    appendU32(output, request.height);
    return appendCapabilities(output, request.candidates);
}

bool readSelectionRequest(const protocol::ByteBuffer& input,
                          std::size_t& offset,
                          DisplayCodecSelectionRequest& request)
{
    std::uint16_t platform = 0;
    std::uint16_t direction = 0;
    std::uint16_t architecture = 0;
    std::uint16_t socProfile = 0;
    std::uint8_t allowHardware = 0;
    std::uint8_t allowSoftware = 0;
    std::uint8_t preferHardware = 0;
    std::uint8_t preferZeroCopy = 0;
    std::uint8_t requireLowLatency = 0;
    if (!readU16(input, offset, platform) ||
        !readU16(input, offset, direction) ||
        !readEnumVector(input, offset, request.codecPreference, knownCodecId) ||
        !readEnumVector(input, offset, request.acceptedPixelFormats, knownPixelFormat) ||
        !readEnumVector(input, offset, request.acceptedInputMemoryTypes, knownMemoryType) ||
        !readEnumVector(input, offset, request.acceptedOutputMemoryTypes, knownMemoryType) ||
        !readU16(input, offset, architecture) ||
        !readU16(input, offset, socProfile) ||
        !readString(input, offset, request.requestedAdapterId) ||
        !readU8(input, offset, allowHardware) ||
        !readU8(input, offset, allowSoftware) ||
        !readU8(input, offset, preferHardware) ||
        !readU8(input, offset, preferZeroCopy) ||
        !readU8(input, offset, requireLowLatency) ||
        !readU32(input, offset, request.width) ||
        !readU32(input, offset, request.height) ||
        !readCapabilities(input, offset, request.candidates)) {
        return false;
    }
    request.platform = static_cast<DisplayPlatformFamily>(platform);
    request.direction = static_cast<DisplayCodecDirection>(direction);
    request.architecture = static_cast<DisplayTargetArchitecture>(architecture);
    request.socProfile = static_cast<DisplayTargetSocProfile>(socProfile);
    if (!knownPlatform(request.platform) ||
        !knownCodecDirection(request.direction) ||
        !knownArchitecture(request.architecture) ||
        !knownSocProfile(request.socProfile)) {
        return false;
    }
    request.allowHardware = allowHardware != 0;
    request.allowSoftware = allowSoftware != 0;
    request.preferHardware = preferHardware != 0;
    request.preferZeroCopy = preferZeroCopy != 0;
    request.requireLowLatency = requireLowLatency != 0;
    return true;
}

} // namespace

const char* displayCodecPeerProfileExtensionKey()
{
    return "display.codec.v1";
}

protocol::ByteBuffer encodeDisplayCodecPeerProfilePayload(
    const DisplayCodecPeerProfilePayload& payload)
{
    protocol::ByteBuffer output;
    appendU32(output, DisplayCodecPeerProfileMagic);
    appendU16(output, DisplayCodecPeerProfileVersion);
    appendU8(output, payload.hasEncoderRequest ? 1U : 0U);
    appendU8(output, payload.hasDecoderRequest ? 1U : 0U);
    if (payload.hasEncoderRequest &&
        !appendSelectionRequest(output, payload.encoderRequest)) {
        return {};
    }
    if (payload.hasDecoderRequest &&
        !appendSelectionRequest(output, payload.decoderRequest)) {
        return {};
    }
    return output;
}

DisplayCodecPeerProfileDecodeResult decodeDisplayCodecPeerProfilePayload(
    const protocol::ByteBuffer& payload)
{
    DisplayCodecPeerProfileDecodeResult result;
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t hasEncoderRequest = 0;
    std::uint8_t hasDecoderRequest = 0;
    if (!readU32(payload, offset, magic) ||
        !readU16(payload, offset, version) ||
        !readU8(payload, offset, hasEncoderRequest) ||
        !readU8(payload, offset, hasDecoderRequest)) {
        result.message = "display codec peer profile payload is truncated";
        return result;
    }
    if (magic != DisplayCodecPeerProfileMagic) {
        result.message = "display codec peer profile magic is invalid";
        return result;
    }
    if (version != DisplayCodecPeerProfileVersion) {
        result.message = "display codec peer profile version is unsupported";
        return result;
    }

    result.payload.hasEncoderRequest = hasEncoderRequest != 0;
    result.payload.hasDecoderRequest = hasDecoderRequest != 0;
    if (result.payload.hasEncoderRequest &&
        !readSelectionRequest(payload, offset, result.payload.encoderRequest)) {
        result.message = "display codec encoder request is invalid";
        return result;
    }
    if (result.payload.hasDecoderRequest &&
        !readSelectionRequest(payload, offset, result.payload.decoderRequest)) {
        result.message = "display codec decoder request is invalid";
        return result;
    }
    if (offset != payload.size()) {
        result.message = "display codec peer profile payload has trailing bytes";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
