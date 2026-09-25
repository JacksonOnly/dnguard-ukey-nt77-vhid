add_executable(nt77ctl nt77ctl.cpp)
target_compile_features(nt77ctl PRIVATE cxx_std_17)
target_include_directories(nt77ctl PRIVATE ../driver)
target_link_libraries(nt77ctl PRIVATE bcrypt)

add_test(
    NAME nt77ctl_usage
    COMMAND ${CMAKE_COMMAND}
            -DNT77CTL=$<TARGET_FILE:nt77ctl>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/nt77ctl_usage_test.cmake)

if(MSVC)
    target_compile_options(nt77ctl PRIVATE /W4 /WX)
else()
    target_compile_options(nt77ctl PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
