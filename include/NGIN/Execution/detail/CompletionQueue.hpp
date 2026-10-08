#pragma once

#include <NGIN/Execution/CompletionReservation.hpp>
#include <NGIN/Execution/ScheduleResult.hpp>
#include <NGIN/Execution/WorkItem.hpp>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <expected>
#include <memory_resource>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace NGIN::Execution::detail
{
    // The queue owns accepted delivery nodes, including reservations not yet
    // published. Executors must drive it until Outstanding() reaches zero.
    class CompletionQueue final
    {
    public:
        using Wake = void (*)(void*) noexcept;

        /// @param singleDriver The queue has one driving thread. Publications then
        /// signal it after releasing the lock (see Queue).
        explicit CompletionQueue(std::size_t capacity = 4096, Wake wake = nullptr, void* context = nullptr,
                                 std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
                                 bool singleDriver = false)
            : m_capacity(capacity), m_wake(wake), m_context(context), m_resource(resource),
              m_signalAfterUnlock(singleDriver && wake)
        {
            if (capacity == 0 || !resource)
                throw std::invalid_argument("Completion queue requires positive capacity and an allocator");
        }

        ~CompletionQueue()
        {
            // A producer may still be signaling the executor after its item was
            // consumed. Its final access is the decrement below; wait for it.
            // The owner keeps the signaled object alive until this returns.
            while (m_notifying.load(std::memory_order_acquire) != 0)
                std::this_thread::yield();
            if (m_outstanding != 0)
                std::terminate();
        }

        CompletionQueue(const CompletionQueue&)            = delete;
        CompletionQueue& operator=(const CompletionQueue&) = delete;

        [[nodiscard]] std::expected<CompletionReservation, ScheduleError> Reserve(WorkItem work) noexcept
        {
            if (work.IsEmpty())
                return std::unexpected(ScheduleError::Rejected);
            std::lock_guard lock(m_mutex);
            if (m_closed)
                return std::unexpected(ScheduleError::Stopped);
            if (m_outstanding == m_capacity)
                return std::unexpected(ScheduleError::ResourceExhausted);
            Node* node;
            try
            {
                std::pmr::polymorphic_allocator<Node> allocator(m_resource);
                node = allocator.new_object<Node>(this, std::move(work));
            } catch (const std::bad_alloc&)
            {
                return std::unexpected(ScheduleError::ResourceExhausted);
            }
            ++m_outstanding;
            return CompletionReservation(node, &Publish, &Release, &Schedule);
        }

        void Close() noexcept
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
            Notify();
        }

        [[nodiscard]] std::size_t Outstanding() const noexcept
        {
            std::lock_guard lock(m_mutex);
            return m_outstanding;
        }

        [[nodiscard]] bool HasReady() const noexcept
        {
            std::lock_guard lock(m_mutex);
            return m_head != nullptr;
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            Node* node;
            {
                std::lock_guard lock(m_mutex);
                node = m_head;
                if (!node)
                    return false;
                node->state = Node::State::Running;
                m_head      = node->next;
                if (!m_head)
                    m_tail = nullptr;
            }
            node->work.Invoke();
            bool destroy = false;
            {
                std::lock_guard lock(m_mutex);
                if (node->pending)
                {
                    node->pending = false;
                    Enqueue(node);
                }
                else
                {
                    node->state = Node::State::Reserved;
                    destroy     = !node->owned;
                }
            }
            if (destroy)
                Destroy(node);
            return true;
        }

    private:
        struct Node
        {
            Node(CompletionQueue* queue, WorkItem item) noexcept : owner(queue), work(std::move(item)) {}
            CompletionQueue* owner;
            WorkItem         work;
            Node*            next {};
            enum class State
            {
                Reserved,
                Queued,
                Running
            };
            State state {State::Reserved};
            bool  owned {true};
            bool  pending {false};
        };

        void Enqueue(Node* node, bool notify = true) noexcept
        {
            node->state = Node::State::Queued;
            node->next  = nullptr;
            if (m_tail)
                m_tail->next = node;
            else
                m_head = node;
            m_tail = node;
            if (notify)
                Notify();
        }

        static void Publish(void* state) noexcept { Queue(state, true); }
        static void Schedule(void* state) noexcept { Queue(state, false); }

        static void Queue(void* state, bool transfer) noexcept
        {
            Node*            node  = static_cast<Node*>(state);
            CompletionQueue* owner = node->owner;
            {
                std::lock_guard lock(owner->m_mutex);
                // Concurrent/repeated deliveries into one reserved callback are a
                // producer contract violation, not an invitation to drop a resume.
                if (!node->owned || node->state == Node::State::Queued || node->pending)
                    std::terminate();
                if (transfer)
                    node->owned = false;
                if (node->state == Node::State::Running)
                {
                    node->pending = true;
                    return;
                }
                // Waking a blocked driver can take microseconds. Doing that under
                // the lock stalls every concurrent producer behind the wake, so a
                // single-driver queue signals after unlocking; its destructor waits
                // for this in-flight signal. With several drivers, signaling under
                // the lock keeps woken drivers waiting for the publisher, which
                // retains its own continuation chains instead of migrating each step.
                owner->Enqueue(node, !owner->m_signalAfterUnlock);
                if (!owner->m_signalAfterUnlock)
                    return;
                owner->m_notifying.fetch_add(1, std::memory_order_relaxed);
            }
            owner->m_wake(owner->m_context);
            owner->m_notifying.fetch_sub(1, std::memory_order_release);
        }

        static void Release(void* state) noexcept
        {
            Node*            node  = static_cast<Node*>(state);
            CompletionQueue* owner = node->owner;
            bool             destroy;
            {
                std::lock_guard lock(owner->m_mutex);
                assert(node->owned);
                node->owned = false;
                destroy     = node->state == Node::State::Reserved;
            }
            if (destroy)
                Destroy(node);
        }

        static void Destroy(Node* node) noexcept
        {
            CompletionQueue* owner = node->owner;
            // Callable destruction may release another reservation. Do it
            // outside the queue lock, before publishing capacity recovery.
            node->work = {};
            {
                std::lock_guard                       lock(owner->m_mutex);
                std::pmr::polymorphic_allocator<Node> allocator(owner->m_resource);
                allocator.delete_object(node);
                assert(owner->m_outstanding != 0);
                --owner->m_outstanding;
                // Capacity recovery creates no runnable work. Only the final
                // retirement after Close can unblock an executor's drain wait.
                if (owner->m_closed && owner->m_outstanding == 0)
                    owner->Notify();
            }
        }

        void Notify() noexcept
        {
            // Called under the queue lock. The private hook may signal a wait
            // primitive, but must never dispatch work or reenter this queue.
            if (m_wake)
                m_wake(m_context);
        }

        const std::size_t          m_capacity;
        Wake                       m_wake;
        void*                      m_context;
        std::pmr::memory_resource* m_resource;
        mutable std::mutex         m_mutex;
        Node*                      m_head {};
        Node*                      m_tail {};
        std::size_t                m_outstanding {};
        const bool                 m_signalAfterUnlock;
        std::atomic<std::size_t>   m_notifying {};
        bool                       m_closed {};
    };
}// namespace NGIN::Execution::detail
