if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(INPUT_FILE "${OUTPUT_DIR}/opaque-session-input.txt")
set(OUTPUT_FILE "${OUTPUT_DIR}/opaque-session-output.txt")
file(WRITE "${INPUT_FILE}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"pointer\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:0x0\"}]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"reuse\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:session-1\"}]}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"release\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"duplicate\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"malformed\",\"reference\":\"opaque:session-0\"}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--jsonl') -RedirectStandardInput '${INPUT_FILE}' -RedirectStandardOutput '${OUTPUT_FILE}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERRORS RESULT_VARIABLE RESULT)
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" OUTPUT)
endif()
file(REMOVE "${INPUT_FILE}" "${OUTPUT_FILE}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "opaque session failed with ${RESULT}: ${ERRORS}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"pointer\"" POINTER_POSITION)
if(POINTER_POSITION LESS 0)
    message(FATAL_ERROR "pointer call response was not returned: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"issued_references\":[\"opaque:session-1\"]" ISSUED_POSITION)
if(ISSUED_POSITION LESS 0)
    message(FATAL_ERROR "pointer result was not tokenized: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "opaque:0x" PUBLIC_ADDRESS_POSITION)
if(NOT PUBLIC_ADDRESS_POSITION LESS 0)
    message(FATAL_ERROR "numeric pointer leaked into JSONL output: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"release\",\"success\":true,\"status\":\"released\"" RELEASE_POSITION)
if(RELEASE_POSITION LESS 0)
    message(FATAL_ERROR "owned reference was not released: ${OUTPUT}")
endif()
string(REGEX MATCHALL "reference-not-found" RELEASE_ERRORS "${OUTPUT}")
list(LENGTH RELEASE_ERRORS RELEASE_ERROR_COUNT)
if(NOT RELEASE_ERROR_COUNT EQUAL 2)
    message(FATAL_ERROR "duplicate and malformed releases were not rejected: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "session-reference-unsupported" REUSE_POSITION)
if(REUSE_POSITION LESS 0)
    message(FATAL_ERROR "session reference was resolved into an isolated worker call: ${OUTPUT}")
endif()

set(SECOND_INPUT "${OUTPUT_DIR}/opaque-session-second-input.txt")
set(SECOND_OUTPUT "${OUTPUT_DIR}/opaque-session-second-output.txt")
file(WRITE "${SECOND_INPUT}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"second-hello\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"foreign\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"second-quit\"}\n")
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--jsonl') -RedirectStandardInput '${SECOND_INPUT}' -RedirectStandardOutput '${SECOND_OUTPUT}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    OUTPUT_VARIABLE SECOND_STDOUT ERROR_VARIABLE SECOND_ERRORS RESULT_VARIABLE SECOND_RESULT)
if(EXISTS "${SECOND_OUTPUT}")
    file(READ "${SECOND_OUTPUT}" SECOND_OUTPUT_TEXT)
endif()
file(REMOVE "${SECOND_INPUT}" "${SECOND_OUTPUT}")
if(NOT SECOND_RESULT EQUAL 0)
    message(FATAL_ERROR "isolated session failed with ${SECOND_RESULT}: ${SECOND_ERRORS}")
endif()
string(FIND "${SECOND_OUTPUT_TEXT}" "reference-not-found" FOREIGN_POSITION)
if(FOREIGN_POSITION LESS 0)
    message(FATAL_ERROR "reference leaked across sessions: ${SECOND_OUTPUT_TEXT}")
endif()
