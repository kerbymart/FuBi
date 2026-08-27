if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

file(SHA256 "${FIXTURE}" FIXTURE_HASH)
set(PROFILE "${OUTPUT_DIR}/worker-failure-profile.json")
file(WRITE "${PROFILE}"
    "{\"schema_version\":1,\"module\":{\"sha256\":\"${FIXTURE_HASH}\",\"architecture\":\"x64\"},\"functions\":["
    "{\"rva\":4096,\"selector\":\"NamedExport\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false},"
    "{\"rva\":4352,\"selector\":\"CrashProcess\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false},"
    "{\"rva\":4400,\"selector\":\"HangProcess\",\"abi\":\"x64\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false}]}")

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call CrashProcess
    --prototype-override "${PROFILE}" --json
    RESULT_VARIABLE CRASH_RESULT OUTPUT_VARIABLE CRASH_OUTPUT ERROR_VARIABLE CRASH_DIAGNOSTICS)
if(NOT CRASH_RESULT EQUAL 9)
    message(FATAL_ERROR "expected crash exit code 9, got ${CRASH_RESULT}: ${CRASH_DIAGNOSTICS}")
endif()
string(FIND "${CRASH_OUTPUT}" "\"correlation_id\":\"cli-call\"" CRASH_CORRELATION)
string(FIND "${CRASH_OUTPUT}" "worker-crashed" CRASH_FAILURE)
if(CRASH_CORRELATION LESS 0 OR CRASH_FAILURE LESS 0)
    message(FATAL_ERROR "crash response was not structured: ${CRASH_OUTPUT}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call NamedExport
    --prototype-override "${PROFILE}" --json
    RESULT_VARIABLE NORMAL_RESULT OUTPUT_VARIABLE NORMAL_OUTPUT ERROR_VARIABLE NORMAL_DIAGNOSTICS)
if(NOT NORMAL_RESULT EQUAL 0)
    message(FATAL_ERROR "normal call did not recover after failures: ${NORMAL_RESULT}: ${NORMAL_DIAGNOSTICS}")
endif()
string(FIND "${NORMAL_OUTPUT}" "\"correlation_id\":\"cli-call\"" NORMAL_CORRELATION)
string(FIND "${NORMAL_OUTPUT}" "\"success\":true" NORMAL_SUCCESS)
if(NORMAL_CORRELATION LESS 0 OR NORMAL_SUCCESS LESS 0)
    message(FATAL_ERROR "normal response was not structured: ${NORMAL_OUTPUT}")
endif()
file(REMOVE "${PROFILE}")
