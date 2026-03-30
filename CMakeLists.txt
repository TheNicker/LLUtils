cmake_minimum_required(VERSION 3.10)

if(TARGET LLUtils)
    return()
endif()

file(GLOB_RECURSE llutils_header_files
    "./Include/*.h"
)

option(LLUTILS_ENABLE_DEBUG_SYMBOLS "Enable LLUtils stack-symbol support" ON)

add_library(LLUtils INTERFACE)
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
