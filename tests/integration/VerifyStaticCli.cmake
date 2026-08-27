if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(marker "${OUTPUT_DIR}/static_fixture.executed")
file(REMOVE "${marker}")

execute_process(
    COMMAND "${FUBI}" "${FIXTURE}" --list
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Static catalog failed (${result}): ${output}${error}")
endif()
if(EXISTS "${marker}")
    message(FATAL_ERROR "Static catalog executed the target DLL")
endif()
string(FIND "${output}" "export_count =" catalogPosition)
if(catalogPosition EQUAL -1)
    message(FATAL_ERROR "Static catalog did not report exports")
endif()
