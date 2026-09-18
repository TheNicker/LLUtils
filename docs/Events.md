# Events and subscriptions

[`LLUtils::Event<Signature>`](../Include/LLUtils/Event.h) broadcasts notifications to registered callbacks. Publishers raise events; listeners retain a `Subscription` for as long as they want notifications.

## Capabilities

- **Synchronous delivery:** callbacks run on the raising thread, in registration order.
- **Owned registration:** each subscription identifies one registration and automatically unsubscribes on destruction.
- **Default mode:** `Event<Signature>` uses direct callback storage without locks or snapshots for serialized publishers.
- **Thread-safe mode:** `Event<Signature, true>` protects registrations and takes shared snapshots. Callbacks run outside the lock, allowing nested raises and registration changes.
- **Conditional delivery:** `RaiseWhile` rechecks application state between listeners, including after recursive calls, so delivery can stop when that state changes.

## Public API

Create the event, retain subscriptions, raise notifications, then release subscriptions before destroying the event.

| API | Usage |
|---|---|
| `Subscribe(callback)` | Returns `Event::Subscription`. Store it; discarding it immediately unregisters the callback. Repeated subscriptions are independent. |
| `Raise(args...)` | Notifies listeners; ignores callback return values. |
| `RaiseWhile(predicate, args...)` | Continues while the zero-argument predicate returns true. |
| `subscription.Unsubscribe()` | Removes the registration early. Safe to repeat. |
| `Subscription` | Move-only; moving empties the source. Move assignment first removes the destination's old registration. Default construction creates an empty handle. |
| `if (subscription)` | Tests whether the handle owns a registration. |

## Example

```cpp
#include <LLUtils/Event.h>
#include <iostream>

int main()
{
    int total = 0;
    LLUtils::Event<void(int)> progress;

    {
        auto subscription = progress.Subscribe(
            [&total](int amount) { total += amount; });

        progress.Raise(3);
        progress.Raise(4);
        std::cout << total << '\n'; // Prints 7.
    } // Subscription destruction unregisters the callback.

    progress.Raise(5);              // No listeners remain.
    std::cout << total << '\n';     // Still prints 7.
}
```

Link the [CMake target](../CMakeLists.txt) `LLUtils::LLUtils` for include paths and C++23 settings.

## Reasoning

### Owning registrations

A listener can stop observing while the publisher remains alive. A move-only subscription ties removal to the listener's scope, including exception unwinding, and identifies one registration without comparing callbacks. Both event modes share the handle implementation while supplying their own registration tokens and removal policy.

### Choosing the event mode

A GUI publisher already serialized by its owner needs no registry lock or snapshot allocation. Default mode avoids those costs. When access overlaps, thread-safe mode takes a snapshot under the lock, then releases it before invoking callbacks. Selected callables remain alive while application code runs or changes registrations.

### Rechecking state after recursive calls

Suppose listeners A and B both need a live server connection. Each raise uses `RaiseWhile([&] { return serverAvailable; })`, reading the same state:

1. With the server available, the outer raise invokes A. A triggers a nested raise of the same event.
2. The nested raise invokes A again. This time A detects a connection failure, sets `serverAvailable = false`, and returns.
3. The nested raise rechecks the predicate and skips B. When the outer invocation of A returns, the outer raise also rechecks and skips B.

Checking availability only before the outer raise would miss the failure. `RaiseWhile` stops subsequent callbacks; it does not interrupt A or detect the outage itself.

### Customizing notification policy

Derived thread-safe notifications can use protected `VisitListeners` to customize delivery policy. [Exception diagnostics](ExceptionHandling.md) use it to contain observer failures and suppress recursive notification.

## Limitations

- **Lifetimes:** keep the event stationary and alive until subscriptions and active calls finish; handles store an owner pointer. Captured pointers and references need their own lifetime protection.
- **Inside a default-mode callback:** recursive raises and unsubscription are supported. Do not subscribe new listeners to the same event: growing its callback vector can move the callback still executing.
- **During default-mode cleanup:** removing a callback may destroy an object it owns through a capture, such as a `shared_ptr`. That object's destructor must not raise the same event or change its subscriptions while the callback vector is being cleaned up or destroyed.
- **Concurrent callbacks:** thread-safe mode protects the registry, not listener state. Synchronize shared state and access to the same subscription handle.
- **No draining:** thread-safe unsubscription affects future snapshots; already selected callbacks may still run. Own their captured state or stop and drain producers before destroying borrowed objects.
- **No scheduling:** events do not queue work or switch threads. `RaiseWhile` cannot interrupt a running callback; the application controls recursion and shutdown.
- **Failures propagate:** a callback or predicate exception stops dispatch. Registration and snapshot allocation can also throw.
- **Callable and argument constraints:** `Event::Func` is `std::function<Signature>`, requiring copyable callable targets. Default `Raise` copies ordinary lvalue arguments; `RaiseWhile` and thread-safe `Raise` can borrow them. Callbacks receive lvalues, so move-only payloads need references or shared ownership. The copying difference's rationale remains undocumented.

## Complexity

For `n` stored listeners, excluding callback work, argument copying, allocation cost, and lock contention:

- **Subscribe:** amortized O(1) vector insertion; O(n) when storage grows.
- **Unsubscribe:** O(n) search and removal or marking. Default mode delays compaction until the outermost dispatch ends.
- **Raise:** O(n) traversal. Default mode uses O(1) auxiliary dispatch space; thread-safe mode copies an O(n) snapshot for each raise, even if the predicate stops delivery immediately.
