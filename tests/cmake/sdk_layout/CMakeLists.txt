cmake_minimum_required(VERSION 3.25)
project(cantordust_sdk_layout CXX)

if(NOT SDK_LAYOUT_INPUT MATCHES "^(checkout|source)$")
    message(FATAL_ERROR "SDK_LAYOUT_INPUT must be checkout or source")
endif()

include("${CANTORDUST_SOURCE_DIR}/cmake/CantordustSdk.cmake")

set(_source "${CMAKE_CURRENT_BINARY_DIR}/fake-sdk/src")
file(MAKE_DIRECTORY "${_source}/include" "${_source}/cmake")
file(WRITE "${_source}/include/pro.h" "#define IDA_SDK_VERSION 940\n")
file(WRITE "${_source}/cmake/idasdk_init.cmake" "# fake init helper\n")

if(SDK_LAYOUT_INPUT STREQUAL "checkout")
    set(_input "${CMAKE_CURRENT_BINARY_DIR}/fake-sdk")
else()
    set(_input "${_source}")
endif()

set(IDASDK "${_input}" CACHE PATH "IDA SDK path" FORCE)
Cantordust_ResolveSdkSource(_resolved "${IDASDK}")
set(IDASDK "${_resolved}" CACHE PATH "IDA SDK source" FORCE)
set(ENV{IDASDK} "${IDASDK}")

if(NOT IDASDK STREQUAL _source OR NOT "$ENV{IDASDK}" STREQUAL _source)
    message(FATAL_ERROR "SDK source normalization did not persist")
endif()

# Reproduces the SDK imported target's include-directory construction.
add_library(idasdk::plugin INTERFACE IMPORTED)
set_target_properties(idasdk::plugin PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${IDASDK}/include")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/probe.cpp" "int Probe() { return 0; }\n")
add_library(probe STATIC "${CMAKE_CURRENT_BINARY_DIR}/probe.cpp")
target_link_libraries(probe PRIVATE idasdk::plugin)
