if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

set(INPUT_FILE "${OUTPUT_DIR}/persistent-session-input.txt")
set(OUTPUT_FILE "${OUTPUT_DIR}/persistent-session-output.txt")
file(WRITE "${INPUT_FILE}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"first\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:0x1\"}]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"reuse\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:session-1\"}]}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"release\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"duplicate\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"stale\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:session-1\"}]}\n"
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
    message(FATAL_ERROR "persistent session failed with ${RESULT}: ${ERRORS}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"first\"" FIRST_POSITION)
string(FIND "${OUTPUT}" "\"issued_references\":[\"opaque:session-1\"]" ISSUED_POSITION)
if(FIRST_POSITION LESS 0 OR ISSUED_POSITION LESS 0)
    message(FATAL_ERROR "first pointer result was not tokenized: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"reuse\"" REUSE_POSITION)
string(FIND "${OUTPUT}" "\"issued_references\":[\"opaque:session-2\"]" REISSUED_POSITION)
if(REUSE_POSITION LESS 0 OR REISSUED_POSITION LESS 0)
    message(FATAL_ERROR "same-session pointer reuse failed: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"release\",\"success\":true,\"status\":\"released\"" RELEASE_POSITION)
if(RELEASE_POSITION LESS 0)
    message(FATAL_ERROR "reference release failed: ${OUTPUT}")
endif()
string(REGEX MATCHALL "reference-not-found" RELEASE_ERRORS "${OUTPUT}")
list(LENGTH RELEASE_ERRORS RELEASE_ERROR_COUNT)
if(NOT RELEASE_ERROR_COUNT EQUAL 2)
    message(FATAL_ERROR "stale and duplicate references were not rejected: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "opaque:0x" PUBLIC_ADDRESS_POSITION)
if(NOT PUBLIC_ADDRESS_POSITION LESS 0)
    message(FATAL_ERROR "numeric pointer leaked into persistent session output: ${OUTPUT}")
endif()
