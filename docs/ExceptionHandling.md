# Exception diagnostics

[`LLUtils::Exception`](../Include/LLUtils/Exception.h) preserves an error's description, source location, system error, stack, and optional original cause. Use it to add context, notify diagnostic observers, and format a report without losing the underlying failure.

## Capabilities

- **Thread origin:** each diagnostic records the native thread ID and its main/worker classification when constructed. Formatting on another thread preserves this origin.
- **Shared snapshot:** diagnostic data is immutable. Copies and moves share it without allocating; moved-from exceptions remain readable. Only a new diagnostic node triggers notification.
- **System-error capture:** preserve an explicit `std::error_code`, or capture the current OS error before evaluating the description.
- **Contextual rethrow:** add a higher-level explanation while retaining the original exception's type and lifetime through `std::exception_ptr`.
- **Protected observation:** callbacks run synchronously on the emitting thread, outside the registry lock. Observer exceptions are contained, and recursive notification on the same thread is suppressed.
- **Bounded reports:** [`FormatException`](../Include/LLUtils/ExceptionFormatter.h) formats LLUtils causes, standard nested exceptions, system errors, and unknown exception types. Truncation is explicit and preserves valid UTF-8 boundaries.
- **Worker failure boundary:** `StartThread` inherits the caller's Windows terminate handler and unwinds task-local objects when an exception reaches its catch.

## Thread identity

Call `Exception::RegisterMainThread()` at application entry. New LLUtils exceptions capture a native OS thread ID and `ThreadRole::Main` or `ThreadRole::Worker`; worker means any other thread, including threads created outside `StartThread`. OIViewer registers before installing its terminal handlers. Library hosts must register their own main thread rather than infer it from the first exception or library call.

Reports show `Thread: 1234 (main)` or `Thread: 5678 (worker)` before the source location for every LLUtils node in the cause chain. Copies, moves, and plain rethrows retain the snapshot. A contextual `Rethrow` records the wrapper's creating thread and leaves the inner exception's origin intact, so a main-thread wrapper can retain a worker-thread cause.

Before registration, the ID is still captured and the role is `Unknown`; later registration does not rewrite old snapshots. Manually constructed `EventArgs` default to ID zero and unknown role, formatted as `Thread: unavailable (unknown)`. Native IDs may be reused after a thread exits. Capture uses the Windows thread ID or Linux `gettid`, with no extra allocation or mutex; unsupported platforms return zero. Metadata describes construction, not the thread that later throws a previously constructed exception or presents a dialog. Standard/foreign exceptions do not gain inferred origin data, and the OIV C callback structure is unchanged.

## Public API

Create or capture the error, optionally add context inside a catch, then format it at the reporting boundary. Subscribe before errors occur to observe new diagnostics. The main APIs are:

| API | Usage |
|---|---|
| `LL_EXCEPTION(code, description)` | Constructs, notifies, and throws an `Exception`. |
| `Exception::FromSystemError(code, description)` | Constructs an exception from a saved `std::error_code`; use `throw` to throw it. |
| `LL_EXCEPTION_SYSTEM_ERROR(description)` | Captures `GetLastError()` on Windows or `errno` elsewhere, then constructs and throws. |
| `Exception::Rethrow(code, description)` | Throws a new diagnostic with the active exception as its cause. Outside a catch, throws `InvalidState`. Use bare `throw;` when no extra context is needed. |
| `Exception::RegisterMainThread()` | Call from the application main thread at entry, before diagnostics or workers start. Repeating from that same thread is harmless. |
| `GetDetails()` / `what()` | Borrow the structured `EventArgs` snapshot or its UTF-8 description without allocating. |
| `Exception::OnException.Subscribe(callback)` | Receives `const Exception::EventArgs&`; retain the returned subscription. Destruction or `Unsubscribe()` removes it. |
| `FormatException(details)` / `FormatException(exception_ptr)` | Returns a UTF-8 report. An empty pointer reports that no active exception is available. |
| `LL_EXCEPTION_DONT_THROW(code, description)` | Reports in `Mode::Error` without explicitly throwing the diagnostic. |
| `LL_ERROR(code, description)` | Throws by default in `_DEBUG`; `Exception::SetThrowErrorsInDebug(false)` switches it to reporting only. Release builds report only. |
| `StartThread(function, args...)` | From [Thread.h](../Include/LLUtils/Thread.h); returns a `std::thread`. An escaping task exception terminates the process. Join or detach before destroying the handle. |

## Example

This simulates a connection failure, adds operation context, and reports both messages:

```cpp
#include <LLUtils/ExceptionFormatter.h>
#include <cstdio>
#include <stdexcept>

int main()
{
    using Exception = LLUtils::Exception;
    try
    {
        try
        {
            throw std::runtime_error("Connection refused");
        }
        catch (...)
        {
            Exception::Rethrow(Exception::ErrorCode::RuntimeError,
                               "Cannot download image");
        }
    }
    catch (...)
    {
        try
        {
            const auto report = LLUtils::FormatException(std::current_exception());
            std::fputs(report.c_str(), stderr);
        }
        catch (...)
        {
            std::fputs("Diagnostic report unavailable\n", stderr);
        }
    }
    return 1;
}
```

The report includes `Cannot download image`, followed by a `Caused by:` section containing `Connection refused`. The exit status is 1 for the simulated failure. Link [LLUtils::LLUtils](../CMakeLists.txt) for include paths, C++23, and configured symbol support. For standalone Windows builds, define `UNICODE`, `_UNICODE`, and `NOMINMAX`.

## Reasoning

### Sharing immutable diagnostics

An exception may be copied during propagation when memory is scarce. Sharing its snapshot keeps copy and move construction nonallocating. Moves leave the source readable, so accessors never need to allocate a replacement empty diagnostic.

MSVC iterator debugging can allocate inside otherwise `noexcept` string/vector constructors. Diagnostic fields are built in their final storage with potentially throwing empty constructors. Debug inputs and returned diagnostic buffers avoid those move constructors so allocation failures remain catchable; non-debug builds retain ownership transfers. Stack capture allocates its known frame count once and fills entries in place.

### Capturing the system error first

Suppose an OS call fails, then code builds a description containing a filename. That work may overwrite `GetLastError()` or `errno`. `LL_EXCEPTION_SYSTEM_ERROR` saves the error before evaluating the description, preserving the failed operation's code. `FromSystemError` instead uses a code the caller has already captured.

### Containing observer failures

Suppose observer A logs errors and observer B displays them:

1. Constructing an exception for a failed connection invokes A.
2. A encounters a logging failure and constructs and throws another LLUtils exception. Its construction does not notify observers again on that thread, preventing recursive logging.
3. The dispatcher catches A's exception and still invokes B with the original connection error. Diagnostic failure does not replace the error being reported.

### Preserving worker termination diagnostics

`StartThread` captures the launching thread's terminate handler and binds the function and arguments before starting the worker. On Windows it installs that handler in the worker because the [Microsoft CRT stores handlers per thread](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/set-terminate-crt?view=msvc-170). The task stores copies or moved values; invoking it as an rvalue allows bound move-only arguments to be consumed.

A plain `std::thread` already calls `std::terminate()` when an exception escapes its function. The explicit catch adds stack unwinding to that boundary before termination; [unwinding without a matching catch is not guaranteed](https://eel.is/c++draft/except.terminate). It is not required merely to invoke the terminate handler or make an uncaught exception available to it.

For example, suppose a worker holds a mutex through a local `std::lock_guard`:

1. The worker calls `LL_EXCEPTION`: construction notifies `Exception::OnException`, then the macro throws.
2. If the exception reaches the wrapper's catch, unwinding destroys the guard and releases the mutex first. This applies to task-local objects; the bound callable and arguments still belong to the surrounding lambda.
3. The catch calls `std::terminate()` on the worker. Its handler can report the original type and cause with `FormatException(std::current_exception())`, then must terminate the process. The default handler calls `std::abort()`.

This is a fatal-error boundary. To recover or report failure back to the launching thread, catch inside the task before the exception reaches it.

## Limitations

- **Borrowed data:** `GetDetails()`, `what()`, and observer arguments do not transfer ownership. Retain an exception copy or copy needed fields before the owning snapshot disappears.
- **Observer concurrency and lifetime:** different threads may invoke the same observer concurrently. Unsubscription does not wait for callbacks already selected; protect shared state and drain producers before destroying borrowed captures. Release subscriptions before static teardown. See [Events](Events.md).
- **Reporting can fail:** initial construction and formatting allocate. Even `LL_EXCEPTION_DONT_THROW` can throw on allocation failure. A `noexcept` reporter must catch failures and send a static fallback directly to its sink, without relying on application observers or another formatted report.
- **Optional diagnostics:** stack, symbol, and system-message enrichment are best-effort. Observer snapshot allocation failure skips notification without replacing the original error.
- **Report limits:** at most 16 KiB, 16 exception nodes, and 64 frames per node; `Mode::Error` shows at most three frames. These limits bound formatted output, not the stored description or cause chain. Invalid UTF-8 uses a placeholder; native text APIs need conversion.
- **Worker exception transport:** `join()` only waits; it does not rethrow worker exceptions. Capture an `std::exception_ptr` inside the task and rethrow it after joining, or use a future whose `get()` rethrows the stored exception.
- **Thread termination:** a `noexcept` violation can terminate before the wrapper's catch, bypassing its unwinding behavior. Some runtime termination paths may expose no original exception. Diagnostics do not make a corrupted process recoverable.

## Complexity

- **Copy/move construction and access:** O(1), sharing or reading the existing snapshot.
- **Notification:** for `n` observers, O(n) snapshot time and temporary space, plus callback work and lock contention.
- **Capture and formatting:** capture uses at most 64 frames; formatting obeys the limits above. Text allocation, OS message lookup, and symbol lookup have additional costs, so these limits are not latency guarantees.
