if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "FUBI, FIXTURE, and OUTPUT_DIR are required")
endif()

file(SHA256 "${FIXTURE}" FIXTURE_HASH)
set(marker "${OUTPUT_DIR}/x86_register_fixture.executed")
file(REMOVE "${marker}")
execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list --json
    RESULT_VARIABLE list_result OUTPUT_VARIABLE list_output ERROR_VARIABLE list_diagnostics)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "sentinel fixture catalog failed: ${list_diagnostics}")
endif()
if(EXISTS "${marker}")
    message(FATAL_ERROR "sentinel fixture was executed by a static operation")
endif()

function(run_sentinel selector abi label)
    string(REGEX MATCH "\\\"rva\\\":([0-9]+),[^}]*\\\"name\\\":\\\"${selector}\\\"" match "${list_output}")
    if(NOT match)
        message(FATAL_ERROR "${label} was not present in the static catalog: ${list_output}")
    endif()
    set(rva "${CMAKE_MATCH_1}")
    set(profile "${OUTPUT_DIR}/x86-${selector}.json")
    file(WRITE "${profile}"
        "{\"schema_version\":1,\"module\":{\"sha256\":\"${FIXTURE_HASH}\",\"architecture\":\"x86\"},\"functions\":["
        "{\"rva\":${rva},\"selector\":\"${selector}\",\"abi\":\"${abi}\","
        "\"return_type\":{\"kind\":\"integer\",\"width\":32},\"parameters\":[],\"variadic\":false}]}" )
    execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call "${selector}"
        --prototype-override "${profile}" --json
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} failed with exit ${result}: ${output} ${diagnostics}")
    endif()
    string(FIND "${output}" "\"success\":true" success)
    string(FIND "${output}" "\"return_value\":\"1\"" returned)
    if(success LESS 0 OR returned LESS 0)
        message(FATAL_ERROR "${label} did not report a successful sentinel result: ${output}")
    endif()
    file(REMOVE "${profile}")
endfunction()

run_sentinel(X86CdeclRegisterSentinel __cdecl "x86 cdecl register sentinel")
run_sentinel(X86StdcallRegisterSentinel __stdcall "x86 stdcall register sentinel")
