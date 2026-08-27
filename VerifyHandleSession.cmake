if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()
set(INPUT_FILE "${OUTPUT_DIR}/handle-session-input.txt")
set(OUTPUT_FILE "${OUTPUT_DIR}/handle-session-output.txt")
file(WRITE "${INPUT_FILE}"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"create\",\"selector\":\"CreateOwnedHandle\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"x64\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"handle\",\"width\":64,\"ownership\":\"owned\",\"release_adapter\":\"CloseHandle\"},\"parameters\":[]},\"arguments\":[]}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"release\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"release\",\"correlation_id\":\"duplicate\",\"reference\":\"opaque:session-1\"}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "$process = Start-Process -FilePath '${FUBI}' -ArgumentList @('${FIXTURE}','--jsonl') -RedirectStandardInput '${INPUT_FILE}' -RedirectStandardOutput '${OUTPUT_FILE}' -NoNewWindow -PassThru -Wait; exit $process.ExitCode"
    RESULT_VARIABLE RESULT ERROR_VARIABLE ERRORS)
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" OUTPUT)
endif()
file(REMOVE "${INPUT_FILE}" "${OUTPUT_FILE}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "handle session failed with ${RESULT}: ${ERRORS}")
endif()
string(FIND "${OUTPUT}" "\"correlation_id\":\"create\"" CREATE_POSITION)
string(FIND "${OUTPUT}" "\"issued_references\":[\"opaque:session-1\"]" TOKEN_POSITION)
string(FIND "${OUTPUT}" "\"correlation_id\":\"release\",\"success\":true,\"status\":\"released\"" RELEASE_POSITION)
string(FIND "${OUTPUT}" "reference-not-found" DUPLICATE_POSITION)
string(FIND "${OUTPUT}" "opaque:0x" ADDRESS_POSITION)
if(CREATE_POSITION LESS 0 OR TOKEN_POSITION LESS 0 OR RELEASE_POSITION LESS 0 OR DUPLICATE_POSITION LESS 0 OR NOT ADDRESS_POSITION LESS 0)
    message(FATAL_ERROR "handle issuance or release contract failed: ${OUTPUT}")
endif()
