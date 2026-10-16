cmake_minimum_required(VERSION 3.25)

option(CANTORDUST_BUILD_IDA_PLUGIN "Build the native IDA Qt plugin" OFF)

if(CANTORDUST_BUILD_IDA_PLUGIN)
    if(NOT IDASDK AND DEFINED ENV{IDASDK})
        set(IDASDK "$ENV{IDASDK}" CACHE PATH "Path to the IDA SDK")
    endif()

    if(NOT IDASDK)
        message(FATAL_ERROR
            "Set -DIDASDK=/path/to/ida-sdk or the IDASDK environment variable")
    endif()

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CantordustSdk.cmake")
    Cantordust_ResolveSdkSource(_cantordust_sdk_source "${IDASDK}")

    # The SDK init helper normalizes ENV{IDASDK}, but find_package(idasdk)
    # gives the CMake variable precedence. Keep both paths identical.
    set(IDASDK "${_cantordust_sdk_source}" CACHE PATH
        "IDA SDK source directory (containing include/pro.h)" FORCE)
    set(ENV{IDASDK} "${IDASDK}")
    message(STATUS "CantorDust SDK source directory: ${IDASDK}")
    if(EXISTS "${IDASDK}/cmake/idasdk_init.cmake")
        include("${IDASDK}/cmake/idasdk_init.cmake")
        set(CANTORDUST_LEGACY_SDK OFF)
    else()
        set(CANTORDUST_LEGACY_SDK ON)
    endif()
    file(STRINGS "${IDASDK}/include/pro.h" _cd_sdk_version_line
        REGEX "^#[ \t]*define[ \t]+IDA_SDK_VERSION[ \t]+[0-9]+")
    if(NOT _cd_sdk_version_line MATCHES "IDA_SDK_VERSION[ \t]+([0-9]+)")
        message(FATAL_ERROR "Cannot identify SDK version in ${IDASDK}/include/pro.h")
    endif()
    set(CANTORDUST_SDK_VERSION_NUM "${CMAKE_MATCH_1}")
    if(CANTORDUST_SDK_VERSION_NUM LESS 920)
        message(FATAL_ERROR "Cantor Dust native builds require SDK 9.2 or newer")
    endif()
    if(CANTORDUST_TARGET_IDA_VERSION)
        string(REGEX MATCH "^9\.([0-9]+)" _cd_minor_match
            "${CANTORDUST_TARGET_IDA_VERSION}")
        if(NOT _cd_minor_match)
            message(FATAL_ERROR "Invalid CANTORDUST_TARGET_IDA_VERSION")
        endif()
        math(EXPR _cd_target_num "900 + ${CMAKE_MATCH_1} * 10")
        if(NOT CANTORDUST_SDK_VERSION_NUM EQUAL _cd_target_num)
            message(FATAL_ERROR "SDK version ${CANTORDUST_SDK_VERSION_NUM} does not "
                "match requested IDA ${CANTORDUST_TARGET_IDA_VERSION}")
        endif()
    endif()
    message(STATUS "Cantor Dust SDK ${CANTORDUST_SDK_VERSION_NUM} "
        "(legacy: ${CANTORDUST_LEGACY_SDK})")
endif()

project(ida_cantordust VERSION 0.8.0 LANGUAGES CXX)

# A native ARM64 runner can still pick the emulated x64 MSVC toolchain.
# Fail during configuration, before any 46-step compile or costly link.
if(CANTORDUST_BUILD_IDA_PLUGIN AND WIN32 AND
   CANTORDUST_EXPECTED_TARGET_ARCH STREQUAL "ARM64")
    string(REPLACE "\\" "/" _cd_compiler_path "${CMAKE_CXX_COMPILER}")
    string(TOLOWER "${_cd_compiler_path}" _cd_compiler_path)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$" OR
       NOT _cd_compiler_path MATCHES "/arm64/cl\\.exe$")
        message(FATAL_ERROR
            "Windows ARM64 target selected, but CMake configured "
            "${CMAKE_CXX_COMPILER} for ${CMAKE_SYSTEM_PROCESSOR}. "
            "Pass the SDK arm-Windows.toolchain.cmake before project().")
    endif()
    message(STATUS "Cantor Dust native target architecture: Windows ARM64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opt-in diagnostics for portable code; the SDK/Qt plugin retains its own
# compiler settings so these switches cannot alter IDA's ABI configuration.
option(CANTORDUST_STRICT_WARNINGS "Enable additional portable C++ warnings" OFF)
option(CANTORDUST_ENABLE_SANITIZERS "Use ASan and UBSan for portable tests" OFF)
option(CANTORDUST_CHECK_HEADERS "Compile portable headers independently" OFF)
option(CANTORDUST_USE_PCH "Use target-specific precompiled headers for faster builds" ON)

option(CANTORDUST_MAX_RELEASE_OPTIMIZATIONS
    "Use LTO and platform-specific whole-program optimizations for Release builds" ON)

# GCC/Clang already use -O3 in CMake's Release profile; MSVC uses /O2.
# Cross-translation-unit optimization was previously missing. Apply IPO/LTO
# to every Cantor Dust target (core, native IDA plugin, tests and tools) when
# it is built as Release. Keep Debug/sanitizer profiles fully diagnostic.
# Do not enable -march=native (binary portability) or fast-math (NaN/Inf and
# floating-point behavior matter to the byte-cloud decoders).
if(CANTORDUST_MAX_RELEASE_OPTIMIZATIONS AND NOT CANTORDUST_ENABLE_SANITIZERS)
    if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release")
        include(CheckIPOSupported)
        check_ipo_supported(RESULT _cantordust_ipo_supported
            OUTPUT _cantordust_ipo_error LANGUAGES CXX)

        # Some Linux Clang distributions ship lld but no LLVMgold.so. In
        # that case, GCC's default system linker cannot consume LLVM bitcode.
        # Retry IPO with lld; the check module picks up CXX_FLAGS for its
        # nested test project, while add_link_options handles real targets.
        if(NOT _cantordust_ipo_supported AND
           CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND UNIX AND NOT APPLE)
            find_program(_cantordust_lld NAMES ld.lld)
            if(_cantordust_lld)
                set(_cantordust_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
                string(APPEND CMAKE_CXX_FLAGS " -fuse-ld=lld")
                check_ipo_supported(RESULT _cantordust_lld_ipo_supported
                    OUTPUT _cantordust_lld_ipo_error LANGUAGES CXX)
                set(CMAKE_CXX_FLAGS "${_cantordust_saved_cxx_flags}")
                if(_cantordust_lld_ipo_supported)
                    set(_cantordust_ipo_supported TRUE)
                    add_link_options("$<$<CONFIG:Release>:-fuse-ld=lld>")
                    message(STATUS "Cantor Dust Release: using LLVM lld for LTO")
                else()
                    set(_cantordust_ipo_error "${_cantordust_lld_ipo_error}")
                endif()
            endif()
        endif()
        if(NOT _cantordust_ipo_supported)
            message(FATAL_ERROR
                "Release LTO/IPO is unsupported by this toolchain:\n"
                "${_cantordust_ipo_error}\n"
                "Use -DCANTORDUST_MAX_RELEASE_OPTIMIZATIONS=OFF only if "
                "your SDK/toolchain does not support LTO.")
        endif()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        message(STATUS "Cantor Dust Release: whole-program IPO/LTO enabled")
    endif()

    if(MSVC)
        # MSVC's default Release flags include /Ob2. Remove that inherited
        # inlining level before adding /Ob3, otherwise cl.exe emits D9025
        # for every translation unit. Keep /O2, /GL and /LTCG unchanged.
        string(REGEX REPLACE "(^|[ \t])/Ob[0-3]([ \t]|$)" "\\1\\2"
            CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        # Use the highest inlining level, function/global sections and
        # linker-level dead/identical code elimination in Release only.
        # CMake's IPO property injects /GL and /LTCG.
        add_compile_options(
            "$<$<CONFIG:Release>:/Ob3>"
            "$<$<CONFIG:Release>:/Gy>"
            "$<$<CONFIG:Release>:/Gw>")
        add_link_options(
            "$<$<CONFIG:Release>:/OPT:REF>"
            "$<$<CONFIG:Release>:/OPT:ICF>"
            "$<$<CONFIG:Release>:/INCREMENTAL:NO>")
    elseif(APPLE)
        add_compile_options(
            "$<$<CONFIG:Release>:-ffunction-sections>"
            "$<$<CONFIG:Release>:-fdata-sections>")
        add_link_options("$<$<CONFIG:Release>:LINKER:-dead_strip>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            "$<$<CONFIG:Release>:-ffunction-sections>"
            "$<$<CONFIG:Release>:-fdata-sections>")
        add_link_options("$<$<CONFIG:Release>:LINKER:--gc-sections>")
    endif()
endif()

if(CANTORDUST_ENABLE_SANITIZERS)
    if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ASan/UBSan option requires GCC or Clang outside MSVC")
    endif()

    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

set(CANTORDUST_ENGINE_SOURCES
    src/tier1/universe_explore.cpp
    src/tier1/universe_routes.cpp
    src/tier1/universe_export.cpp
    src/tier1/universe_communities.cpp
    src/tier1/universe_filter.cpp
    src/tier1/universe_inspector.cpp
    src/tier1/universe_history.cpp
    src/math/color.cpp
    src/math/cube.cpp
    src/math/cube_lighting.cpp
    src/math/density.cpp
    src/math/entropy.cpp
    src/math/hilbert.cpp
    src/math/pixel.cpp
    src/math/ranges.cpp
    src/math/raster_fit.cpp
    src/math/window.cpp
    src/math/zoom.cpp
    src/math/universe.cpp
    src/render/palette.cpp
    src/render/cube.cpp
    src/render/render.cpp
    src/render/universe.cpp
    src/render/universe_raster.cpp
)

add_library(cantordust_core STATIC ${CANTORDUST_ENGINE_SOURCES})
target_include_directories(cantordust_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(cantordust_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# A PCH is private to its compilation target. Do not propagate it to tests,
# tools, SDK mock targets, or header self-containment checks.
if(CANTORDUST_USE_PCH)
    target_precompile_headers(cantordust_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/pch/core_pch.h")
endif()

if(CANTORDUST_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(cantordust_core PRIVATE /W4)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cantordust_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(CANTORDUST_CHECK_HEADERS)
    # Each generated translation unit includes ONE header. A combined TU
    # would conceal missing direct dependencies via incidental includes.
    file(GLOB _cantordust_headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/public/cantordust/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/math/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tier1/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.h")

    set(_cantordust_header_sources)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/header_self_containment")

    foreach(_header IN LISTS _cantordust_headers)
        file(RELATIVE_PATH _relative "${CMAKE_CURRENT_SOURCE_DIR}/src" "${_header}")
        string(MAKE_C_IDENTIFIER "${_relative}" _identifier)
        set(_unit "${CMAKE_CURRENT_BINARY_DIR}/header_self_containment/${_identifier}.cpp")
        file(WRITE "${_unit}" "#include \"${_relative}\"\n")
        list(APPEND _cantordust_header_sources "${_unit}")
    endforeach()

    add_library(cantordust_header_self_containment OBJECT
        ${_cantordust_header_sources})
    target_include_directories(cantordust_header_self_containment PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/public)
endif()

include(CTest)
if(BUILD_TESTING)
    # Native IDA SDK headers use .hpp names (ida.hpp, funcs.hpp, etc.).
    # The mock implementations live in tests/stubs/*.h. Copy them to the
    # build tree under the SDK names so production SDK includes stay intact.
    set(_cantordust_mock_sdk_dir
        "${CMAKE_CURRENT_BINARY_DIR}/ida_mock_sdk")
    file(MAKE_DIRECTORY "${_cantordust_mock_sdk_dir}")

    foreach(_ida_header IN ITEMS ida bytes funcs name segment xref)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs/${_ida_header}.h"
            "${_cantordust_mock_sdk_dir}/${_ida_header}.hpp"
            COPYONLY)
    endforeach()

    add_test(NAME compound_scalar_alias_syntax
        COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/check_compound_scalar_aliases.cmake)

    add_executable(cantordust_universe_search_layout_tests
        tests/universe_search_layout_tests.cpp)
    target_include_directories(cantordust_universe_search_layout_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME universe_search_layout
        COMMAND cantordust_universe_search_layout_tests)

    add_executable(cantordust_scalar_types_tests tests/scalar_types_tests.cpp)
    target_include_directories(cantordust_scalar_types_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/public)
    add_test(NAME scalar_types COMMAND cantordust_scalar_types_tests)

    add_executable(cantordust_constants_tests tests/constants_tests.cpp)
    target_include_directories(cantordust_constants_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cantordust_constants_tests PRIVATE cantordust_core)
    add_test(NAME named_constants COMMAND cantordust_constants_tests)

    add_executable(cantordust_palette_roles_tests tests/palette_roles_tests.cpp)
    target_link_libraries(cantordust_palette_roles_tests PRIVATE cantordust_core)
    add_test(NAME palette_roles COMMAND cantordust_palette_roles_tests)

    add_executable(cantordust_raster_fit_tests tests/raster_fit_tests.cpp)
    target_include_directories(cantordust_raster_fit_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cantordust_raster_fit_tests PRIVATE cantordust_core)
    add_test(NAME raster_fit COMMAND cantordust_raster_fit_tests)

    add_executable(cantordust_tests tests/core_tests.cpp)
    target_include_directories(cantordust_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(cantordust_tests PRIVATE cantordust_core)
    add_test(NAME core COMMAND cantordust_tests)

    add_executable(cantordust_cube_render_regression_tests
        tests/cube_render_regression_tests.cpp
    )
    target_include_directories(cantordust_cube_render_regression_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(cantordust_cube_render_regression_tests
        PRIVATE cantordust_core
    )
    add_test(NAME cube_render_regression
        COMMAND cantordust_cube_render_regression_tests)

    add_executable(cantordust_universe_explore_tests
        tests/universe_explore_tests.cpp)
    target_include_directories(cantordust_universe_explore_tests
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cantordust_universe_explore_tests
        PRIVATE cantordust_core)
    add_test(NAME universe_explore
        COMMAND cantordust_universe_explore_tests)

    add_executable(cantordust_universe_communities_tests
        tests/universe_communities_tests.cpp)
    target_link_libraries(cantordust_universe_communities_tests
        PRIVATE cantordust_core)
    add_test(NAME universe_communities
        COMMAND cantordust_universe_communities_tests)

    add_executable(cantordust_struct_layout_tests
        tests/struct_layout_tests.cpp)
    target_link_libraries(cantordust_struct_layout_tests PRIVATE cantordust_core)
    add_test(NAME hot_struct_layout COMMAND cantordust_struct_layout_tests)

    add_executable(cantordust_precompute_render_tests
        tests/precompute_render_tests.cpp)
    target_include_directories(cantordust_precompute_render_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cantordust_precompute_render_tests
        PRIVATE cantordust_core)
    add_test(NAME precompute_render COMMAND cantordust_precompute_render_tests)

    add_executable(cantordust_cube_modes_tests tests/cube_modes_tests.cpp)
    target_link_libraries(cantordust_cube_modes_tests PRIVATE cantordust_core)
    add_test(NAME cube_modes COMMAND cantordust_cube_modes_tests)

    add_executable(cantordust_cube_sampling_tests tests/cube_sampling_tests.cpp)
    target_link_libraries(cantordust_cube_sampling_tests PRIVATE cantordust_core)
    add_test(NAME cube_sampling_extended COMMAND cantordust_cube_sampling_tests)

    add_executable(cantordust_universe_filter_tests
        tests/universe_filter_tests.cpp)
    target_link_libraries(cantordust_universe_filter_tests
        PRIVATE cantordust_core)
    add_test(NAME universe_filter COMMAND cantordust_universe_filter_tests)

    add_executable(cantordust_universe_inspector_tests
        tests/universe_inspector_tests.cpp)
    target_link_libraries(cantordust_universe_inspector_tests
        PRIVATE cantordust_core)
    add_test(NAME universe_inspector
        COMMAND cantordust_universe_inspector_tests)

    add_executable(cantordust_universe_export_tests
        tests/universe_export_tests.cpp)
    target_link_libraries(cantordust_universe_export_tests PRIVATE cantordust_core)
    add_test(NAME universe_export COMMAND cantordust_universe_export_tests)

    add_executable(cantordust_universe_tests tests/universe_tests.cpp)
    target_include_directories(cantordust_universe_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cantordust_universe_tests PRIVATE cantordust_core)
    add_test(NAME universe COMMAND cantordust_universe_tests)

    add_executable(cantordust_ida_universe_mock_tests
        tests/ida_universe_mock_tests.cpp
        src/ida/ida_universe.cpp)
    target_include_directories(cantordust_ida_universe_mock_tests PRIVATE
        ${_cantordust_mock_sdk_dir}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/public)
    target_link_libraries(cantordust_ida_universe_mock_tests PRIVATE cantordust_core)
    add_test(NAME ida_universe_mock COMMAND cantordust_ida_universe_mock_tests)

    add_executable(cantordust_ida_memory_mock_tests
        tests/ida_memory_mock_tests.cpp
        src/ida/ida_memory.cpp
    )
    target_include_directories(cantordust_ida_memory_mock_tests PRIVATE
        ${_cantordust_mock_sdk_dir}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/public
    )
    target_link_libraries(cantordust_ida_memory_mock_tests PRIVATE cantordust_core)
    # Qt defines `slots` as a macro in the native plugin. Mirror that in
    # the portable IDA memory mock so local identifier collisions fail CI.
    target_compile_definitions(cantordust_ida_memory_mock_tests PRIVATE slots=)
    add_test(NAME ida_memory_mock COMMAND cantordust_ida_memory_mock_tests)

    add_test(NAME qt_namespace_accept
        COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/qt_namespace
            -B ${CMAKE_CURRENT_BINARY_DIR}/qt-namespace-accept
            -DQT_NAMESPACE_TEST_VALUE=QT)
    add_test(NAME qt_namespace_reject
        COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/qt_namespace
            -B ${CMAKE_CURRENT_BINARY_DIR}/qt-namespace-reject
            -DQT_NAMESPACE_TEST_VALUE=Wrong)
    set_tests_properties(qt_namespace_reject PROPERTIES WILL_FAIL TRUE)

    foreach(_layout IN ITEMS checkout source)
        add_test(NAME sdk_layout_${_layout}
            COMMAND ${CMAKE_COMMAND}
                -S ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/sdk_layout
                -B ${CMAKE_CURRENT_BINARY_DIR}/sdk-layout-${_layout}
                -DSDK_LAYOUT_INPUT=${_layout}
                -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR})
    endforeach()

    # Validate the embedded icon manifest even on machines without IDA/Qt.
    add_test(NAME svg_icon_resource
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/svg_icon_resource.cmake)

    add_test(NAME panel_menu_wiring
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/panel_menu_wiring.cmake)

    add_test(NAME panel_supplemental_controls
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/panel_supplemental_controls.cmake)

    add_test(NAME universe_hop_budget_wiring
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/universe_hop_budget_wiring.cmake)

    add_test(NAME panel_overview_first_layout
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/panel_overview_first_layout.cmake)

    add_test(NAME panel_call_path_endpoint_order
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/panel_call_path_endpoint_order.cmake)

    add_test(NAME call_path_shortcuts
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/call_path_shortcuts.cmake)

    add_test(NAME panel_narrow_universe_layout
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/panel_narrow_universe_layout.cmake)

    add_test(NAME cantor_panel_split_integrity
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/cantor_panel_split_integrity.cmake)

    # Catch frozen halo/route pulse parameters even in headless CI where
    # the Qt/IDA widget itself cannot be compiled or instantiated.
    add_test(NAME universe_widget_pulse_wiring
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/universe_widget_pulse_wiring.cmake)

    # The IDA widget is excluded from portable builds. Keep export methods
    # defined so Windows plugin linking cannot fail on missing symbols.
    add_test(NAME universe_widget_export_definitions
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/universe_widget_export_definitions.cmake)

    add_test(NAME palette_ui_wiring
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/palette_ui_wiring.cmake)

    add_test(NAME overview_range_wiring
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/overview_range_wiring.cmake)

    add_test(NAME detail_range_status_sync
        COMMAND ${CMAKE_COMMAND}
            -DCANTORDUST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/detail_range_status_sync.cmake)
endif()

add_executable(cantordust_universe_benchmark tools/universe_benchmark.cpp)
target_link_libraries(cantordust_universe_benchmark PRIVATE cantordust_core)

add_executable(cantordust_universe_explore_preview
    tools/universe_explore_preview.cpp)
target_link_libraries(cantordust_universe_explore_preview
    PRIVATE cantordust_core)

add_executable(cantordust_universe_preview tools/universe_preview.cpp)
target_link_libraries(cantordust_universe_preview PRIVATE cantordust_core)

add_executable(cantordust_precompute_benchmark tools/precompute_benchmark.cpp)
target_include_directories(cantordust_precompute_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cantordust_precompute_benchmark PRIVATE cantordust_core)

add_executable(cantordust_cube_benchmark tools/cube_benchmark.cpp)
target_link_libraries(cantordust_cube_benchmark PRIVATE cantordust_core)

add_executable(cantordust_cube_preview tools/cube_preview.cpp)
target_link_libraries(cantordust_cube_preview PRIVATE cantordust_core)

add_executable(cantordust_preview tools/preview.cpp)
target_link_libraries(cantordust_preview PRIVATE cantordust_core)

if(CANTORDUST_BUILD_IDA_PLUGIN)
    if(NOT CANTORDUST_LEGACY_SDK)
        find_package(idasdk REQUIRED)
    endif()

    # The SDK skips Qt targets when the Qt development package is absent.
    find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
    if(NOT Qt6_FOUND)
        message(FATAL_ERROR
            "Cantor Dust needs Qt6 Core, Gui and Widgets development files.\n"
            "IDA's bundled Qt runtime is not sufficient for compilation.\n"
            "Install a Qt build compatible with your IDA installation and "
            "configured with -qtnamespace QT.\n"
            "Windows example:\n"
            "  scripts/build-ida.ps1 -Sdk C:/dev/ida-sdk-9.4 "
            "-QtPrefix C:/dev/qt-ida-<version>\n"
            "Linux/macOS example:\n"
            "  CANTORDUST_QT_PREFIX=/opt/qt-ida-<version> "
            "scripts/build-ida.sh /path/to/ida-sdk\n"
            "The Qt prefix must contain lib/cmake/Qt6/Qt6Config.cmake.")
    endif()
    message(STATUS "CantorDust Qt version: ${Qt6_VERSION}")
    if(CANTORDUST_EXPECTED_QT_VERSION AND
       NOT Qt6_VERSION VERSION_EQUAL CANTORDUST_EXPECTED_QT_VERSION)
        message(FATAL_ERROR
            "The Qt development version is ${Qt6_VERSION}, but IDA needs "
            "${CANTORDUST_EXPECTED_QT_VERSION}. Rebuild the matching "
            "namespaced QtBase or choose the correct Qt prefix.")
    endif()

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CantordustQt.cmake")
    Cantordust_RequireQtNamespace(Qt6::Core)

    if(CANTORDUST_LEGACY_SDK)
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CantordustLegacySdk.cmake")
    endif()

    ida_add_plugin(cantor_dust
        TYPE QT
        QT_COMPONENTS Core Gui Widgets
        SOURCES
            src/ida/ida_plugin.cpp
            src/ida/cantor_panel.cpp
            src/ida/cantor_panel_menus.cpp
            src/ida/cantor_panel_bookmarks.cpp
            src/ida/cantor_panel_ranges.cpp
            src/ida/cantor_panel_universe.cpp
            src/ida/cantor_panel_inspector.cpp
            src/ida/cantor_panel_render_navigation.cpp
            src/ida/cube_widget.cpp
            src/ida/entropy_widget.cpp
            src/ida/ida_memory.cpp
            src/ida/ida_universe.cpp
            src/ida/universe_widget.cpp
            src/ida/plot_widget.cpp
            resources/cantor_dust.qrc
        DEFINES QT_NAMESPACE=QT QT_CORE_LIB QT_DLL QT_GUI_LIB
                QT_THREAD_SUPPORT QT_WIDGETS_LIB
    )

    if(NOT TARGET cantor_dust)
        message(FATAL_ERROR
            "IDA SDK skipped cantor_dust. Check that your Qt6 build "
            "matches the Qt namespace, version and ABI required by IDA. "
            "Point CMAKE_PREFIX_PATH at that Qt installation.")
    endif()

    # The SDK's ida_add_plugin() creates a CMake MODULE target, which by
    # default gets a .so suffix even on macOS. The HCLI native plugin
    # packager and IDA's macOS plugin layout require a .dylib file.
    # Apply this after ida_add_plugin for both legacy and native SDKs.
    if(APPLE)
        set_target_properties(cantor_dust PROPERTIES SUFFIX ".dylib")
    endif()

    # Embed icons into the plugin: no dependency on the caller's working
    # directory or a separately installed resource directory at runtime.
    set_target_properties(cantor_dust PROPERTIES AUTORCC ON)

    target_include_directories(cantor_dust PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(cantor_dust PRIVATE cantordust_core)

    # Qt and IDA builds need different compilation environments. The plugin
    # gets its own Qt-only PCH, with no IDA SDK headers or portable PCH reuse.
    if(CANTORDUST_USE_PCH)
        target_precompile_headers(cantor_dust PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/pch/ida_pch.h")
    endif()

    if(MSVC)
        target_compile_options(cantor_dust PRIVATE /GR)
    endif()
endif()
