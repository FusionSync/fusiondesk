#ifndef FUSIONDESK_PROTOCOL_FEATURE_FLAGS_H
#define FUSIONDESK_PROTOCOL_FEATURE_FLAGS_H

#include <cstdint>

namespace fusiondesk {
namespace protocol {

using FeatureMask = std::uint64_t;

namespace feature {

constexpr FeatureMask Mouse = 0x0000000000000001ULL;
constexpr FeatureMask Keyboard = 0x0000000000000002ULL;
constexpr FeatureMask Display = 0x0000000000000004ULL;
constexpr FeatureMask Audio = 0x0000000000000008ULL;
constexpr FeatureMask Clipboard = 0x0000000000000010ULL;
constexpr FeatureMask Microphone = 0x0000000000000020ULL;
constexpr FeatureMask Camera = 0x0000000000000040ULL;
constexpr FeatureMask SecondDisplay = 0x0000000000000080ULL;
constexpr FeatureMask Filesystem = 0x0000000000000100ULL;
constexpr FeatureMask Screen = 0x0000000000000200ULL;
constexpr FeatureMask Printer = 0x0000000000000400ULL;
constexpr FeatureMask Touch = 0x0000000000000800ULL;
constexpr FeatureMask Gamepad = 0x0000000000001000ULL;
constexpr FeatureMask PeripheralUsb = 0x0000000100000000ULL;

constexpr FeatureMask RemoteDesktopBase = Screen | Display | Mouse | Keyboard;
constexpr FeatureMask RedirectionSuite = Clipboard | Filesystem | Printer | Camera | Audio | Microphone;
constexpr FeatureMask EnterpriseSuite = RemoteDesktopBase | RedirectionSuite | SecondDisplay | Touch | Gamepad | PeripheralUsb;

} // namespace feature

struct FeatureSet
{
    FeatureMask bits = 0;

    bool has(FeatureMask feature) const
    {
        return (bits & feature) == feature;
    }

    void enable(FeatureMask feature)
    {
        bits |= feature;
    }

    void disable(FeatureMask feature)
    {
        bits &= ~feature;
    }
};

} // namespace protocol
} // namespace fusiondesk

#endif // FUSIONDESK_PROTOCOL_FEATURE_FLAGS_H
