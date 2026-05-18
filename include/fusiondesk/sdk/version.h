#ifndef FUSIONDESK_SDK_VERSION_H
#define FUSIONDESK_SDK_VERSION_H

#include <cstdint>

namespace fusiondesk {
namespace sdk {

constexpr std::uint16_t SdkVersionMajor = 0;
constexpr std::uint16_t SdkVersionMinor = 1;
constexpr std::uint16_t SdkVersionPatch = 0;

constexpr const char* SdkVersionString = "0.1.0";

} // namespace sdk
} // namespace fusiondesk

#endif // FUSIONDESK_SDK_VERSION_H
