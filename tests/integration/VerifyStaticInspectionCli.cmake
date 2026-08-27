if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(MARKER "${OUTPUT_DIR}/static_fixture.executed")

function(run_static_case LABEL EXPECTED_TEXT)
    file(REMOVE "${MARKER}")
    execute_process(
        COMMAND "${FUBI}" "${FIXTURE}" ${ARGN}
        WORKING_DIRECTORY "${OUTPUT_DIR}"
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT
        ERROR_VARIABLE ERRORS)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${LABEL} failed (${RESULT}): ${OUTPUT}${ERRORS}")
    endif()
    if(EXISTS "${MARKER}")
        message(FATAL_ERROR "${LABEL} executed the target DLL")
    endif()
    string(FIND "${OUTPUT}" "${EXPECTED_TEXT}" POSITION)
    if(POSITION LESS 0)
        message(FATAL_ERROR "${LABEL} did not report '${EXPECTED_TEXT}': ${OUTPUT}")
    endif()
endfunction()

run_static_case("list" "export_count =" --list)
run_static_case("list-json" "\"schema_version\":1" --list --json)
run_static_case("describe" "FuBi function description" --describe FxDriverEntryUm)
run_static_case("describe-json" "\"function\"" --describe FxDriverEntryUm --json)

run_static_case("inspect-exports" "mode = exports" --inspect exports)
run_static_case("inspect-exports-json" "\"mode\":\"exports\"" --inspect exports --json)
run_static_case("inspect-imports" "mode = imports" --inspect imports)
run_static_case("inspect-imports-json" "\"mode\":\"imports\"" --inspect imports --json)
run_static_case("inspect-runtime-functions" "mode = runtime-functions" --inspect runtime-functions)
run_static_case("inspect-runtime-functions-json" "\"mode\":\"runtime-functions\"" --inspect runtime-functions --json)
run_static_case("inspect-debug" "mode = debug" --inspect debug)
run_static_case("inspect-debug-json" "\"mode\":\"debug\"" --inspect debug --json)
run_static_case("inspect-wdf-bind" "mode = wdf-bind" --inspect wdf-bind)
run_static_case("inspect-wdf-bind-json" "\"mode\":\"wdf-bind\"" --inspect wdf-bind --json)

file(REMOVE "${MARKER}")
