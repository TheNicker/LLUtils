# Exception diagnostics

[`LLUtils::Exception`](../Include/LLUtils/Exception.h) preserves an error's description, source location, system error, stack, and optional original cause. Use it to add context and notify diagnostic observers without losing the underlying failure.

## Capabilities

- **Shared snapshot:** diagnostic data is immutable. Copies and moves share it without allocating; moved-from exceptions remain readable. Only a new diagnostic node triggers notification.
- **System-error capture:** preserve an explicit `std::error_code`, or capture the current OS error before evaluating the description.
- **Contextual rethrow:** add a higher-level explanation while retaining the original exception's type and lifetime through `std::exception_ptr`.
- **Protected observation:** callbacks run synchronously on the emitting thread, outside the registry lock. Observer exceptions are contained, and recursive notification on the same thread is suppressed.

## Public API

Construct or capture an error, optionally add context inside a catch, and inspect its details. Subscribe before errors occur to observe new diagnostics.

| API | Usage |
|---|---|
| `LL_EXCEPTION(code, description)` | Constructs, notifies, and throws an `Exception`. |
| `Exception::FromSystemError(code, description)` | Constructs an exception from a saved `std::error_code`; use `throw` to throw it. |
| `LL_EXCEPTION_SYSTEM_ERROR(description)` | Captures `GetLastError()` on Windows or `errno` elsewhere, then constructs and throws. |
| `Exception::Rethrow(code, description)` | Throws a new diagnostic with the active exception as its cause. Outside a catch, throws `InvalidState`. Use bare `throw;` when no extra context is needed. |
| `GetDetails()` / `what()` | Borrow the structured `EventArgs` snapshot or its UTF-8 description without allocating. |
| `Exception::OnException.Subscribe(callback)` | Receives `const Exception::EventArgs&`; retain the returned subscription. Destruction or `Unsubscribe()` removes it. |
| `LL_EXCEPTION_DONT_THROW(code, description)` | Reports in `Mode::Error` without explicitly throwing the diagnostic. |
| `LL_ERROR(code, description)` | Throws by default in `_DEBUG`; `Exception::SetThrowErrorsInDebug(false)` switches it to reporting only. Release builds report only. |

## Reasoning

### Sharing immutable diagnostics

An exception may be copied during propagation when memory is scarce. Sharing its snapshot keeps copy and move construction nonallocating. Moves leave the source readable, so accessors never need to allocate a replacement empty diagnostic.

### Capturing the system error first

Suppose an OS call fails, then code builds a description containing a filename. That work may overwrite `GetLastError()` or `errno`. `LL_EXCEPTION_SYSTEM_ERROR` saves the error before evaluating the description, preserving the failed operation's code. `FromSystemError` instead uses a code the caller has already captured.

### Containing observer failures

Suppose observer A logs errors and observer B displays them:

1. Constructing an exception for a failed connection invokes A.
2. A encounters a logging failure and constructs and throws another LLUtils exception. Its construction does not notify observers again on that thread, preventing recursive logging.
3. The dispatcher catches A's exception and still invokes B with the original connection error. Diagnostic failure does not replace the error being reported.

## Limitations

- **Borrowed data:** `GetDetails()`, `what()`, and observer arguments do not transfer ownership. Retain an exception copy or copy needed fields before the owning snapshot disappears.
- **Observer concurrency and lifetime:** different threads may invoke the same observer concurrently. Unsubscription does not wait for callbacks already selected; protect shared state and drain producers before destroying borrowed captures. Release subscriptions before static teardown. See [Events](Events.md).
- **Optional diagnostics:** stack, symbol, and system-message enrichment are best-effort. Observer snapshot allocation failure skips notification without replacing the original error.
- **Reporting can fail:** initial construction allocates. Even `LL_EXCEPTION_DONT_THROW` can throw on allocation failure; contain the complete reporting operation at a `noexcept` boundary.

## Complexity

- **Copy/move construction and access:** O(1), sharing or reading the existing snapshot.
- **Notification:** for `n` observers, O(n) snapshot time and temporary space, plus callback work and lock contention.
- **Capture:** at most 64 frames; text allocation, OS messages, and symbol lookup add costs.
