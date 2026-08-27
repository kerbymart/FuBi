if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()
if(NOT DEFINED ARCHITECTURE OR NOT ARCHITECTURE STREQUAL "x64")
    message(STATUS "Worker failure protocol skipped: controlled RVA profile is x64-only")
    return()
endif()

file(SHA256 "${FIXTURE}" FIXTURE_HASH)
set(PROFILE "${OUTPUT_DIR}/worker-failure-profile.json")
file(WRITE "${PROFILE}"
    "{\"schema_version\":1,\"module\":{\"sha256\":\"${FIXTURE_HASH}\",\"architecture\":\"${ARCHITECTURE}\"},\"functions\":["
    "{\"rva\":4096,\"selector\":\"NamedExport\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false},"
    "{\"rva\":4352,\"selector\":\"CrashProcess\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false},"
    "{\"rva\":4400,\"selector\":\"HangProcess\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false}]}")

function(assert_one_json_line value label)
    string(REPLACE "\r\n" "\n" normalized "${value}")
    string(REPLACE "\n" ";" lines "${normalized}")
    set(count 0)
    foreach(line IN LISTS lines)
        if(NOT line STREQUAL "")
            math(EXPR count "${count} + 1")
        endif()
    endforeach()
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "${label} did not produce exactly one JSON response line: ${value}")
    endif()
    string(FIND "${normalized}" "{\"schema_version\":" schema)
    if(schema LESS 0)
        message(FATAL_ERROR "${label} response is not JSON: ${value}")
    endif()
endfunction()

file(REMOVE "export_fixture.executed")
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list --json
    RESULT_VARIABLE LIST_RESULT OUTPUT_VARIABLE LIST_OUTPUT ERROR_VARIABLE LIST_DIAGNOSTICS)
if(NOT LIST_RESULT EQUAL 0)
    message(FATAL_ERROR "static list failed: ${LIST_RESULT}: ${LIST_DIAGNOSTICS}")
endif()
assert_one_json_line("${LIST_OUTPUT}" "static list")
if(EXISTS "export_fixture.executed")
    message(FATAL_ERROR "static list executed the fixture")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call CrashProcess
    --prototype-override "${PROFILE}" --json
    RESULT_VARIABLE CRASH_RESULT OUTPUT_VARIABLE CRASH_OUTPUT ERROR_VARIABLE CRASH_DIAGNOSTICS)
if(NOT CRASH_RESULT EQUAL 9)
    message(FATAL_ERROR "expected crash exit code 9, got ${CRASH_RESULT}: ${CRASH_DIAGNOSTICS}")
endif()
string(FIND "${CRASH_OUTPUT}" "\"correlation_id\":\"cli-call\"" CRASH_CORRELATION)
string(FIND "${CRASH_OUTPUT}" "worker-crashed" CRASH_FAILURE)
string(FIND "${CRASH_OUTPUT}" "\"worker_exit_code\":" CRASH_EXIT)
assert_one_json_line("${CRASH_OUTPUT}" "crash")
if(CRASH_CORRELATION LESS 0 OR CRASH_FAILURE LESS 0 OR CRASH_EXIT LESS 0)
    message(FATAL_ERROR "crash response was not structured: ${CRASH_OUTPUT}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call NamedExport
    --prototype-override "${PROFILE}" --json
    RESULT_VARIABLE RECOVER_CRASH_RESULT OUTPUT_VARIABLE RECOVER_CRASH_OUTPUT
    ERROR_VARIABLE RECOVER_CRASH_DIAGNOSTICS)
if(NOT RECOVER_CRASH_RESULT EQUAL 0)
    message(FATAL_ERROR "normal call did not recover after crash: ${RECOVER_CRASH_RESULT}: ${RECOVER_CRASH_DIAGNOSTICS}")
endif()
assert_one_json_line("${RECOVER_CRASH_OUTPUT}" "recovery after crash")

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call HangProcess
    --prototype-override "${PROFILE}" --json --timeout 200
    TIMEOUT 10 RESULT_VARIABLE HANG_RESULT OUTPUT_VARIABLE HANG_OUTPUT
    ERROR_VARIABLE HANG_DIAGNOSTICS)
if(NOT HANG_RESULT EQUAL 9)
    message(FATAL_ERROR "expected timeout exit code 9, got ${HANG_RESULT}: ${HANG_DIAGNOSTICS}")
endif()
string(FIND "${HANG_OUTPUT}" "\"correlation_id\":\"cli-call\"" HANG_CORRELATION)
string(FIND "${HANG_OUTPUT}" "timed-out" HANG_FAILURE)
if(HANG_CORRELATION LESS 0 OR HANG_FAILURE LESS 0)
    message(FATAL_ERROR "timeout response was not structured: ${HANG_OUTPUT}")
endif()
assert_one_json_line("${HANG_OUTPUT}" "timeout")

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call NamedExport
    --prototype-override "${PROFILE}" --json
    RESULT_VARIABLE NORMAL_RESULT OUTPUT_VARIABLE NORMAL_OUTPUT ERROR_VARIABLE NORMAL_DIAGNOSTICS)
if(NOT NORMAL_RESULT EQUAL 0)
    message(FATAL_ERROR "normal call did not recover after failures: ${NORMAL_RESULT}: ${NORMAL_DIAGNOSTICS}")
endif()
assert_one_json_line("${NORMAL_OUTPUT}" "recovery after timeout")
string(FIND "${NORMAL_OUTPUT}" "\"correlation_id\":\"cli-call\"" NORMAL_CORRELATION)
string(FIND "${NORMAL_OUTPUT}" "\"success\":true" NORMAL_SUCCESS)
if(NORMAL_CORRELATION LESS 0 OR NORMAL_SUCCESS LESS 0)
    message(FATAL_ERROR "normal response was not structured: ${NORMAL_OUTPUT}")
endif()
file(REMOVE "${PROFILE}")
