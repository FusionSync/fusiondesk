if(NOT DEFINED FUSIONDESK_TOOLCHAIN_ENV_PREFIX)
    message(FATAL_ERROR "FUSIONDESK_TOOLCHAIN_ENV_PREFIX is required before including linux-cross-common.cmake")
endif()

if(NOT DEFINED FUSIONDESK_TOOLCHAIN_PROCESSOR)
    message(FATAL_ERROR "FUSIONDESK_TOOLCHAIN_PROCESSOR is required before including linux-cross-common.cmake")
endif()

function(fusiondesk_env_path output suffix)
    set(env_name "${FUSIONDESK_TOOLCHAIN_ENV_PREFIX}_${suffix}")
    if(DEFINED ENV{${env_name}})
        file(TO_CMAKE_PATH "$ENV{${env_name}}" env_value)
        set(${output} "${env_value}" PARENT_SCOPE)
    endif()
endfunction()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "${FUSIONDESK_TOOLCHAIN_PROCESSOR}")

fusiondesk_env_path(FUSIONDESK_TOOLCHAIN_SYSROOT SYSROOT)
fusiondesk_env_path(FUSIONDESK_TOOLCHAIN_C C)
fusiondesk_env_path(FUSIONDESK_TOOLCHAIN_CXX CXX)

if(FUSIONDESK_TOOLCHAIN_SYSROOT)
    set(CMAKE_SYSROOT "${FUSIONDESK_TOOLCHAIN_SYSROOT}" CACHE PATH "FusionDesk target sysroot" FORCE)
    set(CMAKE_FIND_ROOT_PATH "${FUSIONDESK_TOOLCHAIN_SYSROOT}" CACHE STRING "FusionDesk target root path" FORCE)
endif()

if(FUSIONDESK_TOOLCHAIN_C)
    set(CMAKE_C_COMPILER "${FUSIONDESK_TOOLCHAIN_C}" CACHE FILEPATH "FusionDesk C compiler" FORCE)
else()
    message(FATAL_ERROR "Set ${FUSIONDESK_TOOLCHAIN_ENV_PREFIX}_C to the target C compiler")
endif()

if(FUSIONDESK_TOOLCHAIN_CXX)
    set(CMAKE_CXX_COMPILER "${FUSIONDESK_TOOLCHAIN_CXX}" CACHE FILEPATH "FusionDesk CXX compiler" FORCE)
else()
    message(FATAL_ERROR "Set ${FUSIONDESK_TOOLCHAIN_ENV_PREFIX}_CXX to the target C++ compiler")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
