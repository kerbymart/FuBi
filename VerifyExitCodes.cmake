if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE)
    message(FATAL_ERROR "FUBI and FIXTURE are required")
endif()

execute_process(COMMAND "${FUBI}" RESULT_VARIABLE usage_result)
if(NOT usage_result EQUAL 2)
    message(FATAL_ERROR "expected usage exit code 2, got ${usage_result}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list RESULT_VARIABLE success_result)
if(NOT success_result EQUAL 0)
    message(FATAL_ERROR "expected success exit code 0, got ${success_result}")
endif()

execute_process(COMMAND "${FUBI}" "missing-file.dll" RESULT_VARIABLE catalog_result)
if(NOT catalog_result EQUAL 3)
    message(FATAL_ERROR "expected catalog exit code 3, got ${catalog_result}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --describe missing
    RESULT_VARIABLE selector_result)
if(NOT selector_result EQUAL 4)
    message(FATAL_ERROR "expected selector exit code 4, got ${selector_result}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list --profile missing-profile.json
    RESULT_VARIABLE profile_result)
if(NOT profile_result EQUAL 6)
    message(FATAL_ERROR "expected profile exit code 6, got ${profile_result}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --list --symbols
    RESULT_VARIABLE symbols_result)
if(NOT symbols_result EQUAL 7)
    message(FATAL_ERROR "expected symbols exit code 7, got ${symbols_result}")
endif()

execute_process(COMMAND "${FUBI}" "${FIXTURE}" --call NamedExport
    RESULT_VARIABLE validation_result)
if(NOT validation_result EQUAL 8)
    message(FATAL_ERROR "expected validation exit code 8, got ${validation_result}")
endif()
