if(NOT DEFINED NT77CTL)
    message(FATAL_ERROR "NT77CTL was not provided")
endif()

execute_process(
    COMMAND "${NT77CTL}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(NOT result EQUAL 2)
    message(FATAL_ERROR "Expected exit code 2, got ${result}")
endif()

set(output "${stdout}${stderr}")
if(NOT output MATCHES "Usage:")
    message(FATAL_ERROR "Expected Usage output, got: ${output}")
endif()

execute_process(
    COMMAND "${NT77CTL}" hardware-id invalid
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr)

if(NOT invalid_result EQUAL 1)
    message(FATAL_ERROR
            "Expected invalid hardware ID exit code 1, got ${invalid_result}")
endif()

set(invalid_output "${invalid_stdout}${invalid_stderr}")
if(NOT invalid_output MATCHES "Usage:")
    message(FATAL_ERROR
            "Expected Usage after invalid hardware ID, got: ${invalid_output}")
endif()
