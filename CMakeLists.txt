cmake_minimum_required(VERSION 3.20)
project(nt77_vhid LANGUAGES C CXX)

enable_testing()
option(NT77_ENABLE_ASAN "Enable AddressSanitizer for portable tests" OFF)

if(NT77_ENABLE_ASAN AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

add_subdirectory(tests)

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()
