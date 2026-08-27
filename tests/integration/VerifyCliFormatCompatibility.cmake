if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(JSON_ALIAS "${OUTPUT_DIR}/cli-format-json-alias.txt")
set(JSON_FORMAT "${OUTPUT_DIR}/cli-format-json-format.txt")
execute_process(
    COMMAND "${FUBI}" "${FIXTURE}" --list --json
    OUTPUT_FILE "${JSON_ALIAS}"
    ERROR_VARIABLE JSON_ALIAS_ERROR
    RESULT_VARIABLE JSON_ALIAS_RESULT)
execute_process(
    COMMAND "${FUBI}" "${FIXTURE}" --list --format json
    OUTPUT_FILE "${JSON_FORMAT}"
    ERROR_VARIABLE JSON_FORMAT_ERROR
    RESULT_VARIABLE JSON_FORMAT_RESULT)
if(NOT JSON_ALIAS_RESULT EQUAL 0 OR NOT JSON_FORMAT_RESULT EQUAL 0)
    message(FATAL_ERROR "JSON format compatibility failed: ${JSON_ALIAS_ERROR}${JSON_FORMAT_ERROR}")
endif()
file(READ "${JSON_ALIAS}" JSON_ALIAS_OUTPUT)
file(READ "${JSON_FORMAT}" JSON_FORMAT_OUTPUT)
if(NOT JSON_ALIAS_OUTPUT STREQUAL JSON_FORMAT_OUTPUT)
    message(FATAL_ERROR "--format json did not preserve --json output")
endif()

set(INPUT_FILE "${OUTPUT_DIR}/cli-format-session-input.txt")
set(JSONL_ALIAS "${OUTPUT_DIR}/cli-format-jsonl-alias.txt")
set(JSONL_FORMAT "${OUTPUT_DIR}/cli-format-jsonl-format.txt")
set(SESSION_FORMAT "${OUTPUT_DIR}/cli-format-session.txt")
file(WRITE "${INPUT_FILE}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--jsonl') -RedirectStandardInput '${INPUT_FILE}' -RedirectStandardOutput '${JSONL_ALIAS}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    ERROR_VARIABLE JSONL_ALIAS_ERROR
    RESULT_VARIABLE JSONL_ALIAS_RESULT)
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--format','jsonl') -RedirectStandardInput '${INPUT_FILE}' -RedirectStandardOutput '${JSONL_FORMAT}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    ERROR_VARIABLE JSONL_FORMAT_ERROR
    RESULT_VARIABLE JSONL_FORMAT_RESULT)
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--session') -RedirectStandardInput '${INPUT_FILE}' -RedirectStandardOutput '${SESSION_FORMAT}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    ERROR_VARIABLE SESSION_ERROR
    RESULT_VARIABLE SESSION_RESULT)
if(NOT JSONL_ALIAS_RESULT EQUAL 0 OR NOT JSONL_FORMAT_RESULT EQUAL 0 OR NOT SESSION_RESULT EQUAL 0)
    message(FATAL_ERROR "JSONL compatibility failed: ${JSONL_ALIAS_ERROR}${JSONL_FORMAT_ERROR}${SESSION_ERROR}")
endif()
file(READ "${JSONL_ALIAS}" JSONL_ALIAS_OUTPUT)
file(READ "${JSONL_FORMAT}" JSONL_FORMAT_OUTPUT)
file(READ "${SESSION_FORMAT}" SESSION_OUTPUT)
string(REPLACE "\r\n" "\n" JSONL_ALIAS_OUTPUT "${JSONL_ALIAS_OUTPUT}")
string(REPLACE "\r\n" "\n" JSONL_FORMAT_OUTPUT "${JSONL_FORMAT_OUTPUT}")
string(REPLACE "\r\n" "\n" SESSION_OUTPUT "${SESSION_OUTPUT}")
if(NOT JSONL_ALIAS_OUTPUT STREQUAL JSONL_FORMAT_OUTPUT)
    message(FATAL_ERROR "--format jsonl did not preserve --jsonl protocol output")
endif()
if(NOT JSONL_ALIAS_OUTPUT STREQUAL SESSION_OUTPUT)
    message(FATAL_ERROR "--session did not preserve JSONL protocol output")
endif()

file(REMOVE "${JSON_ALIAS}" "${JSON_FORMAT}" "${INPUT_FILE}" "${JSONL_ALIAS}" "${JSONL_FORMAT}" "${SESSION_FORMAT}")
