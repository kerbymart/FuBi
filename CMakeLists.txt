#
# @file CMakeLists.txt
# @brief CMake build configuration file for this project.
#        Contains the necessary commands and settings for building the project using CMake.
# @author Kerby
# @date 2022-12-20
#
cmake_minimum_required(VERSION 3.10)
project(Fubi)

set(BOOST_ROOT "C:/local/boost_1_73_0")
set(Boost_INCLUDE_DIRS "C:/local/boost_1_73_0")
set(Boost_LIBRARY_DIRS "C:/local/boost_1_73_0/lib64-msvc-14.2")
set(Boost_USE_STATIC_LIBS ON)

message(STATUS "BOOST_ROOT: ${BOOST_ROOT}")
message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")

find_package(Boost REQUIRED)
find_package(Boost REQUIRED COMPONENTS unit_test_framework)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

set(SOURCES DbgHelpDll.cpp Fubi.cpp SysExports.cpp fubimain.cpp stdafx.cpp)
add_executable(Fubi ${SOURCES})
target_link_libraries(Fubi PRIVATE DbgHelp)

set_property(TARGET Fubi PROPERTY ENTRY_POINT "fubimain.cpp")

set(CMAKE_GENERATOR_PLATFORM x86)
set(CMAKE_SYSTEM_VERSION 6.0)

set(TEST_SOURCES TestSignatureParser.cpp)
#add_definitions(-DBOOST_TEST_DYN_LINK)
add_definitions(-DBOOST_TEST_STATIC_LINK)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
