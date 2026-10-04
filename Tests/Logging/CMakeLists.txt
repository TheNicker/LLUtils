# One translation unit per selectable tag, so a suite can be run or fixed in isolation.
add_executable(tests_logging
    TestLoggingCore.cpp
    TestLoggingConcurrency.cpp
    TestLoggingFlush.cpp
    TestLoggingHistory.cpp
    TestLoggingFileSink.cpp
    TestLoggingFormat.cpp
    TestLoggingException.cpp
    TestLoggingLazy.cpp
    TestLoggingProcess.cpp
    OtherTranslationUnit.cpp
)
target_link_libraries(tests_logging PRIVATE Catch2::Catch2WithMain LLUtils::LLUtils Threads::Threads)
add_executable(tests_logging_process FailureProcess.cpp)
target_link_libraries(tests_logging_process PRIVATE LLUtils::LLUtils Threads::Threads)
add_dependencies(tests_logging tests_logging_process)
add_executable(tests_logging_benchmark EXCLUDE_FROM_ALL Benchmark.cpp)
target_link_libraries(tests_logging_benchmark PRIVATE LLUtils::LLUtils Threads::Threads)
if(WIN32)
    target_compile_definitions(tests_logging PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_definitions(tests_logging_process PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_definitions(tests_logging_benchmark PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

# Exercise the public library baseline even when an embedding application uses a newer standard.
set_target_properties(tests_logging tests_logging_process tests_logging_benchmark PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)
add_test(NAME llutils.logging COMMAND tests_logging --reporter compact)
set_tests_properties(llutils.logging PROPERTIES LABELS "LLUtils;Logging")
