if(NOT DEFINED TEST OR NOT DEFINED FIXTURE OR NOT DEFINED WORKER)
    message(FATAL_ERROR "TEST, FIXTURE, and WORKER are required")
endif()
execute_process(COMMAND "${TEST}" --run_test=ArchitectureRejectionHappensBeforeTargetLoad
    RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE DIAGNOSTICS)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "x86 architecture rejection test failed: ${RESULT}: ${OUTPUT}${DIAGNOSTICS}")
endif()
execute_process(COMMAND "${TEST}" --run_test=MissingWorkerIsReportedBeforeTargetLoad
    RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE DIAGNOSTICS)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "x86 missing worker test failed: ${RESULT}: ${OUTPUT}${DIAGNOSTICS}")
endif()
