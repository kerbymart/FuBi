#
# @file CMakeLists.txt
# @brief CMake build configuration file for this project.
#        Contains the necessary commands and settings for building the project using CMake.
# @author Kerby
# @date 2022-12-20
#
cmake_minimum_required(VERSION 3.10)
project(Fubi)

set(FUBI_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
set(FUBI_CLI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/cli")
set(FUBI_WORKER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/worker")
set(FUBI_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows")
set(FUBI_LEGACY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy")
set(FUBI_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
set(FUBI_FIXTURE_EXPORTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/exports")
set(FUBI_FIXTURE_STATIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/static")
set(FUBI_FIXTURE_HANDLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/handles")
set(FUBI_FIXTURE_X86_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/x86")
set(FUBI_INTEGRATION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration")
include_directories(
    ${FUBI_CORE_DIR}
    ${FUBI_CLI_DIR}
    ${FUBI_PLATFORM_DIR}
    ${FUBI_LEGACY_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FUBI_MAX_IMAGE_BYTES 67108864 CACHE STRING
    "Maximum number of bytes FuBi accepts from one static PE image")
if(NOT FUBI_MAX_IMAGE_BYTES MATCHES "^[0-9]+$" OR FUBI_MAX_IMAGE_BYTES LESS 4096)
    message(FATAL_ERROR "FUBI_MAX_IMAGE_BYTES must be an integer of at least 4096")
endif()
set(FUBI_ACCEPTANCE_BINARY "" CACHE FILEPATH
    "Optional external hash-pinned PE/DLL for static acceptance testing; never committed")
set(FUBI_ACCEPTANCE_SHA256 "" CACHE STRING
    "SHA-256 pin for FUBI_ACCEPTANCE_BINARY")

set(FUBI_BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/boost_1_87_0" CACHE PATH
    "Path to local Boost headers")
set(FUBI_BOOST_INCLUDE_DIRS "")
if(EXISTS "${FUBI_BOOST_ROOT}/boost/version.hpp")
    set(FUBI_BOOST_INCLUDE_DIRS "${FUBI_BOOST_ROOT}")
else()
    find_package(Boost QUIET)
    if(Boost_FOUND)
        foreach(boost_include_dir IN LISTS Boost_INCLUDE_DIRS)
            if(EXISTS "${boost_include_dir}/boost/version.hpp")
                list(APPEND FUBI_BOOST_INCLUDE_DIRS "${boost_include_dir}")
            endif()
        endforeach()
    endif()
    if(NOT FUBI_BOOST_INCLUDE_DIRS)
        message(FATAL_ERROR
            "FuBi requires local Boost headers for its grandfathered signature parser. "
            "Set -DFUBI_BOOST_ROOT=<path-to-Boost-root> or make Boost discoverable "
            "through CMAKE_PREFIX_PATH. FuBi never downloads dependencies.")
    endif()
endif()
list(REMOVE_DUPLICATES FUBI_BOOST_INCLUDE_DIRS)
message(STATUS "Using local Boost headers from: ${FUBI_BOOST_INCLUDE_DIRS}")

add_library(fubi_boost_headers INTERFACE)
target_include_directories(fubi_boost_headers INTERFACE ${FUBI_BOOST_INCLUDE_DIRS})

add_library(fubi_build_limits INTERFACE)
target_compile_definitions(
    fubi_build_limits INTERFACE FUBI_MAX_IMAGE_BYTES=${FUBI_MAX_IMAGE_BYTES})

set(SOURCES
    ${FUBI_CORE_DIR}/PEImage.cpp
    ${FUBI_CORE_DIR}/StaticExportCatalog.cpp
    ${FUBI_CORE_DIR}/FunctionCatalog.cpp
    ${FUBI_CORE_DIR}/PrototypeProfile.cpp
    ${FUBI_CORE_DIR}/DbgHelpDll.cpp
    ${FUBI_CORE_DIR}/CallContract.cpp
    ${FUBI_CORE_DIR}/SessionReferences.cpp
    ${FUBI_CORE_DIR}/InvocationEngine.cpp
    ${FUBI_CORE_DIR}/ProcessInvocation.cpp
    ${FUBI_CORE_DIR}/WindowsPatternCatalog.cpp
    ${FUBI_CORE_DIR}/InspectionService.cpp
    ${FUBI_CLI_DIR}/fubimain.cpp
    ${FUBI_PLATFORM_DIR}/stdafx.cpp)
if(MSVC)
    enable_language(ASM_MASM)
endif()
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(NATIVE_CALL_SOURCES ${FUBI_PLATFORM_DIR}/NativeCall_x64.asm)
else()
    set(NATIVE_CALL_SOURCES)
endif()
set(FUBI_TEST_ARCHITECTURE x86)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FUBI_TEST_ARCHITECTURE x64)
endif()
add_executable(Fubi ${SOURCES})
target_sources(Fubi PRIVATE ${NATIVE_CALL_SOURCES})
target_link_libraries(Fubi PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)

add_executable(FubiWorker ${FUBI_WORKER_DIR}/InvocationWorkerMain.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/DbgHelpDll.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${FUBI_CORE_DIR}/SessionReferences.cpp ${FUBI_PLATFORM_DIR}/stdafx.cpp ${NATIVE_CALL_SOURCES})
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(FubiWorker PROPERTIES OUTPUT_NAME "FubiWorker_x86")
endif()
target_link_libraries(FubiWorker PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)

set(TEST_SOURCES ${FUBI_TEST_DIR}/TestSignatureParser.cpp)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE fubi_boost_headers)

add_library(export_fixture SHARED ${FUBI_FIXTURE_EXPORTS_DIR}/TestExportsFixture.cpp ${FUBI_FIXTURE_EXPORTS_DIR}/TestExportsFixture.def)
target_compile_options(export_fixture PRIVATE /Od /Zi)
set_target_properties(export_fixture PROPERTIES LINK_FLAGS "/DEBUG:FULL")
add_library(persistent_output_fixture SHARED ${FUBI_FIXTURE_HANDLES_DIR}/TestPersistentOutputFixture.cpp ${FUBI_FIXTURE_HANDLES_DIR}/TestPersistentOutputFixture.def)
target_compile_options(persistent_output_fixture PRIVATE /Od)
add_library(handle_fixture SHARED ${FUBI_FIXTURE_HANDLES_DIR}/TestHandleFixture.cpp ${FUBI_FIXTURE_HANDLES_DIR}/TestHandleFixture.def)
target_compile_options(handle_fixture PRIVATE /Od)
add_executable(export_tests ${FUBI_TEST_DIR}/TestExports.cpp ${FUBI_LEGACY_DIR}/SysExports.cpp ${FUBI_CORE_DIR}/DbgHelpDll.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_PLATFORM_DIR}/stdafx.cpp)
target_link_libraries(export_tests PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)
add_dependencies(export_tests export_fixture)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(x86_register_fixture SHARED
        ${FUBI_FIXTURE_X86_DIR}/X86RegisterSentinel.asm
        ${FUBI_FIXTURE_X86_DIR}/X86RegisterFixture.cpp)
    set_target_properties(x86_register_fixture PROPERTIES
        OUTPUT_NAME "x86_register_fixture")
    target_link_options(x86_register_fixture PRIVATE
        "/EXPORT:X86CdeclRegisterSentinel=X86CdeclRegisterSentinel"
        "/EXPORT:X86StdcallRegisterSentinel=X86StdcallRegisterSentinel")
endif()

set(STATIC_FIXTURE_SOURCES ${FUBI_FIXTURE_STATIC_DIR}/TestStaticFixture.cpp ${FUBI_FIXTURE_STATIC_DIR}/TestStaticFixture.rc)
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND STATIC_FIXTURE_SOURCES ${FUBI_FIXTURE_STATIC_DIR}/StaticPatternFixture.asm)
endif()
add_library(static_fixture SHARED ${STATIC_FIXTURE_SOURCES})
target_link_libraries(static_fixture PRIVATE user32 delayimp)
target_compile_options(static_fixture PRIVATE /guard:cf)
target_link_options(static_fixture PRIVATE /DELAYLOAD:user32.dll /DEBUG:FULL /guard:cf)
set_target_properties(static_fixture PROPERTIES OUTPUT_NAME "static_fixture")

add_executable(
    static_catalog_tests
    ${FUBI_TEST_DIR}/TestStaticCatalog.cpp
    ${FUBI_CORE_DIR}/StaticExportCatalog.cpp
    ${FUBI_CORE_DIR}/PEImage.cpp)
target_link_libraries(static_catalog_tests PRIVATE fubi_boost_headers fubi_build_limits)
add_dependencies(static_catalog_tests static_fixture export_fixture)

add_executable(inspection_service_tests
    ${FUBI_TEST_DIR}/TestInspectionService.cpp ${FUBI_CORE_DIR}/InspectionService.cpp ${FUBI_CORE_DIR}/WindowsPatternCatalog.cpp
    ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp)
target_link_libraries(inspection_service_tests PRIVATE fubi_boost_headers fubi_build_limits)
add_dependencies(inspection_service_tests static_fixture export_fixture)

add_executable(windows_pattern_tests ${FUBI_TEST_DIR}/TestWindowsPatternCatalog.cpp ${FUBI_CORE_DIR}/WindowsPatternCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp)
target_link_libraries(windows_pattern_tests PRIVATE fubi_boost_headers fubi_build_limits)
add_dependencies(windows_pattern_tests static_fixture)

add_executable(function_catalog_tests ${FUBI_TEST_DIR}/TestFunctionCatalog.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp)
target_link_libraries(function_catalog_tests PRIVATE bcrypt fubi_boost_headers fubi_build_limits)
add_dependencies(function_catalog_tests export_fixture)

add_executable(prototype_profile_tests ${FUBI_TEST_DIR}/TestPrototypeProfile.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${FUBI_CORE_DIR}/DbgHelpDll.cpp ${FUBI_CORE_DIR}/CallContract.cpp)
target_link_libraries(prototype_profile_tests PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)
add_dependencies(prototype_profile_tests export_fixture)

add_executable(call_contract_tests ${FUBI_TEST_DIR}/TestCallContract.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/ProcessInvocation.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/DbgHelpDll.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${NATIVE_CALL_SOURCES})
target_link_libraries(call_contract_tests PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)
add_dependencies(call_contract_tests export_fixture FubiWorker)

add_executable(invocation_adapter_tests ${FUBI_TEST_DIR}/TestInvocationAdapter.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/ProcessInvocation.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/DbgHelpDll.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${NATIVE_CALL_SOURCES})
target_link_libraries(invocation_adapter_tests PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)
add_dependencies(invocation_adapter_tests export_fixture FubiWorker)

add_executable(internal_rva_tests ${FUBI_TEST_DIR}/TestInternalRva.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${NATIVE_CALL_SOURCES})
target_link_libraries(internal_rva_tests PRIVATE bcrypt fubi_boost_headers fubi_build_limits)
add_dependencies(internal_rva_tests static_fixture)

add_executable(mixed_floating_tests ${FUBI_TEST_DIR}/TestMixedFloating.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${NATIVE_CALL_SOURCES})
target_link_libraries(mixed_floating_tests PRIVATE bcrypt fubi_boost_headers fubi_build_limits)
add_dependencies(mixed_floating_tests static_fixture)

add_executable(string_output_tests ${FUBI_TEST_DIR}/TestStringOutput.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${NATIVE_CALL_SOURCES})
target_link_libraries(string_output_tests PRIVATE bcrypt fubi_boost_headers fubi_build_limits)
add_dependencies(string_output_tests static_fixture)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(fixture_matrix_tests ${FUBI_TEST_DIR}/TestFixtureMatrix.cpp ${FUBI_CORE_DIR}/CallContract.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp ${NATIVE_CALL_SOURCES})
    target_link_libraries(fixture_matrix_tests PRIVATE bcrypt DbgHelp fubi_boost_headers fubi_build_limits)
    add_dependencies(fixture_matrix_tests export_fixture handle_fixture)
endif()

add_executable(exit_code_tests ${FUBI_TEST_DIR}/TestExitCodes.cpp)
target_link_libraries(exit_code_tests PRIVATE fubi_boost_headers)

add_executable(session_reference_tests ${FUBI_TEST_DIR}/TestSessionReferences.cpp ${FUBI_CORE_DIR}/SessionReferences.cpp)
target_link_libraries(session_reference_tests PRIVATE fubi_boost_headers)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(x86_abi_fixture SHARED ${FUBI_FIXTURE_X86_DIR}/X86AbiFixture.cpp ${FUBI_FIXTURE_X86_DIR}/X86AbiFixture.def)
    target_compile_options(x86_abi_fixture PRIVATE /Od /Zi)
    set_target_properties(x86_abi_fixture PROPERTIES
        LINK_FLAGS "/DEBUG:FULL")
    add_executable(x86_abi_tests ${FUBI_TEST_DIR}/TestX86Abi.cpp ${FUBI_CORE_DIR}/CallContract.cpp
        ${FUBI_CORE_DIR}/ProcessInvocation.cpp ${FUBI_CORE_DIR}/InvocationEngine.cpp ${FUBI_CORE_DIR}/FunctionCatalog.cpp
        ${FUBI_CORE_DIR}/PrototypeProfile.cpp ${FUBI_CORE_DIR}/StaticExportCatalog.cpp ${FUBI_CORE_DIR}/PEImage.cpp
        ${FUBI_CORE_DIR}/DbgHelpDll.cpp ${FUBI_PLATFORM_DIR}/stdafx.cpp)
    target_link_libraries(x86_abi_tests PRIVATE bcrypt DbgHelp
        fubi_boost_headers fubi_build_limits)
    add_dependencies(x86_abi_tests x86_abi_fixture FubiWorker)
endif()

enable_testing()
add_test(NAME signature_parser COMMAND tests)
add_test(NAME export_enumeration COMMAND export_tests)
add_test(NAME static_catalog COMMAND static_catalog_tests)
add_test(NAME inspection_service COMMAND inspection_service_tests)
add_test(NAME windows_patterns COMMAND windows_pattern_tests)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_test(
        NAME windows_pattern_acceptance
        COMMAND windows_pattern_tests
            --run_test=ScansControlledPeForDirectAndImportCalls)
    add_test(
        NAME windows_pattern_determinism
        COMMAND windows_pattern_tests
            --run_test=ScanIsDeterministicAndDoesNotExecuteTheFixture)
endif()
add_test(NAME function_catalog COMMAND function_catalog_tests)
add_test(NAME prototype_profile COMMAND prototype_profile_tests)
add_test(NAME call_contract COMMAND call_contract_tests)
add_test(NAME invocation_adapter COMMAND invocation_adapter_tests)
add_test(NAME internal_rva COMMAND internal_rva_tests)
add_test(NAME mixed_floating COMMAND mixed_floating_tests)
add_test(NAME string_output COMMAND string_output_tests)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_test(NAME fixture_matrix COMMAND fixture_matrix_tests)
endif()
add_test(NAME exit_code_constants COMMAND exit_code_tests)
add_test(NAME session_references COMMAND session_reference_tests)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_test(NAME x86_abi_worker_guarantees COMMAND x86_abi_tests)
    add_test(
        NAME x86_abi_worker_diagnostics
        COMMAND ${CMAKE_COMMAND}
            -DTEST=$<TARGET_FILE:x86_abi_tests>
            -DFIXTURE=$<TARGET_FILE:x86_abi_fixture>
            -DWORKER=$<TARGET_FILE:FubiWorker>
            -P ${FUBI_INTEGRATION_DIR}/VerifyX86Abi.cmake)
endif()
add_test(
    NAME static_cli_no_execution
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:static_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyStaticCli.cmake)
add_test(
    NAME static_inspection_cli_no_execution
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:static_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyStaticInspectionCli.cmake)
add_test(
    NAME jsonl_session
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:export_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyJsonlSession.cmake)
add_test(
    NAME cli_format_compatibility
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:export_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyCliFormatCompatibility.cmake)
add_test(
    NAME opaque_session_references
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:export_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyOpaqueSessionReferences.cmake)
add_test(
    NAME persistent_session_references
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:export_fixture>
        -DNOISE_FIXTURE=$<TARGET_FILE:persistent_output_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyPersistentSessionReferences.cmake)
add_test(
    NAME handle_session_references
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:handle_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyHandleSession.cmake)
add_test(
    NAME exit_code_contract
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:export_fixture>
        -P ${FUBI_INTEGRATION_DIR}/VerifyExitCodes.cmake)
add_test(
    NAME worker_failure_protocol
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:export_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -DARCHITECTURE=${FUBI_TEST_ARCHITECTURE}
        -P ${FUBI_INTEGRATION_DIR}/VerifyWorkerFailure.cmake)
add_test(
    NAME framework_managed_call_block
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DFIXTURE=$<TARGET_FILE:static_fixture>
        -DCONTROL_FIXTURE=$<TARGET_FILE:export_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -DARCHITECTURE=${FUBI_TEST_ARCHITECTURE}
        -P ${FUBI_INTEGRATION_DIR}/VerifyFrameworkManaged.cmake)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(x86_fixture SHARED ${FUBI_FIXTURE_X86_DIR}/TestX86Fixture.cpp ${FUBI_FIXTURE_X86_DIR}/TestX86Fixture.def)
    add_test(NAME x86_worker_invocation COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi> -DFIXTURE=$<TARGET_FILE:x86_fixture>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi> -DARCHITECTURE=${FUBI_TEST_ARCHITECTURE}
        -P ${FUBI_INTEGRATION_DIR}/VerifyX86Worker.cmake)
endif()
add_test(
    NAME acceptance_static_catalog
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DACCEPTANCE_FILE=${FUBI_ACCEPTANCE_BINARY}
        -DSHA256=${FUBI_ACCEPTANCE_SHA256}
        -P ${FUBI_INTEGRATION_DIR}/VerifyAcceptance.cmake)
add_test(
    NAME acceptance_t1pidd_catalog
    COMMAND ${CMAKE_COMMAND}
        -DFUBI=$<TARGET_FILE:Fubi>
        -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
        -P ${FUBI_INTEGRATION_DIR}/VerifyT1PiddAcceptance.cmake)
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_test(
        NAME x86_nonvolatile_registers
        COMMAND ${CMAKE_COMMAND}
            -DFUBI=$<TARGET_FILE:Fubi>
            -DFIXTURE=$<TARGET_FILE:x86_register_fixture>
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
            -P ${FUBI_INTEGRATION_DIR}/VerifyX86NonvolatileRegisters.cmake)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(thiscall_fixture SHARED ${FUBI_FIXTURE_X86_DIR}/ThisCallFixture.cpp ${FUBI_FIXTURE_X86_DIR}/ThisCallFixture.asm ${FUBI_FIXTURE_X86_DIR}/ThisCallFixture.def)
    target_compile_options(thiscall_fixture PRIVATE /Od /Zi)
    target_link_options(thiscall_fixture PRIVATE /DEBUG:FULL)
    add_test(
        NAME x86_thiscall_invocation
        COMMAND ${CMAKE_COMMAND}
            -DFUBI=$<TARGET_FILE:Fubi>
            -DFIXTURE=$<TARGET_FILE:thiscall_fixture>
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
            -P ${FUBI_INTEGRATION_DIR}/VerifyX86ThisCall.cmake)
    add_library(fastcall_fixture SHARED ${FUBI_FIXTURE_X86_DIR}/FastCallFixture.cpp ${FUBI_FIXTURE_X86_DIR}/FastCallFixture.asm ${FUBI_FIXTURE_X86_DIR}/FastCallFixture.def)
    target_compile_options(fastcall_fixture PRIVATE /Od /Zi)
    target_link_options(fastcall_fixture PRIVATE /DEBUG:FULL)
    add_test(
        NAME x86_fastcall_invocation
        COMMAND ${CMAKE_COMMAND}
            -DFUBI=$<TARGET_FILE:Fubi>
            -DFIXTURE=$<TARGET_FILE:fastcall_fixture>
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
            -P ${FUBI_INTEGRATION_DIR}/VerifyX86FastCall.cmake)
    add_test(
        NAME x86_convention_matrix
        COMMAND ${CMAKE_COMMAND}
            -DFUBI=$<TARGET_FILE:Fubi>
            -DTEST=$<TARGET_FILE:x86_abi_tests>
            -DX86_FIXTURE=$<TARGET_FILE:x86_fixture>
            -DTHISCALL_FIXTURE=$<TARGET_FILE:thiscall_fixture>
            -DFASTCALL_FIXTURE=$<TARGET_FILE:fastcall_fixture>
            -DWORKER=$<TARGET_FILE:FubiWorker>
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
            -P ${FUBI_INTEGRATION_DIR}/VerifyX86ConventionMatrix.cmake)
    add_test(
        NAME architecture_convention_matrix
        COMMAND ${CMAKE_COMMAND}
            -DTEST=$<TARGET_FILE:call_contract_tests>
            -DFUBI=$<TARGET_FILE:Fubi>
            -DX86_FIXTURE=$<TARGET_FILE:x86_fixture>
            -DTHISCALL_FIXTURE=$<TARGET_FILE:thiscall_fixture>
            -DFASTCALL_FIXTURE=$<TARGET_FILE:fastcall_fixture>
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
            -DARCHITECTURE=x86
            -P ${FUBI_INTEGRATION_DIR}/VerifyArchitectureConventionMatrix.cmake)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_test(
        NAME architecture_convention_matrix
        COMMAND ${CMAKE_COMMAND}
            -DTEST=$<TARGET_FILE:call_contract_tests>
            -DFUBI=$<TARGET_FILE:Fubi>
            -DX64_FIXTURE=$<TARGET_FILE:export_fixture>
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:Fubi>
            -DARCHITECTURE=x64
            -P ${FUBI_INTEGRATION_DIR}/VerifyArchitectureConventionMatrix.cmake)
endif()

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
