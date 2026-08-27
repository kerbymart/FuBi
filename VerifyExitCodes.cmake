if(NOT DEFINED FUBI OR NOT DEFINED FIXTURE)
    message(FATAL_ERROR "FUBI and FIXTURE are required")
endif()

execute_process(COMMAND "${FUBI}" RESULT_VARIABLE usage_result)
if(NOT usage_result EQUAL 2)
    message(FATAL_ERROR "expected usage exit code 2, got ${usage_result}")
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
