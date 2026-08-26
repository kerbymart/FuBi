#
# @file CMakeLists.txt
# @brief CMake build configuration file for this project.
#        Contains the necessary commands and settings for building the project using CMake.
# @author Kerby
# @date 2022-12-20
#
cmake_minimum_required(VERSION 3.10)
project(Fubi)

set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/boost_1_87_0" CACHE PATH "Boost source directory")
if(EXISTS "${BOOST_ROOT}/boost/version.hpp")
    set(Boost_INCLUDE_DIRS "${BOOST_ROOT}")
else()
    include(FetchContent)
    FetchContent_Declare(
        boost_headers
        URL https://archives.boost.io/release/1.87.0/source/boost_1_87_0.zip
        SOURCE_SUBDIR _headers_only
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(boost_headers)
    set(Boost_INCLUDE_DIRS "${boost_headers_SOURCE_DIR}")
endif()
message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")

include_directories(${Boost_INCLUDE_DIRS})

set(SOURCES DbgHelpDll.cpp Fubi.cpp SysExports.cpp fubimain.cpp stdafx.cpp)
add_executable(Fubi ${SOURCES})
target_link_libraries(Fubi PRIVATE DbgHelp)

set_property(TARGET Fubi PROPERTY ENTRY_POINT "fubimain.cpp")

set(CMAKE_GENERATOR_PLATFORM x86)
set(CMAKE_SYSTEM_VERSION 6.0)

set(TEST_SOURCES TestSignatureParser.cpp)
add_executable(tests ${TEST_SOURCES})

add_library(export_fixture SHARED TestExportsFixture.cpp TestExportsFixture.def)
add_executable(export_tests TestExports.cpp SysExports.cpp DbgHelpDll.cpp stdafx.cpp)
target_link_libraries(export_tests PRIVATE DbgHelp)
add_dependencies(export_tests export_fixture)

enable_testing()
add_test(NAME signature_parser COMMAND tests)
add_test(NAME export_enumeration COMMAND export_tests)

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
