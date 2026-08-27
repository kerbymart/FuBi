if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED NOISE_FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, NOISE_FIXTURE, and OUTPUT_DIR are required")
endif()

set(INPUT_FILE "${OUTPUT_DIR}/persistent-session-input.txt")
set(OUTPUT_FILE "${OUTPUT_DIR}/persistent-session-output.txt")
file(WRITE "${INPUT_FILE}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{malformed-json}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"first\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:0x1\"}]}\n"
    "{bad-json-while-worker-is-running}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"reuse\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:session-1\"}]}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"release\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"duplicate\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"stale\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:session-1\"}]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"crash\",\"selector\":\"CrashProcess\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[]},\"arguments\":[]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"after-crash\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:0x1\"}]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"hang\",\"selector\":\"HangProcess\",\"timeout_ms\":100,\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[]},\"arguments\":[]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"after-timeout\",\"selector\":\"PointerEcho\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"parameters\":[{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":64,\"pointer_depth\":1},\"value\":\"opaque:0x1\"}]}\n"
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
string(FIND "${OUTPUT}" "\"correlation_id\":\"jsonl-error\"" MALFORMED_POSITION)
if(MALFORMED_POSITION LESS 0)
    message(FATAL_ERROR "malformed request was not recovered: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"crash\"" CRASH_POSITION)
string(FIND "${OUTPUT}" "worker-crashed" CRASH_STATUS_POSITION)
if(CRASH_POSITION LESS 0 OR CRASH_STATUS_POSITION LESS 0)
    message(FATAL_ERROR "persistent crash was not reported: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"after-crash\",\"success\":true" AFTER_CRASH_POSITION)
if(AFTER_CRASH_POSITION LESS 0)
    message(FATAL_ERROR "persistent worker did not recover after crash: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"hang\"" HANG_POSITION)
string(FIND "${OUTPUT}" "\"status\":\"timed-out\"" TIMEOUT_POSITION)
if(HANG_POSITION LESS 0 OR TIMEOUT_POSITION LESS 0)
    message(FATAL_ERROR "persistent timeout was not reported: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"after-timeout\",\"success\":true" AFTER_TIMEOUT_POSITION)
if(AFTER_TIMEOUT_POSITION LESS 0)
    message(FATAL_ERROR "persistent worker did not recover after timeout: ${OUTPUT}")
endif()
string(FIND "${OUTPUT}" "opaque:0x" PUBLIC_ADDRESS_POSITION)
if(NOT PUBLIC_ADDRESS_POSITION LESS 0)
    message(FATAL_ERROR "numeric pointer leaked into persistent session output: ${OUTPUT}")
endif()

set(NOISE_INPUT "${OUTPUT_DIR}/persistent-output-input.txt")
set(NOISE_OUTPUT "${OUTPUT_DIR}/persistent-output-output.txt")
file(WRITE "${NOISE_INPUT}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"noise-hello\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"noise-call\",\"selector\":\"EmitStdout\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[]},\"arguments\":[]}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"noise-quit\"}\n")
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${NOISE_FIXTURE}','--jsonl') -RedirectStandardInput '${NOISE_INPUT}' -RedirectStandardOutput '${NOISE_OUTPUT}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    OUTPUT_VARIABLE NOISE_PROCESS_OUTPUT ERROR_VARIABLE NOISE_ERRORS RESULT_VARIABLE NOISE_RESULT)
if(EXISTS "${NOISE_OUTPUT}")
    file(READ "${NOISE_OUTPUT}" NOISE_PROTOCOL)
endif()
file(REMOVE "${NOISE_INPUT}" "${NOISE_OUTPUT}")
if(NOT NOISE_RESULT EQUAL 0)
    message(FATAL_ERROR "persistent output isolation run failed with ${NOISE_RESULT}: ${NOISE_ERRORS}")
endif()
string(FIND "${NOISE_PROTOCOL}" "\"correlation_id\":\"noise-call\"" NOISE_CALL_POSITION)
string(FIND "${NOISE_PROTOCOL}" "target-output-must-not-reach-protocol" NOISE_LEAK_POSITION)
if(NOISE_CALL_POSITION LESS 0 OR NOT NOISE_LEAK_POSITION LESS 0)
    message(FATAL_ERROR "target stdout corrupted or leaked into JSONL protocol: ${NOISE_PROTOCOL}")
endif()
