if(NOT DEFINED FUSIONDESK_ROOT)
    message(FATAL_ERROR "FUSIONDESK_ROOT is required")
endif()

set(scan_roots
    "${FUSIONDESK_ROOT}/include/fusiondesk/core"
    "${FUSIONDESK_ROOT}/src/core"
    "${FUSIONDESK_ROOT}/include/fusiondesk/modules"
    "${FUSIONDESK_ROOT}/src/modules"
)

set(forbidden_tokens
    "#include <Q"
    "#include<Q"
    "#include <Qt"
    "#include<Qt"
    "QString"
    "QByteArray"
    "QObject"
    "QTcpSocket"
    "QThread"
    "QVariant"
    "QJson"
    "QWindow"
    "QAndroid"
    "Source/"
    "ThirdParty/"
)

set(violations)

foreach(root IN LISTS scan_roots)
    if(NOT EXISTS "${root}")
        continue()
    endif()

    file(GLOB_RECURSE source_files
        "${root}/*.h"
        "${root}/*.hpp"
        "${root}/*.cpp"
        "${root}/*.cc"
        "${root}/*.cxx"
    )

    foreach(source_file IN LISTS source_files)
        file(READ "${source_file}" content)
        foreach(token IN LISTS forbidden_tokens)
            string(FIND "${content}" "${token}" token_index)
            if(NOT token_index EQUAL -1)
                file(RELATIVE_PATH relative_file "${FUSIONDESK_ROOT}" "${source_file}")
                list(APPEND violations "${relative_file}: forbidden token '${token}'")
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n" violation_text)
    message(FATAL_ERROR "FusionDesk source purity check failed:\n${violation_text}")
endif()

message(STATUS "FusionDesk source purity check passed")
