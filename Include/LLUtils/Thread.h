#pragma once

#include <exception>
#include <functional>
#include <thread>
#include <utility>

namespace LLUtils
{
    // Launch a bound task in a std::thread; join or detach before destroying the handle.
    // An exception escaping the task terminates the process, not just the worker.
    // join() waits for completion; it does not rethrow the worker's exception.
    template <class Function, class... Args>
    [[nodiscard]] std::thread StartThread(Function&& function, Args&&... args)
    {
        return std::thread(
            // Init-captures deduce their types and evaluate on the launching thread.
            // Store its handler and copy/move the function and arguments into the task.
            [handler = std::get_terminate(),
             task    = std::bind_front(std::forward<Function>(function), std::forward<Args>(args)...)]() mutable
            {
#ifdef _WIN32
                // The Microsoft CRT keeps handlers per thread; install the caller's policy.
                std::set_terminate(handler);
#else
                (void) handler;  // Other supported runtimes already share a process-wide handler.
#endif
                try
                {
                    // The mutable closure lets this rvalue call move bound arguments.
                    std::move(task)();
                }
                catch (...)
                {
                    // Reaching this catch unwinds task-local objects before termination.
                    // A bare std::thread also terminates on an escaping exception, but
                    // does not guarantee that unwinding. noexcept violations can bypass us.
                    // The handler runs on this worker and can inspect current_exception().
                    std::terminate();
                }
            });
    }
}  // namespace LLUtils
