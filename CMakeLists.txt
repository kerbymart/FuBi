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

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG v4.1.1
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(zydis)

include_directories(${Boost_INCLUDE_DIRS})

set(SOURCES
    AnalysisReport.cpp
    DbgHelpDll.cpp
    Fubi.cpp
    PEAnalyzer.cpp
    PEImage.cpp
    SysExports.cpp
    fubimain.cpp
    stdafx.cpp)
add_executable(Fubi ${SOURCES})
target_link_libraries(Fubi PRIVATE DbgHelp bcrypt version Zydis)

set(TEST_SOURCES TestSignatureParser.cpp)
add_executable(tests ${TEST_SOURCES})

add_library(export_fixture SHARED TestExportsFixture.cpp TestExportsFixture.def)
add_executable(export_tests TestExports.cpp SysExports.cpp DbgHelpDll.cpp stdafx.cpp)
target_link_libraries(export_tests PRIVATE DbgHelp)
add_dependencies(export_tests export_fixture)

add_library(static_fixture SHARED TestStaticFixture.cpp TestStaticFixture.rc)
target_link_libraries(static_fixture PRIVATE user32 delayimp)
target_compile_options(static_fixture PRIVATE /guard:cf)
target_link_options(static_fixture PRIVATE /DELAYLOAD:user32.dll /DEBUG:FULL /guard:cf)
set_target_properties(static_fixture PROPERTIES OUTPUT_NAME "static_fixture")

add_executable(
    static_analysis_tests
    TestStaticAnalysis.cpp
    AnalysisReport.cpp
    PEAnalyzer.cpp
    PEImage.cpp)
target_link_libraries(static_analysis_tests PRIVATE DbgHelp bcrypt version Zydis)
add_dependencies(static_analysis_tests static_fixture export_fixture)

enable_testing()
add_test(NAME signature_parser COMMAND tests)
add_test(NAME export_enumeration COMMAND export_tests)
add_test(NAME static_analysis COMMAND static_analysis_tests)
add_test(
    NAME static_cli_no_execution
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:static_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyStaticCli.cmake)

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
