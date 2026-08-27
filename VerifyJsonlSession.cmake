if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(INPUT_FILE "${OUTPUT_DIR}/jsonl-session-input.txt")
set(OUTPUT_FILE "${OUTPUT_DIR}/jsonl-session-output.txt")
file(WRITE "${INPUT_FILE}"
    "{malformed-json}\n"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"list\",\"correlation_id\":\"list\"}\n"
    "{\"schema_version\":1,\"action\":\"describe\",\"correlation_id\":\"describe\",\"selector\":\"NamedExport\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"release\"}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")

execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--jsonl') -RedirectStandardInput '${INPUT_FILE}' -RedirectStandardOutput '${OUTPUT_FILE}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERRORS
    RESULT_VARIABLE RESULT)
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" OUTPUT)
endif()
file(REMOVE "${INPUT_FILE}")
file(REMOVE "${OUTPUT_FILE}")

if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "JSONL session failed with ${RESULT}: ${ERRORS}")
endif()
string(REPLACE "\r\n" "\n" OUTPUT "${OUTPUT}")
string(REPLACE "\n" ";" LINES "${OUTPUT}")
set(LINE_COUNT 0)
foreach(LINE IN LISTS LINES)
    if(NOT LINE STREQUAL "")
        math(EXPR LINE_COUNT "${LINE_COUNT} + 1")
    endif()
endforeach()
if(NOT LINE_COUNT EQUAL 6)
    message(FATAL_ERROR "expected exactly six JSONL response lines, got ${LINE_COUNT}: ${OUTPUT}")
endif()
foreach(ACTION hello list describe release quit)
    string(FIND "${OUTPUT}" "\"action\":\"${ACTION}\"" POSITION)
    if(POSITION LESS 0)
        message(FATAL_ERROR "JSONL response for ${ACTION} was not found: ${OUTPUT}")
    endif()
endforeach()
string(FIND "${OUTPUT}" "\"status\":\"validation-failed\"" MALFORMED_POSITION)
if(MALFORMED_POSITION LESS 0)
    message(FATAL_ERROR "malformed JSONL input was not recovered as a structured response: ${OUTPUT}")
endif()
