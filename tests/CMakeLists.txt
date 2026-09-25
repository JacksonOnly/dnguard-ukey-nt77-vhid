add_executable(nt77_protocol_tests
    protocol_tests.c
    ../src/protocol/nt77_protocol.c)
target_include_directories(nt77_protocol_tests PRIVATE ../src/protocol)
target_compile_features(nt77_protocol_tests PRIVATE c_std_11)

if(MSVC)
    target_compile_options(nt77_protocol_tests PRIVATE /W4 /WX)
else()
    target_compile_options(nt77_protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME nt77_protocol_tests COMMAND nt77_protocol_tests)

add_executable(nt77_protocol_tests_fixed
    protocol_fixed_tests.c
    ../src/protocol/nt77_protocol.c)
target_include_directories(nt77_protocol_tests_fixed PRIVATE
    ../src/protocol ../driver)
target_compile_definitions(nt77_protocol_tests_fixed PRIVATE
    NT77VHID_FIXED_CONFIG=1)
target_compile_features(nt77_protocol_tests_fixed PRIVATE c_std_11)

if(MSVC)
    target_compile_options(nt77_protocol_tests_fixed PRIVATE /W4 /WX)
else()
    target_compile_options(nt77_protocol_tests_fixed PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME nt77_protocol_tests_fixed COMMAND nt77_protocol_tests_fixed)

if(NT77_ENABLE_ASAN AND WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" --print-resource-dir
        OUTPUT_VARIABLE NT77_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    find_file(
        NT77_ASAN_RUNTIME
        NAMES clang_rt.asan_dynamic-x86_64.dll
        PATHS "${NT77_CLANG_RESOURCE_DIR}/lib/windows"
        NO_DEFAULT_PATH
        REQUIRED)
    add_custom_command(
        TARGET nt77_protocol_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${NT77_ASAN_RUNTIME}"
                "$<TARGET_FILE_DIR:nt77_protocol_tests>"
        VERBATIM)
endif()
