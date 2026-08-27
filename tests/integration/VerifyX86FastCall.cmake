if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()
file(SHA256 "${FIXTURE}" HASH)
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list --json
    WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE LIST_RESULT
    OUTPUT_VARIABLE LIST_OUTPUT ERROR_VARIABLE LIST_ERROR)
if(NOT LIST_RESULT EQUAL 0)
    message(FATAL_ERROR "fastcall catalog failed: ${LIST_RESULT}: ${LIST_ERROR}")
endif()
function(find_rva SELECTOR OUTPUT)
    string(REGEX MATCH "\\\"rva\\\":([0-9]+)[^}]*\\\"name\\\":\\\"${SELECTOR}\\\"" MATCH "${LIST_OUTPUT}")
    if(NOT MATCH)
        message(FATAL_ERROR "selector ${SELECTOR} was not found: ${LIST_OUTPUT}")
    endif()
    set(${OUTPUT} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
find_rva(FastCallByte BYTE_RVA)
find_rva(FastCallWord WORD_RVA)
find_rva(FastCallDword DWORD_RVA)
find_rva(FastCallRegisterCheck REGISTER_RVA)
set(PROFILE "${OUTPUT_DIR}/x86-fastcall-profile.json")
file(WRITE "${PROFILE}"
    "{\"schema_version\":1,\"module\":{\"sha256\":\"${HASH}\",\"architecture\":\"x86\"},\"functions\":["
    "{\"rva\":${BYTE_RVA},\"selector\":\"FastCallByte\",\"abi\":\"__fastcall\",\"return_type\":{\"kind\":\"integer\",\"width\":8},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1}],\"variadic\":false},"
    "{\"rva\":${WORD_RVA},\"selector\":\"FastCallWord\",\"abi\":\"__fastcall\",\"return_type\":{\"kind\":\"integer\",\"width\":16},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":16}],\"variadic\":false},"
    "{\"rva\":${DWORD_RVA},\"selector\":\"FastCallDword\",\"abi\":\"__fastcall\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32}],\"variadic\":false},"
    "{\"rva\":${REGISTER_RVA},\"selector\":\"FastCallRegisterCheck\",\"abi\":\"__fastcall\",\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32},{\"kind\":\"integer\",\"width\":32}],\"variadic\":false}]}")
function(run_call SELECTOR EXPECTED)
    set(CALL_ARGS --arg pointer:opaque:0x13572468)
    list(APPEND CALL_ARGS ${ARGN})
    execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call "${SELECTOR}"
        --prototype-override "${PROFILE}" ${CALL_ARGS} --json
        WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0 OR NOT OUTPUT MATCHES "\\\"success\\\":true" OR
       NOT OUTPUT MATCHES "\\\"return_value\\\":\\\"${EXPECTED}\\\"")
        message(FATAL_ERROR "${SELECTOR} failed: ${RESULT}: ${ERROR}: ${OUTPUT}")
    endif()
endfunction()
run_call(FastCallByte 165)
run_call(FastCallWord 23205 --arg integer:4660)
run_call(FastCallDword 7 --arg integer:9320 --arg integer:3 --arg integer:4)
# Two consecutive calls exercise callee stack cleanup on the stack-argument path.
run_call(FastCallDword 7 --arg integer:9320 --arg integer:3 --arg integer:4)
run_call(FastCallRegisterCheck 7 --arg integer:1 --arg integer:2 --arg integer:4)
# Repeat the register-preservation fixture to make the nonvolatile guarantee
# observable across independent worker invocations.
run_call(FastCallRegisterCheck 7 --arg integer:1 --arg integer:2 --arg integer:4)
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call "#1"
    --prototype-override "${PROFILE}" --arg pointer:opaque:0x13572468 --json
    WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE ORDINAL_RESULT
    OUTPUT_VARIABLE ORDINAL_OUTPUT ERROR_VARIABLE ORDINAL_ERROR)
if(NOT ORDINAL_RESULT EQUAL 0 OR NOT ORDINAL_OUTPUT MATCHES "\\\"return_value\\\":\\\"165\\\"")
    message(FATAL_ERROR "ordinal fastcall failed: ${ORDINAL_RESULT}: ${ORDINAL_ERROR}: ${ORDINAL_OUTPUT}")
endif()
file(REMOVE "${OUTPUT_DIR}/fastcall_fixture.loaded")
file(WRITE "${OUTPUT_DIR}/invalid-fastcall.jsonl"
    "{\"schema_version\":1,\"action\":\"hello\",\"correlation_id\":\"hello\"}\n"
    "{\"schema_version\":1,\"action\":\"call\",\"correlation_id\":\"missing\",\"selector\":\"FastCallWord\",\"has_prototype_override\":true,\"prototype_override\":{\"abi\":\"__fastcall\",\"quality\":\"user-declared\",\"return_type\":{\"kind\":\"integer\",\"width\":16},\"parameters\":[{\"kind\":\"pointer\",\"width\":32,\"pointer_depth\":1},{\"kind\":\"integer\",\"width\":16}]},\"arguments\":[]}\n"
    "{\"schema_version\":1,\"action\":\"quit\",\"correlation_id\":\"quit\"}\n")
set(SESSION_SCRIPT "${OUTPUT_DIR}/run-x86-fastcall-jsonl.cmd")
file(WRITE "${SESSION_SCRIPT}" "@echo off\n\"%~1\" \"%~2\" --jsonl < \"%~3\"\nexit /b %ERRORLEVEL%\n")
execute_process(COMMAND cmd /c call "${SESSION_SCRIPT}" "${FUBI}" "${FIXTURE}"
    "${OUTPUT_DIR}/invalid-fastcall.jsonl" WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE SESSION_RESULT OUTPUT_VARIABLE SESSION_OUTPUT ERROR_VARIABLE SESSION_ERROR)
if(NOT SESSION_RESULT EQUAL 0 OR NOT SESSION_OUTPUT MATCHES "argument-count-mismatch")
    message(FATAL_ERROR "invalid fastcall request failed: ${SESSION_RESULT}: ${SESSION_ERROR}: ${SESSION_OUTPUT}")
endif()
if(EXISTS "${OUTPUT_DIR}/fastcall_fixture.loaded")
    message(FATAL_ERROR "invalid fastcall request loaded the fixture")
endif()
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call FastCallWord
    --prototype-override "${PROFILE}" --arg integer:1 --arg integer:4660 --json
    WORKING_DIRECTORY "${OUTPUT_DIR}" RESULT_VARIABLE NON_POINTER_RESULT
    OUTPUT_VARIABLE NON_POINTER_OUTPUT ERROR_VARIABLE NON_POINTER_ERROR)
if(NOT NON_POINTER_RESULT EQUAL 8 OR NOT NON_POINTER_OUTPUT MATCHES "argument-type-mismatch")
    message(FATAL_ERROR "non-pointer fastcall object argument was not rejected: ${NON_POINTER_RESULT}: ${NON_POINTER_ERROR}: ${NON_POINTER_OUTPUT}")
endif()
if(EXISTS "${OUTPUT_DIR}/fastcall_fixture.loaded")
    message(FATAL_ERROR "non-pointer fastcall argument loaded the target fixture")
endif()
file(REMOVE "${PROFILE}" "${OUTPUT_DIR}/invalid-fastcall.jsonl" "${SESSION_SCRIPT}")
