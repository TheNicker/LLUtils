/*
Copyright (c) 2019 Lior Lahav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include <LLUtils/UniqueIDProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace LLUtils
{
    template <class T, bool ThreadSafe = false>
    class Event;

    namespace EventDetail
    {
        // Both event modes own their registrations through the same move-only handle.
        // The event supplies its token and unsubscription policy; no type erasure is needed.
        template <class Owner, class Token>
        class Subscription
        {
          public:

            Subscription()                               = default;
            Subscription(const Subscription&)            = delete;
            Subscription& operator=(const Subscription&) = delete;
            Subscription(Subscription&& other) noexcept { MoveFrom(other); }
            Subscription& operator=(Subscription&& other) noexcept
            {
                if (this != &other)
                {
                    Unsubscribe();
                    MoveFrom(other);
                }
                return *this;
            }
            ~Subscription() { Unsubscribe(); }
            [[nodiscard]] explicit operator bool() const noexcept { return fEvent != nullptr; }

            void Unsubscribe() noexcept(noexcept(std::declval<Owner&>().Unsubscribe(std::declval<Token>())))
            {
                if (fEvent != nullptr)
                {
                    fEvent->Unsubscribe(fToken);
                    fEvent = nullptr;
                    fToken = {};
                }
            }

          private:

            friend Owner;
            Subscription(Owner* event, Token token) : fEvent(event), fToken(std::move(token)) {}
            void MoveFrom(Subscription& other) noexcept
            {
                fEvent       = other.fEvent;
                fToken       = std::move(other.fToken);
                other.fEvent = nullptr;
                other.fToken = {};
            }
            Owner* fEvent{};
            Token fToken{};
        };
    }  // namespace EventDetail

    template <class T>
    class Event<T, false>
    {
      public:

        // Event is single-threaded and must stay at the same address while subscribed.
        // It must outlive its subscriptions; only unsubscription is supported during dispatch.
        using Func = std::function<T>;

      private:

        using SubscriptionID         = std::uint64_t;
        using SubscriptionIDProvider = UniqueIdProvider<SubscriptionID>;

      public:

        using Subscription = EventDetail::Subscription<Event, SubscriptionID>;

        template <class... Args>
        void Raise(Args... args)
        {
            RaiseWhile([] { return true; }, args...);
        }

        // Recheck a lifetime/state boundary between listeners, including after nested dispatch.
        // Existing Raise() retains its unconditional propagation policy.
        template <class Predicate, class... Args>
        void RaiseWhile(Predicate&& continueDispatch, Args&&... args)
        {
            const RaiseScope raiseScope(*this);
            const auto listenerCount = fListeners.size();

            for (std::size_t index = 0; index < listenerCount && continueDispatch(); ++index)
            {
                auto& listener = fListeners[index];
                if (listener.subscribed)
                    listener.func(args...);
            }
        }

        [[nodiscard]] Subscription Subscribe(Func func)
        {
            const auto id = fSubscriptionIDProvider.Acquire();
            fListeners.push_back(Listener{id, std::move(func), true});
            return Subscription(this, id);
        }

      private:

        friend Subscription;

        struct Listener
        {
            SubscriptionID id{};
            Func func;
            bool subscribed = true;
        };

        using Listeners = std::vector<Listener>;

        class RaiseScope
        {
          public:

            explicit RaiseScope(Event& event) : fEvent(event) { ++fEvent.fRaiseDepth; }
            RaiseScope(const RaiseScope&)            = delete;
            RaiseScope& operator=(const RaiseScope&) = delete;

            ~RaiseScope()
            {
                --fEvent.fRaiseDepth;
                if (fEvent.fRaiseDepth == 0)
                    fEvent.RemoveUnsubscribedListeners();
            }

          private:

            Event& fEvent;
        };

        void Unsubscribe(SubscriptionID id)
        {
            for (auto& listener : fListeners)
            {
                if (listener.id == id)
                {
                    listener.subscribed = false;
                    break;
                }
            }

            if (fRaiseDepth == 0)
                RemoveUnsubscribedListeners();
        }

        void RemoveUnsubscribedListeners()
        {
            fListeners.erase(std::remove_if(fListeners.begin(), fListeners.end(),
                                            [](const Listener& listener) { return !listener.subscribed; }),
                             fListeners.end());
        }

        Listeners fListeners;
        SubscriptionIDProvider fSubscriptionIDProvider{1};
        std::uint32_t fRaiseDepth = 0;
    };

    // Thread safety is opt-in: default events retain their storage, live-dispatch semantics,
    // and lack of locking/snapshot allocation. This mode synchronizes registration and
    // snapshots only; callbacks can run concurrently and must protect their own state.
    template <class T>
    class Event<T, true>
    {
      public:

        using Func         = std::function<T>;
        using Subscription = EventDetail::Subscription<Event, std::shared_ptr<Func>>;

        // The event must stay at the same address and outlive its subscriptions and all calls.
        // Different handles may be used concurrently; the same handle needs external synchronization.
        [[nodiscard]] Subscription Subscribe(Func func)
        {
            auto callback = std::make_shared<Func>(std::move(func));
            {
                const std::lock_guard<std::mutex> lock(fMutex);
                fCallbacks.push_back(callback);
            }
            return Subscription(this, std::move(callback));
        }

        template <class... Args>
        void Raise(Args&&... args)
        {
            RaiseWhile([] { return true; }, std::forward<Args>(args)...);
        }

        // Like the default mode, callback/predicate exceptions propagate and stop dispatch.
        // Unsubscription affects future snapshots; already selected callbacks may still run.
        template <class Predicate, class... Args>
        void RaiseWhile(Predicate&& continueDispatch, Args&&... args)
        {
            VisitListeners(
                [&](const Func& func)
                {
                    const bool dispatch = continueDispatch();
                    if (dispatch)
                        func(args...);
                    return dispatch;
                });
        }

      protected:

        // Visit a stable snapshot outside the lock, stopping when the visitor returns false.
        // Derived notifications can provide failure policy without duplicating event ownership.
        template <class Visitor>
        void VisitListeners(Visitor&& visitor)
        {
            // Copy construction can throw; an MSVC debug vector's noexcept default
            // constructor allocates iterator bookkeeping and cannot provide that guarantee.
            auto callbacks = [&]
            {
                const std::lock_guard<std::mutex> lock(fMutex);
                return fCallbacks;
            }();
            for (const auto& callback : callbacks)
            {
                if (!visitor(*callback))
                    break;
            }
        }

      private:

        friend Subscription;

        void Unsubscribe(const std::shared_ptr<Func>& callback) noexcept
        {
            // The subscription retains the removed callback until this lock has been released.
            const std::lock_guard<std::mutex> lock(fMutex);
            std::erase(fCallbacks, callback);
        }

        std::mutex fMutex;
        std::vector<std::shared_ptr<Func>> fCallbacks;
    };
}  // namespace LLUtils
