cmake_minimum_required(VERSION 3.20)

set(LLUTILS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(LLUtils LANGUAGES CXX)
    set(LLUTILS_TOP_LEVEL ON)
endif()

if(TARGET LLUtils)
    return()
endif()

file(GLOB_RECURSE llutils_header_files
    "./Include/*.h"
)

option(LLUTILS_BUILD_TESTS "Build LLUtils tests" ${LLUTILS_TOP_LEVEL})

option(LLUTILS_ENABLE_DEBUG_SYMBOLS "Enable LLUtils stack-symbol support" ON)

add_library(LLUtils INTERFACE)
# Public headers use std::to_underlying and string resize_and_overwrite.
target_compile_features(LLUtils INTERFACE cxx_std_23)
add_library(LLUtils::LLUtils ALIAS LLUtils)

target_include_directories(LLUtils
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_sources(LLUtils
    INTERFACE
        ${llutils_header_files}
)

if (LLUTILS_ENABLE_DEBUG_SYMBOLS)
    target_compile_definitions(LLUtils INTERFACE LLUTILS_ENABLE_DEBUG_SYMBOLS=1)

    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(LLUtils INTERFACE dbghelp.lib)
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(LLUtils INTERFACE /Zc:preprocessor)
endif()

if(LLUTILS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
