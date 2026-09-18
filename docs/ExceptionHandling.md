# Exception diagnostics

`Exception` reports errors synchronously to registered observers. Stack capture remains bounded, with optional symbol enrichment.

## Capabilities

- **Protected observation:** callbacks run synchronously on the emitting thread, outside the registry lock. Observer exceptions are contained, and recursive notification on the same thread is suppressed.

## Public API

Retain a subscription while observing new errors, and release it before application teardown.

| API | Usage |
|---|---|
| `LL_EXCEPTION(code, description)` | Constructs, notifies, and throws an `Exception`. |
| `Exception::OnException.Subscribe(callback)` | Receives `const Exception::EventArgs&`; retain the returned subscription. Destruction or `Unsubscribe()` removes it. |
| `LL_ERROR(code, description)` | Throws by default in `_DEBUG`; `Exception::SetThrowErrorsInDebug(false)` switches it to reporting only. Release builds report only. |

## Reasoning

### Containing observer failures

Suppose observer A logs errors and observer B displays them:

1. Constructing an exception for a failed connection invokes A.
2. A encounters a logging failure and constructs and throws another LLUtils exception. Its construction does not notify observers again on that thread, preventing recursive logging.
3. The dispatcher catches A's exception and still invokes B with the original connection error. Diagnostic failure does not replace the error being reported.

## Limitations

- **Observer concurrency and lifetime:** different threads may invoke the same observer concurrently. Unsubscription does not wait for callbacks already selected; protect shared state and drain producers before destroying borrowed captures. Release subscriptions before static teardown. See [Events](Events.md).
- **Reporting can fail:** observer and snapshot failures are contained, but exception construction and stack capture can still allocate and throw.

## Complexity

- **Notification:** for `n` observers, O(n) snapshot time and temporary space, plus callback work and lock contention.
- **Capture:** at most 64 frames; allocation and optional symbol lookup add platform-dependent costs.
