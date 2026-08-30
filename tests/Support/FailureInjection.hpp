/// @file FailureInjection.hpp
/// @brief Reusable deterministic failure and lifetime probes for low-level tests.
#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace NGIN::Tests
{
    /// @brief Shared exactly-once lifetime counts for coroutine frames and other ownership probes.
    struct FrameLifetimeStatistics final
    {
        std::atomic<std::size_t> constructions {0};
        std::atomic<std::size_t> destructions {0};
        std::atomic<std::size_t> live {0};
    };

    /// @brief Records one lifetime against shared atomic statistics.
    class FrameLifetimeProbe final
    {
    public:
        explicit FrameLifetimeProbe(FrameLifetimeStatistics& statistics) noexcept
            : m_statistics(&statistics)
        {
            m_statistics->constructions.fetch_add(1, std::memory_order_relaxed);
            m_statistics->live.fetch_add(1, std::memory_order_relaxed);
        }

        FrameLifetimeProbe(const FrameLifetimeProbe&)            = delete;
        FrameLifetimeProbe& operator=(const FrameLifetimeProbe&) = delete;
        FrameLifetimeProbe(FrameLifetimeProbe&&)                 = delete;
        FrameLifetimeProbe& operator=(FrameLifetimeProbe&&)      = delete;

        ~FrameLifetimeProbe()
        {
            m_statistics->destructions.fetch_add(1, std::memory_order_relaxed);
            m_statistics->live.fetch_sub(1, std::memory_order_relaxed);
        }

    private:
        FrameLifetimeStatistics* m_statistics;
    };

    /// @brief Arms one deterministic operation to throw after a chosen number of successful calls.
    class FailureCountdown final
    {
    public:
        /// @brief Disables failure injection.
        void Disable() noexcept
        {
            m_remaining = Disabled;
        }

        /// @brief Arms the controller to throw after `successfulCalls` calls to `Hit()`.
        void Arm(const std::size_t successfulCalls) noexcept
        {
            m_remaining = successfulCalls;
        }

        /// @brief Records an injectable operation and throws when the countdown reaches zero.
        void Hit()
        {
            if (m_remaining == Disabled)
                return;
            if (m_remaining == 0)
                throw std::runtime_error("injected test failure");
            --m_remaining;
        }

    private:
        static constexpr std::size_t Disabled = (std::numeric_limits<std::size_t>::max)();
        std::size_t                  m_remaining {Disabled};
    };

    /// @brief PMR resource that can fail one precisely selected allocation attempt.
    class FailureMemoryResource final : public std::pmr::memory_resource
    {
    public:
        explicit FailureMemoryResource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource()) noexcept
            : m_upstream(upstream)
        {
        }

        /// @brief Arms the next allocation request to throw `std::bad_alloc`.
        void FailNextAllocation() noexcept
        {
            m_failureAttempt = m_allocationAttempts;
        }

        /// @brief Disables allocation failure.
        void DisableFailure() noexcept
        {
            m_failureAttempt = Disabled;
        }

        /// @brief Returns the number of allocation requests observed.
        [[nodiscard]] std::size_t AllocationAttempts() const noexcept
        {
            return m_allocationAttempts;
        }

    private:
        void* do_allocate(const std::size_t bytes, const std::size_t alignment) override
        {
            const std::size_t attempt = m_allocationAttempts++;
            if (attempt == m_failureAttempt)
            {
                throw std::bad_alloc {};
            }
            return m_upstream->allocate(bytes, alignment);
        }

        void do_deallocate(void* pointer, const std::size_t bytes, const std::size_t alignment) override
        {
            m_upstream->deallocate(pointer, bytes, alignment);
        }

        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
        {
            return this == &other;
        }

        static constexpr std::size_t Disabled = (std::numeric_limits<std::size_t>::max)();

        std::pmr::memory_resource* m_upstream;
        std::size_t                m_allocationAttempts {0};
        std::size_t                m_failureAttempt {Disabled};
    };

    /// @brief Counts the lifetime operations performed by `ThrowingValue` instances.
    struct LifetimeStatistics final
    {
        std::size_t live {0};
        std::size_t constructions {0};
        std::size_t destructions {0};
        std::size_t copies {0};
        std::size_t moves {0};
        std::size_t assignments {0};
    };

    /// @brief Value type whose construction and assignment can fail deterministically.
    /// @details The controller and statistics objects must outlive every value that refers to them.
    class ThrowingValue final
    {
    public:
        /// @brief Constructs an untracked value for APIs that require default construction.
        ThrowingValue() = default;

        /// @brief Constructs a tracked value after consulting the failure controller.
        ThrowingValue(
                const int           value,
                FailureCountdown&   failures,
                LifetimeStatistics& statistics)
            : m_value(value), m_failures(&failures), m_statistics(&statistics)
        {
            m_failures->Hit();
            RecordConstruction();
        }

        /// @brief Copy-constructs a value after consulting its failure controller.
        ThrowingValue(const ThrowingValue& other)
            : m_value(other.m_value), m_failures(other.m_failures), m_statistics(other.m_statistics)
        {
            Hit();
            RecordConstruction();
            if (m_statistics)
                ++m_statistics->copies;
        }

        /// @brief Move-constructs a value after consulting its failure controller.
        ThrowingValue(ThrowingValue&& other)
            : m_value(other.m_value), m_failures(other.m_failures), m_statistics(other.m_statistics)
        {
            Hit();
            RecordConstruction();
            if (m_statistics)
                ++m_statistics->moves;
        }

        /// @brief Copy-assigns after consulting the source failure controller.
        ThrowingValue& operator=(const ThrowingValue& other)
        {
            if (this == &other)
                return *this;

            if (other.m_failures)
                other.m_failures->Hit();
            RebindStatistics(other.m_statistics);
            m_value    = other.m_value;
            m_failures = other.m_failures;
            if (m_statistics)
            {
                ++m_statistics->copies;
                ++m_statistics->assignments;
            }
            return *this;
        }

        /// @brief Move-assigns after consulting the source failure controller.
        ThrowingValue& operator=(ThrowingValue&& other)
        {
            if (this == &other)
                return *this;

            if (other.m_failures)
                other.m_failures->Hit();
            RebindStatistics(other.m_statistics);
            m_value    = other.m_value;
            m_failures = other.m_failures;
            if (m_statistics)
            {
                ++m_statistics->moves;
                ++m_statistics->assignments;
            }
            return *this;
        }

        /// @brief Records destruction of a live tracked value.
        ~ThrowingValue()
        {
            ReleaseStatistics();
        }

        /// @brief Returns the test payload.
        [[nodiscard]] int Value() const noexcept
        {
            return m_value;
        }

        /// @brief Compares payload values.
        friend bool operator==(const ThrowingValue& left, const ThrowingValue& right) noexcept
        {
            return left.m_value == right.m_value;
        }

    private:
        void Hit()
        {
            if (m_failures)
                m_failures->Hit();
        }

        void RecordConstruction() noexcept
        {
            if (!m_statistics)
                return;
            ++m_statistics->live;
            ++m_statistics->constructions;
        }

        void RebindStatistics(LifetimeStatistics* statistics) noexcept
        {
            if (m_statistics == statistics)
                return;
            if (m_statistics)
                --m_statistics->live;
            m_statistics = statistics;
            if (m_statistics)
                ++m_statistics->live;
        }

        void ReleaseStatistics() noexcept
        {
            if (!m_statistics)
                return;
            --m_statistics->live;
            ++m_statistics->destructions;
            m_statistics = nullptr;
        }

        int                 m_value {0};
        FailureCountdown*   m_failures {nullptr};
        LifetimeStatistics* m_statistics {nullptr};
    };

    /// @brief Shared state for a deterministic, precisely tracked test allocator.
    template<std::size_t MaxLiveAllocations = 128>
    struct FailureAllocatorState final
    {
        std::size_t successfulAllocationsBeforeFailure {
                (std::numeric_limits<std::size_t>::max)()};
        std::size_t                           allocationAttempts {0};
        std::size_t                           allocations {0};
        std::size_t                           deallocations {0};
        std::array<void*, MaxLiveAllocations> livePointers {};
    };

    /// @brief Allocator that injects exhaustion and precisely tracks its live pointers.
    template<std::size_t MaxLiveAllocations = 128>
    class FailureAllocator final
    {
    public:
        using State = FailureAllocatorState<MaxLiveAllocations>;

        /// @brief Live-pointer tracking makes ownership queries definitive.
        static constexpr bool HasPreciseOwnership = true;

        /// @brief Creates an allocator with independent shared state.
        FailureAllocator()
            : m_state(std::make_shared<State>())
        {
        }

        /// @brief Creates an allocator referring to caller-provided shared state.
        explicit FailureAllocator(std::shared_ptr<State> state) noexcept
            : m_state(std::move(state))
        {
        }

        /// @brief Allocates until the configured failure point or tracking capacity is reached.
        [[nodiscard]] void* Allocate(const std::size_t size, const std::size_t alignment) noexcept
        {
            const std::size_t attempt = m_state->allocationAttempts++;
            if (attempt >= m_state->successfulAllocationsBeforeFailure)
                return nullptr;

            std::size_t freeIndex = MaxLiveAllocations;
            for (std::size_t index = 0; index < MaxLiveAllocations; ++index)
            {
                if (!m_state->livePointers[index])
                {
                    freeIndex = index;
                    break;
                }
            }
            if (freeIndex == MaxLiveAllocations)
                return nullptr;

            void* pointer = m_system.Allocate(size, alignment);
            if (!pointer)
                return nullptr;

            m_state->livePointers[freeIndex] = pointer;
            ++m_state->allocations;
            return pointer;
        }

        /// @brief Releases a pointer previously returned by this allocator state.
        void Deallocate(void* pointer, const std::size_t size, const std::size_t alignment) noexcept
        {
            if (!pointer)
                return;

            for (void*& livePointer: m_state->livePointers)
            {
                if (livePointer != pointer)
                    continue;
                livePointer = nullptr;
                ++m_state->deallocations;
                m_system.Deallocate(pointer, size, alignment);
                return;
            }
        }

        /// @brief Returns a definitive answer based on the live-pointer table.
        [[nodiscard]] Memory::Ownership OwnershipOf(const void* pointer) const noexcept
        {
            for (const void* livePointer: m_state->livePointers)
            {
                if (livePointer == pointer)
                    return Memory::Ownership::Owns;
            }
            return Memory::Ownership::DoesNotOwn;
        }

        /// @brief Returns the shared instrumentation state.
        [[nodiscard]] const std::shared_ptr<State>& GetState() const noexcept
        {
            return m_state;
        }

    private:
        std::shared_ptr<State>  m_state;
        Memory::SystemAllocator m_system {};
    };
}// namespace NGIN::Tests
