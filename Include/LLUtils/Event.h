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
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
namespace LLUtils
{

    template <class T>
    class Event
    {
      public:

        // Event is single-threaded. Event must outlive all Connection objects.
        using Func = std::function<T>;

      private:

        using ConnectionID         = std::uint64_t;
        using ConnectionIDProvider = UniqueIdProvider<ConnectionID>;

      public:

        class Connection
        {
          public:

            Connection()                             = default;
            Connection(const Connection&)            = delete;
            Connection& operator=(const Connection&) = delete;

            Connection(Connection&& other) noexcept { MoveFrom(other); }

            Connection& operator=(Connection&& other) noexcept
            {
                if (this != &other)
                {
                    Disconnect();
                    MoveFrom(other);
                }

                return *this;
            }

            ~Connection() { Disconnect(); }

            void Disconnect()
            {
                if (fEvent != nullptr)
                {
                    fEvent->Disconnect(fID);
                    fEvent = nullptr;
                    fID    = {};
                }
            }

          private:

            friend class Event<T>;

            Connection(Event* event, ConnectionID id) : fEvent(event), fID(id) {}

            void MoveFrom(Connection& other) noexcept
            {
                fEvent       = other.fEvent;
                fID          = other.fID;
                other.fEvent = nullptr;
                other.fID    = {};
            }

            Event* fEvent{};
            ConnectionID fID{};
        };

        template <class... Args>
        void Raise(Args... args)
        {
            const RaiseScope raiseScope(*this);
            const auto listenerCount = fListeners.size();

            for (std::size_t index = 0; index < listenerCount; ++index)
            {
                auto& listener = fListeners[index];
                if (listener.connected)
                    listener.func(args...);
            }
        }

        [[nodiscard]] Connection Connect(Func func) { return Connection(this, AddListener(std::move(func))); }

        void Add(Func func) { AddListener(std::move(func)); }

        [[deprecated("Remove(Func) is only reliable for raw function pointers. Use Connect() and "
                     "Connection::Disconnect().")]]
        void Remove(const Func& func)
        {
            const auto address = GetAddress(func);
            if (address == nullptr)
                return;

            for (auto& listener : fListeners)
            {
                if (listener.connected && GetAddress(listener.func) == address)
                    listener.connected = false;
            }

            if (fRaiseDepth == 0)
                RemoveDisconnectedListeners();
        }

      private:

        struct Listener
        {
            ConnectionID id{};
            Func func;
            bool connected = true;
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
                    fEvent.RemoveDisconnectedListeners();
            }

          private:

            Event& fEvent;
        };

        ConnectionID AddListener(Func func)
        {
            const auto id = fConnectionIDProvider.Acquire();
            fListeners.push_back(Listener{id, std::move(func), true});
            return id;
        }

        void Disconnect(ConnectionID id)
        {
            for (auto& listener : fListeners)
            {
                if (listener.id == id)
                {
                    listener.connected = false;
                    break;
                }
            }

            if (fRaiseDepth == 0)
                RemoveDisconnectedListeners();
        }

        static void* GetAddress(const Func& func)
        {
            using FnPointer = T*;
            auto fnPointer  = func.template target<FnPointer>();
            return fnPointer != nullptr ? reinterpret_cast<void*>(*fnPointer) : nullptr;
        }

        void RemoveDisconnectedListeners()
        {
            fListeners.erase(std::remove_if(fListeners.begin(), fListeners.end(),
                                            [](const Listener& listener) { return !listener.connected; }),
                             fListeners.end());
        }

        Listeners fListeners;
        ConnectionIDProvider fConnectionIDProvider{1};
        std::uint32_t fRaiseDepth = 0;
    };
}  // namespace LLUtils
