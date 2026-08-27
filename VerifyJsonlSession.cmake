if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(INPUT_FILE "${OUTPUT_DIR}/jsonl-session-input.txt")
file(WRITE "${INPUT_FILE}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"list\",\"correlation_id\":\"list\"}\n"
    "{\"schema_version\":1,\"action\":\"describe\",\"correlation_id\":\"describe\",\"selector\":\"NamedExport\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"release\"}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")

execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "Get-Content -LiteralPath '${INPUT_FILE}' | & '${FUBI}' '${FIXTURE}' --jsonl"
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERRORS
    RESULT_VARIABLE RESULT)
file(REMOVE "${INPUT_FILE}")

if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "JSONL session failed with ${RESULT}: ${ERRORS}")
endif()
foreach(ACTION hello list describe release quit)
    string(FIND "${OUTPUT}" "\"action\":\"${ACTION}\"" POSITION)
    if(POSITION LESS 0)
        message(FATAL_ERROR "JSONL response for ${ACTION} was not found: ${OUTPUT}")
    endif()
endforeach()
