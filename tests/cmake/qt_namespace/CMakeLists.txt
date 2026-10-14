cmake_minimum_required(VERSION 3.25)
project(cantordust_qt_namespace_fixture LANGUAGES NONE)

if(NOT DEFINED QT_NAMESPACE_TEST_VALUE)
    message(FATAL_ERROR "QT_NAMESPACE_TEST_VALUE must be set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CantordustQt.cmake")
add_library(fake_qt_core INTERFACE)
target_compile_definitions(fake_qt_core INTERFACE
    "QT_CORE_LIB" "QT_NAMESPACE=${QT_NAMESPACE_TEST_VALUE}")
Cantordust_RequireQtNamespace(fake_qt_core)
