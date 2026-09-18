#pragma once

#include <exception>
#include <functional>
#include <thread>
#include <utility>

namespace LLUtils
{
    // The Microsoft CRT keeps terminate handlers per thread. Inherit the launching
    // thread's handler and enter it from a catch so current_exception retains the cause.
    // The returned thread has ordinary std::thread ownership and joining requirements.
    template <class Function, class... Args>
    [[nodiscard]] std::thread StartThread(Function&& function, Args&&... args)
    {
        return std::thread(
            [handler = std::get_terminate(),
             task    = std::bind_front(std::forward<Function>(function), std::forward<Args>(args)...)]() mutable
            {
#ifdef _WIN32
                std::set_terminate(handler);
#else
                (void) handler;  // Other supported runtimes already share a process-wide handler.
#endif
                try
                {
                    std::move(task)();
                }
                catch (...)
                {
                    std::terminate();
                }
            });
    }
}  // namespace LLUtils
