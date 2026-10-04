#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace LLUtils
{
    // Single-consumer, multiple-producer queue with slot and in-flight weight limits.
    // One extra overweight item remains complete. The queue must outlive registrations/work items;
    // close, await producers and join the consumer before destroying it. No thread is owned here.
    // Payload moves/destruction and admission bookkeeping must not reenter the queue while its lock is held.
    template <class T>
        requires std::is_nothrow_move_constructible_v<T>
    class BoundedWorkQueue
    {
        struct Entry
        {
            T value;             // Owned pending payload.
            std::size_t weight;  // Cached charge, not recomputed after moves.
            Entry(T&& payload, std::size_t charge) noexcept : value(std::move(payload)), weight(charge) {}
        };

      public:

        // Registers work outside the queue (e.g. preparing an item); closing rejects future registration.
        class Producer
        {
          public:

            Producer(Producer&& other) noexcept : fQueue(std::exchange(other.fQueue, nullptr)) {}
            Producer& operator=(Producer&& other) noexcept
            {
                if (this != &other)
                {
                    Release();
                    fQueue = std::exchange(other.fQueue, nullptr);
                }
                return *this;
            }
            ~Producer() { Release(); }
            Producer(const Producer&)            = delete;
            Producer& operator=(const Producer&) = delete;

          private:

            friend class BoundedWorkQueue;
            explicit Producer(BoundedWorkQueue* queue) : fQueue(queue) {}
            void Release() noexcept
            {
                if (fQueue)
                {
                    const std::lock_guard lock(fQueue->fMutex);
                    if (--fQueue->fProducers == 0)
                        fQueue->fProducersDone.notify_all();
                    fQueue = nullptr;
                }
            }
            BoundedWorkQueue* fQueue;  // Borrowed queue; exactly one registration is released.
        };
        enum class WaitStatus
        {
            Item,
            Timeout,
            Closed
        };
        // A dequeue result owns the payload directly, avoiding a second move into an optional wrapper.
        // Its slot releases immediately, but weight stays charged until Complete/destruction.
        class WorkItem
        {
          public:

            WorkItem(WorkItem&& other) noexcept
                : fQueue(std::exchange(other.fQueue, nullptr)), fValue(std::move(other.fValue)), fWeight(other.fWeight),
                  fStatus(other.fStatus)
            {
                other.fValue.reset();
            }
            WorkItem& operator=(WorkItem&& other) noexcept
            {
                if (this != &other)
                {
                    Complete();
                    fValue.reset();
                    if (other.fValue)
                        fValue.emplace(std::move(*other.fValue));
                    fQueue  = std::exchange(other.fQueue, nullptr);
                    fWeight = other.fWeight;
                    fStatus = other.fStatus;
                    other.fValue.reset();
                }
                return *this;
            }
            ~WorkItem() { Complete(); }
            WorkItem(const WorkItem&)            = delete;
            WorkItem& operator=(const WorkItem&) = delete;
            WaitStatus Status() const noexcept { return fStatus; }
            explicit operator bool() const noexcept { return fValue.has_value(); }
            T& Value() noexcept { return *fValue; }
            // Release allowance once; payload stays valid so a caller can signal completion afterward.
            void Complete() noexcept
            {
                if (fQueue)
                {
                    const std::lock_guard lock(fQueue->fMutex);
                    if (fWeight > fQueue->fWeightLimit)
                        fQueue->fOversized = false;
                    else
                        fQueue->fCharged -= fWeight;
                    fQueue->fCapacity.notify_all();
                    fQueue = nullptr;
                }
            }

          private:

            friend class BoundedWorkQueue;
            explicit WorkItem(WaitStatus status) : fQueue(nullptr), fWeight(0), fStatus(status) {}
            // Called only with the queue mutex held; construct the final result before recycling its slot.
            WorkItem(BoundedWorkQueue* queue, Entry&& entry) noexcept
                : fQueue(queue), fValue(std::move(entry.value)), fWeight(entry.weight), fStatus(WaitStatus::Item)
            {
                queue->fRing[queue->fHead].reset();
                queue->fHead = (queue->fHead + 1) % queue->fRing.size();
                --queue->fCount;
                queue->fCapacity.notify_all();
            }
            BoundedWorkQueue* fQueue;  // Borrowed owner of the in-flight allowance.
            std::optional<T> fValue;   // Owned payload, including after explicit completion.
            std::size_t fWeight;       // Cached admission charge, independent of payload moves.
            WaitStatus fStatus;        // Result kind; moved-from objects have no payload.
        };
        using WaitResult = WorkItem;
        BoundedWorkQueue(std::size_t slots, std::size_t weightLimit) : fWeightLimit(weightLimit)
        {
            if (!slots || !weightLimit)
                throw std::invalid_argument("Queue limits must be positive");
            fRing.resize(slots);
        }
        std::optional<Producer> TryRegisterProducer()
        {
            const std::lock_guard lock(fMutex);
            if (!fAccepting)
                return {};
            ++fProducers;
            return Producer(this);
        }
        // Admission callbacks may only perform nonblocking, nonthrowing bookkeeping under the queue lock.
        template <class BeforeCommit>
            requires std::is_nothrow_invocable_v<BeforeCommit&, T&>
        bool Push(T value, std::size_t weight, BeforeCommit&& beforeCommit)
        {
            std::unique_lock lock(fMutex);
            fCapacity.wait(lock,
                           [&]
                           {
                               return !fAccepting ||
                                      (fCount < fRing.size() &&
                                       (weight > fWeightLimit ? !fOversized : weight <= fWeightLimit - fCharged));
                           });
            if (!fAccepting)
                return false;
            std::invoke(beforeCommit, value);
            fRing[(fHead + fCount) % fRing.size()].emplace(std::move(value), weight);
            ++fCount;
            if (weight > fWeightLimit)
                fOversized = true;
            else
                fCharged += weight;
            fWork.notify_one();
            return true;
        }
        bool Push(T value, std::size_t weight = 0)
        {
            return Push(std::move(value), weight, [](T&) noexcept {});
        }
        WaitResult WaitUntil(std::optional<std::chrono::steady_clock::time_point> deadline = {})
        {
            std::unique_lock lock(fMutex);
            auto ready = [&] { return fCount != 0 || !fAccepting; };
            if (deadline)
                fWork.wait_until(lock, *deadline, ready);
            else
                fWork.wait(lock, ready);
            if (!fCount)
                return WorkItem(fAccepting ? WaitStatus::Timeout : WaitStatus::Closed);
            // Guaranteed copy elision constructs the final work item without another payload move.
            return WorkItem(this, std::move(*fRing[fHead]));
        }
        void Close() noexcept
        {
            const std::lock_guard lock(fMutex);
            fAccepting = false;
            fCapacity.notify_all();
            fWork.notify_all();
        }
        void WaitForProducers()
        {
            std::unique_lock lock(fMutex);
            fProducersDone.wait(lock, [&] { return fProducers == 0; });
        }

      private:

        std::mutex fMutex;  // Protects every queue/registration/accounting field below.
        std::condition_variable fWork, fCapacity, fProducersDone;  // Consumer, admission and lifetime notifications.
        std::vector<std::optional<Entry>> fRing;                   // Fixed slots allocated once, not a growing backlog.
        const std::size_t fWeightLimit;                            // Normal queued/in-flight allowance.
        std::size_t fHead = 0, fCount = 0, fCharged = 0, fProducers = 0;  // Ring and lifetime accounting.
        bool fAccepting = true, fOversized = false;                       // Close state and extra-item allowance.
    };
}  // namespace LLUtils
