# Test dependencies stay outside the library's interface and production-only builds.
# Catch2 must compile its C++17+ adapters with the same baseline as its consumers.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG de7e8630134f46e67a6b59269436f9cab94cd28e
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_subdirectory(Logging)

add_subdirectory(Utilities)

foreach(testTarget IN ITEMS tests_logging tests_utilities)
target_link_libraries(${testTarget} PRIVATE Catch2::Catch2WithMain)

    if(MINGW)
        set_target_properties(${testTarget} PROPERTIES WIN32_EXECUTABLE FALSE)
        target_link_options(${testTarget} PRIVATE "-mconsole")
        if(NOT LLUTILS_TOP_LEVEL)
            target_link_options(${testTarget} PRIVATE "-municode")
        endif()
    elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_link_options(${testTarget} PRIVATE "/SUBSYSTEM:CONSOLE")
        if(NOT LLUTILS_TOP_LEVEL)
            target_link_options(${testTarget} PRIVATE "/ENTRY:wmainCRTStartup")
        endif()
    endif()
endforeach()
