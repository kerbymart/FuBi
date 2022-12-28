#
# @file CMakeLists.txt
# @brief CMake build configuration file for this project.
#        Contains the necessary commands and settings for building the project using CMake.
# @author Kerby
# @date 2022-12-20
#
cmake_minimum_required(VERSION 3.10)
project(Fubi)

set(SOURCES DbgHelpDll.cpp Fubi.cpp SysExports.cpp fubimain.cpp stdafx.cpp)
add_executable(Fubi ${SOURCES})
target_link_libraries(Fubi PRIVATE DbgHelp)

set_property(TARGET Fubi PROPERTY ENTRY_POINT "fubimain.cpp")

include_directories("C:/local/boost_1_73_0")
link_directories(C:/local/boost_1_73_0/lib64-msvc-14.2)

set(CMAKE_GENERATOR_PLATFORM x86)
set(CMAKE_SYSTEM_VERSION 6.0)
