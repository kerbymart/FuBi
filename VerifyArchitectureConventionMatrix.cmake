if(NOT DEFINED TEST OR NOT DEFINED FUBI OR NOT DEFINED ARCHITECTURE OR
   NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "TEST, FUBI, ARCHITECTURE, and OUTPUT_DIR are required")
endif()

execute_process(COMMAND "${TEST}" "--run_test=WorkerSelection*"
    RESULT_VARIABLE SELECTION_RESULT OUTPUT_VARIABLE SELECTION_OUTPUT
    ERROR_VARIABLE SELECTION_ERROR WORKING_DIRECTORY "${OUTPUT_DIR}")
if(NOT SELECTION_RESULT EQUAL 0)
    message(FATAL_ERROR "${ARCHITECTURE} worker selection matrix failed: ${SELECTION_RESULT}: ${SELECTION_ERROR}: ${SELECTION_OUTPUT}")
endif()

if(ARCHITECTURE STREQUAL "x86")
    foreach(required X86_FIXTURE THISCALL_FIXTURE FASTCALL_FIXTURE)
        if(NOT DEFINED ${required})
            message(FATAL_ERROR "${required} is required for the x86 convention matrix")
        endif()
    endforeach()
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DFUBI=${FUBI}" "-DX86_FIXTURE=${X86_FIXTURE}"
        "-DTHISCALL_FIXTURE=${THISCALL_FIXTURE}"
        "-DFASTCALL_FIXTURE=${FASTCALL_FIXTURE}"
        "-DOUTPUT_DIR=${OUTPUT_DIR}" "-P"
        "${CMAKE_CURRENT_LIST_DIR}/VerifyX86ConventionMatrix.cmake"
        WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE MATRIX_RESULT
        OUTPUT_VARIABLE MATRIX_OUTPUT ERROR_VARIABLE MATRIX_ERROR)
    if(NOT MATRIX_RESULT EQUAL 0)
        message(FATAL_ERROR "x86 convention matrix failed: ${MATRIX_RESULT}: ${MATRIX_ERROR}: ${MATRIX_OUTPUT}")
    endif()
endif()

if(ARCHITECTURE STREQUAL "x64")
    if(NOT DEFINED X64_FIXTURE)
        message(FATAL_ERROR "X64_FIXTURE is required for the x64 convention matrix")
    endif()
    file(REMOVE "${OUTPUT_DIR}/export_fixture.executed")
    execute_process(COMMAND "${FUBI}" "${X64_FIXTURE}" --list --json
        WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE LIST_RESULT
        OUTPUT_VARIABLE LIST_OUTPUT ERROR_VARIABLE LIST_ERROR)
    if(NOT LIST_RESULT EQUAL 0 OR EXISTS "${OUTPUT_DIR}/export_fixture.executed")
        message(FATAL_ERROR "x64 static catalog was not non-executing: ${LIST_RESULT}: ${LIST_ERROR}")
    endif()
    string(REGEX MATCH "\\\"rva\\\":([0-9]+)[^}]*\\\"name\\\":\\\"AliasNamedExport\\\"" MATCH "${LIST_OUTPUT}")
    if(NOT MATCH)
        message(FATAL_ERROR "NamedExport was not found in x64 catalog")
    endif()
    set(NAMED_RVA "${CMAKE_MATCH_1}")
    file(SHA256 "${X64_FIXTURE}" HASH)
    set(PROFILE "${OUTPUT_DIR}/x64-architecture-profile.json")
    file(WRITE "${PROFILE}" "{\"schema_version\":1,\"module\":{\"sha256\":\"${HASH}\",\"architecture\":\"x64\"},\"functions\":[{\"rva\":${NAMED_RVA},\"selector\":\"AliasNamedExport\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false}]}")
    execute_process(COMMAND "${FUBI}" "${X64_FIXTURE}" --call AliasNamedExport --profile "${PROFILE}" --json
        WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE CALL_RESULT
        OUTPUT_VARIABLE CALL_OUTPUT ERROR_VARIABLE CALL_ERROR)
    if(NOT CALL_RESULT EQUAL 0 OR NOT CALL_OUTPUT MATCHES "\\\"return_value\\\":\\\"42\\\"" OR NOT EXISTS "${OUTPUT_DIR}/export_fixture.executed")
        message(FATAL_ERROR "x64 fixture call matrix failed: ${CALL_RESULT}: ${CALL_ERROR}: ${CALL_OUTPUT}")
    endif()
    file(REMOVE "${PROFILE}" "${OUTPUT_DIR}/export_fixture.executed")
endif()
