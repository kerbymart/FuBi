#
# @file CMakeLists.txt
# @brief CMake build configuration file for this project.
#        Contains the necessary commands and settings for building the project using CMake.
# @author Kerby
# @date 2022-12-20
#
cmake_minimum_required(VERSION 3.10)
project(Fubi)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FUBI_MAX_IMAGE_BYTES 67108864 CACHE STRING
    "Maximum number of bytes FuBi accepts from one static PE image")
if(NOT FUBI_MAX_IMAGE_BYTES MATCHES "^[0-9]+$" OR FUBI_MAX_IMAGE_BYTES LESS 4096)
    message(FATAL_ERROR "FUBI_MAX_IMAGE_BYTES must be an integer of at least 4096")
endif()

set(FUBI_BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/boost_1_87_0" CACHE PATH
    "Path to local Boost headers")
if(NOT EXISTS "${FUBI_BOOST_ROOT}/boost/version.hpp")
    message(FATAL_ERROR
        "FuBi requires local Boost headers for its grandfathered signature parser. "
        "Set -DFUBI_BOOST_ROOT=<path-to-Boost-root>. FuBi never downloads dependencies.")
endif()

add_library(fubi_boost_headers INTERFACE)
target_include_directories(fubi_boost_headers INTERFACE "${FUBI_BOOST_ROOT}")

add_library(fubi_build_limits INTERFACE)
target_compile_definitions(
    fubi_build_limits INTERFACE FUBI_MAX_IMAGE_BYTES=${FUBI_MAX_IMAGE_BYTES})

set(SOURCES
    PEImage.cpp
    StaticExportCatalog.cpp
    FunctionCatalog.cpp
    fubimain.cpp
    stdafx.cpp)
add_executable(Fubi ${SOURCES})
target_link_libraries(Fubi PRIVATE bcrypt fubi_build_limits)

set(TEST_SOURCES TestSignatureParser.cpp)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE fubi_boost_headers)

add_library(export_fixture SHARED TestExportsFixture.cpp TestExportsFixture.def)
add_executable(export_tests TestExports.cpp SysExports.cpp DbgHelpDll.cpp stdafx.cpp)
target_link_libraries(export_tests PRIVATE DbgHelp fubi_boost_headers)
add_dependencies(export_tests export_fixture)

add_library(static_fixture SHARED TestStaticFixture.cpp TestStaticFixture.rc)
target_link_libraries(static_fixture PRIVATE user32 delayimp)
target_compile_options(static_fixture PRIVATE /guard:cf)
target_link_options(static_fixture PRIVATE /DELAYLOAD:user32.dll /DEBUG:FULL /guard:cf)
set_target_properties(static_fixture PROPERTIES OUTPUT_NAME "static_fixture")

add_executable(
    static_catalog_tests
    TestStaticCatalog.cpp
    StaticExportCatalog.cpp
    PEImage.cpp)
target_link_libraries(static_catalog_tests PRIVATE fubi_boost_headers fubi_build_limits)
add_dependencies(static_catalog_tests static_fixture export_fixture)

add_executable(function_catalog_tests TestFunctionCatalog.cpp FunctionCatalog.cpp StaticExportCatalog.cpp PEImage.cpp)
target_link_libraries(function_catalog_tests PRIVATE bcrypt fubi_boost_headers fubi_build_limits)
add_dependencies(function_catalog_tests export_fixture)

enable_testing()
add_test(NAME signature_parser COMMAND tests)
add_test(NAME export_enumeration COMMAND export_tests)
add_test(NAME static_catalog COMMAND static_catalog_tests)
add_test(NAME function_catalog COMMAND function_catalog_tests)
add_test(
    NAME static_cli_no_execution
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:static_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyStaticCli.cmake)

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
