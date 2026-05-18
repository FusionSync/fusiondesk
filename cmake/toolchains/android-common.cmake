if(NOT DEFINED ANDROID_ABI)
    message(FATAL_ERROR "ANDROID_ABI is required before including android-common.cmake")
endif()

set(ANDROID_PLATFORM android-23 CACHE STRING "Android API level")
set(ANDROID_STL c++_shared CACHE STRING "Android STL")

if(DEFINED ENV{ANDROID_NDK_HOME})
    file(TO_CMAKE_PATH "$ENV{ANDROID_NDK_HOME}" FUSIONDESK_ANDROID_NDK)
elseif(DEFINED ENV{ANDROID_NDK_ROOT})
    file(TO_CMAKE_PATH "$ENV{ANDROID_NDK_ROOT}" FUSIONDESK_ANDROID_NDK)
else()
    message(FATAL_ERROR "Set ANDROID_NDK_HOME or ANDROID_NDK_ROOT before using Android presets")
endif()

set(FUSIONDESK_ANDROID_TOOLCHAIN
    "${FUSIONDESK_ANDROID_NDK}/build/cmake/android.toolchain.cmake")

if(NOT EXISTS "${FUSIONDESK_ANDROID_TOOLCHAIN}")
    message(FATAL_ERROR "Android toolchain file not found: ${FUSIONDESK_ANDROID_TOOLCHAIN}")
endif()

include("${FUSIONDESK_ANDROID_TOOLCHAIN}")
