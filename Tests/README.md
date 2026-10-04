# LLUtils tests

The new logging suite and tests for the extracted queue, file, lock and limiter utilities are contained here. Existing event, string, exception, and application-integration tests remain in their current upstream locations for this first migration stage.

The logging suite is one translation unit per selectable tag (`TestLoggingCore`, `TestLoggingConcurrency`, `TestLoggingFlush`, `TestLoggingHistory`, `TestLoggingFileSink`, `TestLoggingFormat`, `TestLoggingException`, `TestLoggingLazy`, `TestLoggingProcess`) linked into the single `tests_logging` executable. Temporary directories and the capture sink live in `Support/`, so both suites share one copy.

## Standalone

From an LLUtils checkout, with a C++23-capable compiler:

```sh
cmake -S . -B build/debug -DLLUTILS_BUILD_TESTS=ON
cmake --build build/debug --target tests_logging tests_utilities
ctest --test-dir build/debug -L LLUtils --output-on-failure
```

The subprocess helper builds automatically and resides beside the test executable. The suite has no OIViewer source, fixture, or sibling-directory dependency. If the parent has not supplied `Catch2::Catch2WithMain`, CMake fetches Catch2 at commit `de7e8630134f46e67a6b59269436f9cab94cd28e`. This dependency is used only by tests.

The development benchmark is optional:

```sh
cmake --build build/debug --target tests_logging_benchmark
```

Configure with `-DLLUTILS_BUILD_TESTS=OFF` for a library-only build without test targets or a Catch2 download. Tests default on when LLUtils is the top-level project and off when it is embedded.

## OIViewer

OIViewer enables this suite when `OIV_BUILD_TESTS` is on and supplies its existing Catch2 target. LLUtils owns the executable definitions and CTest registration; upstream source lists do not duplicate them.

```sh
cmake --build build/windows-clang-debug --target tests_logging tests_utilities
ctest --test-dir build/windows-clang-debug -L LLUtils --output-on-failure
```

Use the selected Linux build directory for Linux. Targets are `tests_logging`, `tests_logging_process`, `tests_utilities`, and optional `tests_logging_benchmark`. The `LLUtils` CTest label selects both suites; `Logging` and `Utilities` select their respective suites. The utility suite reuses the subprocess helper for cross-process locking. Benchmark execution is not part of normal builds or CTest runs.
