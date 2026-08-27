if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

file(SHA256 "${FIXTURE}" HASH)
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list --json
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE LIST_RESULT OUTPUT_VARIABLE LIST_OUTPUT ERROR_VARIABLE LIST_ERROR)
if(NOT LIST_RESULT EQUAL 0)
    message(FATAL_ERROR "x86 thiscall catalog failed: ${LIST_RESULT}: ${LIST_ERROR}")
endif()

function(find_rva SELECTOR OUTPUT)
    string(REGEX MATCH "\\\"rva\\\":([0-9]+)[^}]*\\\"name\\\":\\\"${SELECTOR}\\\"" MATCH "${LIST_OUTPUT}")
    if(NOT MATCH)
        message(FATAL_ERROR "selector ${SELECTOR} was not found in catalog: ${LIST_OUTPUT}")
    endif()
    set(${OUTPUT} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

find_rva(ThisCallByte BYTE_RVA)
find_rva(ThisCallWord WORD_RVA)
find_rva(ThisCallDword DWORD_RVA)
find_rva(ThisCallRepeated REPEATED_RVA)
find_rva(ThisCallRegisterCheck REGISTER_RVA)

set(PROFILE "${OUTPUT_DIR}/x86-thiscall-profile.json")
file(WRITE "${PROFILE}"
    "{\"schema_version\":1,\"module\":{\"sha256\":\"${HASH}\",\"architecture\":\"x86\"},\"functions\":["
    "{\"rva\":${BYTE_RVA},\"selector\":\"ThisCallByte\",\"abi\":\"__thiscall\",\"return_type\":{\"kind\":\"integer\",\"width\":8},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1}],\"variadic\":false},"
    "{\"rva\":${WORD_RVA},\"selector\":\"ThisCallWord\",\"abi\":\"__thiscall\",\"return_type\":{\"kind\":\"integer\",\"width\":16},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":16}],\"variadic\":false},"
    "{\"rva\":${DWORD_RVA},\"selector\":\"ThisCallDword\",\"abi\":\"__thiscall\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32}],\"variadic\":false},"
    "{\"rva\":${REPEATED_RVA},\"selector\":\"ThisCallRepeated\",\"abi\":\"__thiscall\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":32}],\"variadic\":false}]}")


function(run_call SELECTOR EXPECTED)
    set(call_args --arg "pointer:opaque:0x13572468")
    list(APPEND call_args ${ARGN})
    execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call "${SELECTOR}" --prototype-override "${PROFILE}" ${call_args} --json
        WORKING_DIRECTORY "${OUTPUT_DIR}"
        RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${SELECTOR} failed: ${RESULT}: ${ERROR}: ${OUTPUT}")
    endif()
    string(FIND "${OUTPUT}" "\"success\":true" SUCCESS)
    string(FIND "${OUTPUT}" "\"return_value\":\"${EXPECTED}\"" RETURN)
    if(SUCCESS LESS 0 OR RETURN LESS 0)
        message(FATAL_ERROR "${SELECTOR} returned unexpected result: ${OUTPUT}")
    endif()
endfunction()

run_call(ThisCallByte 165)
run_call(ThisCallWord 18577 --arg integer:4660)
run_call(ThisCallDword 270549281 --arg integer:1 --arg integer:2 --arg integer:3 --arg integer:4)
run_call(ThisCallRepeated 42 --arg integer:35)
run_call(ThisCallRepeated 106 --arg integer:99)

set(REGISTER_PROFILE "${OUTPUT_DIR}/x86-thiscall-register-profile.json")
file(WRITE "${REGISTER_PROFILE}"
    "{\"schema_version\":1,\"module\":{\"sha256\":\"${HASH}\",\"architecture\":\"x86\"},\"functions\":[{\"rva\":${REGISTER_RVA},\"selector\":\"ThisCallRegisterCheck\",\"abi\":\"__thiscall\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":32}],\"variadic\":false}]}")
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call ThisCallRegisterCheck --prototype-override "${REGISTER_PROFILE}"
    --arg "pointer:opaque:0x13572468" --arg integer:305419896 --json
    WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE REGISTER_RESULT OUTPUT_VARIABLE REGISTER_OUTPUT ERROR_VARIABLE REGISTER_ERROR)
if(NOT REGISTER_RESULT EQUAL 0 OR NOT REGISTER_OUTPUT MATCHES "\\\"return_value\\\":\\\"305419899\\\"")
    message(FATAL_ERROR "thiscall register preservation failed: ${REGISTER_RESULT}: ${REGISTER_ERROR}: ${REGISTER_OUTPUT}")
endif()
run_call(ThisCallRegisterCheck 305419899 --arg integer:305419896)

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call "#1" --prototype-override "${PROFILE}"
    --arg "pointer:opaque:0x13572468" --json
    WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE ORDINAL_RESULT OUTPUT_VARIABLE ORDINAL_OUTPUT ERROR_VARIABLE ORDINAL_ERROR)
if(NOT ORDINAL_RESULT EQUAL 0 OR NOT ORDINAL_OUTPUT MATCHES "\"return_value\":\"165\"")
    message(FATAL_ERROR "ordinal thiscall failed: ${ORDINAL_RESULT}: ${ORDINAL_ERROR}: ${ORDINAL_OUTPUT}")
endif()

file(WRITE "${OUTPUT_DIR}/invalid-thiscall.jsonl"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"missing\",\"selector\":\"ThisCallByte\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"__thiscall\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"integer\",\"width\":8},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":8}]},\"arguments\":[]}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"null\",\"selector\":\"ThisCallByte\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"__thiscall\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"integer\",\"width\":8},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":8}]},\"arguments\":[{\"type\":{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},\"value\":\"opaque:0\"},{\"type\":{\"kind\":\"integer\",\"width\":8},\"value\":\"7\"}]}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")
file(REMOVE "${OUTPUT_DIR}/thiscall_fixture.loaded")
set(SESSION_SCRIPT "${OUTPUT_DIR}/run-x86-thiscall-jsonl.cmd")
file(WRITE "${SESSION_SCRIPT}"
    "@echo off\n"
    "\"%~1\" \"%~2\" --jsonl < \"%~3\"\n"
    "exit /b %ERRORLEVEL%\n")
execute_process(COMMAND cmd /c call "${SESSION_SCRIPT}" "${FUBI}" "${FIXTURE}" "${OUTPUT_DIR}/invalid-thiscall.jsonl"
    WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE SESSION_RESULT OUTPUT_VARIABLE SESSION_OUTPUT ERROR_VARIABLE SESSION_ERROR)
if(NOT SESSION_RESULT EQUAL 0)
    message(FATAL_ERROR "invalid thiscall session failed: ${SESSION_RESULT}: ${SESSION_ERROR}")
endif()
string(FIND "${SESSION_OUTPUT}" "missing-object-pointer" MISSING)
string(FIND "${SESSION_OUTPUT}" "invalid-object-pointer" INVALID)
if(MISSING LESS 0 OR INVALID LESS 0)
    message(FATAL_ERROR "invalid object pointer diagnostics missing: ${SESSION_OUTPUT}")
endif()
if(EXISTS "${OUTPUT_DIR}/thiscall_fixture.loaded")
    message(FATAL_ERROR "invalid object-pointer requests loaded the target fixture")
endif()
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call ThisCallByte --prototype-override "${PROFILE}"
    --arg pointer:malformed --json WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE MALFORMED_RESULT OUTPUT_VARIABLE MALFORMED_OUTPUT ERROR_VARIABLE MALFORMED_ERROR)
if(NOT MALFORMED_RESULT EQUAL 8 OR NOT MALFORMED_OUTPUT MATCHES "invalid-object-pointer")
    message(FATAL_ERROR "malformed object pointer was not rejected: ${MALFORMED_RESULT}: ${MALFORMED_ERROR}: ${MALFORMED_OUTPUT}")
endif()
if(EXISTS "${OUTPUT_DIR}/thiscall_fixture.loaded")
    message(FATAL_ERROR "malformed object-pointer request loaded the target fixture")
endif()

file(REMOVE "${PROFILE}" "${REGISTER_PROFILE}" "${OUTPUT_DIR}/invalid-thiscall.jsonl" "${SESSION_SCRIPT}" "${OUTPUT_DIR}/thiscall_fixture.loaded")
