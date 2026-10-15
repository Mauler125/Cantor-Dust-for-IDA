# Minimal Qt plug-in integration for IDA SDK 9.2/9.3, which do not ship the
# idasdk CMake package. Do not import newer SDK build scripts into old SDKs.
include_guard(GLOBAL)

if(WIN32)
    if(NOT MSVC OR CMAKE_SIZEOF_VOID_P LESS 8)
        message(FATAL_ERROR "IDA 9.2/9.3 Windows plugin builds require x64 MSVC")
    endif()
    set(_cd_library "${IDASDK}/lib/x64_win_vc_64/ida.lib")
    set(_cd_platform __NT__)
elseif(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64;arm64")
        message(FATAL_ERROR
            "Legacy macOS IDA packages must be universal: "
            "set CMAKE_OSX_ARCHITECTURES=x86_64\\;arm64")
    endif()
    find_program(_cd_lipo NAMES lipo REQUIRED)
    set(_cd_universal_lib "${CMAKE_CURRENT_BINARY_DIR}/ida-legacy-universal.dylib")
    execute_process(COMMAND "${_cd_lipo}" -create
        "${IDASDK}/lib/x64_mac_clang_64/libida.dylib"
        "${IDASDK}/lib/arm64_mac_clang_64/libida.dylib"
        -output "${_cd_universal_lib}"
        RESULT_VARIABLE _cd_rc)
    if(NOT _cd_rc EQUAL 0)
        message(FATAL_ERROR "Could not merge IDA legacy macOS import libraries")
    endif()
    set(_cd_library "${_cd_universal_lib}")
    set(_cd_platform __MAC__ __UNIX__)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR
            "IDA SDK 9.2/9.3 only provides Linux x86_64 import libraries")
    endif()
    set(_cd_library "${IDASDK}/lib/x64_linux_gcc_64/libida.so")
    set(_cd_platform __LINUX__ __UNIX__)
else()
    message(FATAL_ERROR "Unsupported legacy IDA SDK host: ${CMAKE_SYSTEM_NAME}")
endif()
if(NOT EXISTS "${_cd_library}")
    message(FATAL_ERROR "Missing IDA SDK import library: ${_cd_library}")
endif()

function(ida_add_plugin name)
    cmake_parse_arguments(P "" "TYPE" "SOURCES;DEFINES;QT_COMPONENTS" ${ARGN})
    if(NOT P_TYPE STREQUAL "QT")
        message(FATAL_ERROR "Legacy IDA adapter only supports TYPE QT")
    endif()
    add_library(${name} MODULE ${P_SOURCES})
    target_include_directories(${name} PRIVATE "${IDASDK}/include" "${IDASDK}")
    target_compile_definitions(${name} PRIVATE
        __IDP__ __EA64__=1 ${_cd_platform} ${P_DEFINES} __QT__)
    target_link_libraries(${name} PRIVATE "${_cd_library}")
    foreach(_part IN LISTS P_QT_COMPONENTS)
        target_link_libraries(${name} PRIVATE "Qt6::${_part}")
    endforeach()
    set_target_properties(${name} PROPERTIES
        PREFIX "" AUTOMOC ON AUTOUIC ON AUTORCC ON
        LIBRARY_OUTPUT_DIRECTORY "${IDABIN}/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${IDABIN}/plugins")
    if(MSVC)
        target_compile_options(${name} PRIVATE /EHsc /utf-8)
        target_link_options(${name} PRIVATE /SUBSYSTEM:WINDOWS)
    else()
        target_compile_options(${name} PRIVATE -fno-strict-aliasing -fvisibility=hidden)
    endif()
endfunction()
