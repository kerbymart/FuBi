if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(marker "${OUTPUT_DIR}/static_fixture.executed")
set(text_report "${OUTPUT_DIR}/static_cli_report.txt")
set(json_report "${OUTPUT_DIR}/static_cli_report.json")
file(REMOVE "${marker}" "${text_report}" "${json_report}")

execute_process(
    COMMAND "${FUBI}" "${FIXTURE}" --dump "${text_report}" --json "${json_report}"
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Static CLI failed (${result}): ${output}${error}")
endif()
if(EXISTS "${marker}")
    message(FATAL_ERROR "Static CLI executed the target DLL")
endif()
if(NOT EXISTS "${text_report}" OR NOT EXISTS "${json_report}")
    message(FATAL_ERROR "Static CLI did not create both reports")
endif()

file(READ "${json_report}" json)
string(FIND "${json}" "NativeUSB UTF16" string_position)
if(string_position EQUAL -1)
    message(FATAL_ERROR "JSON report is missing expected static string evidence")
endif()

file(REMOVE "${text_report}" "${json_report}")
