/// @file WorkItem.hpp
/// @brief A schedulable work item: coroutine continuation or job.
#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include <NGIN/Utilities/Callable.hpp>

namespace NGIN::Execution
{
    /// @brief A move-only unit of work that can be executed by an executor/scheduler.
    ///
    /// WorkItem is a lightweight wrapper that can represent either:
    /// - a `std::coroutine_handle<>` continuation, or
    /// - a normal job (`NGIN::Utilities::Callable<void()>`).
    ///
    /// @note `Invoke()` is `noexcept`; any exception escaping the job/coroutine will call `std::terminate()`.
    class WorkItem final
    {
    public:
        enum class Kind : unsigned char
        {
            None,
            Coroutine,
            Job,
        };

        constexpr WorkItem() noexcept = default;

        explicit WorkItem(std::coroutine_handle<> coroutine) noexcept
            : m_kind(Kind::Coroutine)
        {
            m_storage.coroutine = coroutine;
        }

        explicit WorkItem(NGIN::Utilities::Callable<void()> job)
            : m_kind(Kind::Job)
        {
            if (!job)
            {
                throw std::invalid_argument("NGIN::Execution::WorkItem: job must be non-empty");
            }
            new (&m_storage.job) NGIN::Utilities::Callable<void()>(std::move(job));
        }

        WorkItem(WorkItem&& other) noexcept
        {
            MoveFrom(std::move(other));
        }

        WorkItem& operator=(WorkItem&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        WorkItem(const WorkItem&)            = delete;
        WorkItem& operator=(const WorkItem&) = delete;

        ~WorkItem()
        {
            Reset();
        }

        [[nodiscard]] constexpr Kind GetKind() const noexcept
        {
            return m_kind;
        }

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return m_kind == Kind::None;
        }

        [[nodiscard]] constexpr bool IsCoroutine() const noexcept
        {
            return m_kind == Kind::Coroutine;
        }

        [[nodiscard]] constexpr bool IsJob() const noexcept
        {
            return m_kind == Kind::Job;
        }

        [[nodiscard]] std::coroutine_handle<> GetCoroutine() const noexcept
        {
            return (m_kind == Kind::Coroutine) ? m_storage.coroutine : std::coroutine_handle<> {};
        }

        void Invoke() noexcept
        {
            try
            {
                if (m_kind == Kind::Coroutine)
                {
                    if (m_storage.coroutine && !m_storage.coroutine.done())
                    {
                        m_storage.coroutine.resume();
                    }
                    return;
                }
                if (m_kind == Kind::Job)
                {
                    m_storage.job();
                    return;
                }
            } catch (...)
            {
                std::terminate();
            }
        }

    private:
        union Storage
        {
            std::coroutine_handle<> coroutine;
            NGIN::Utilities::Callable<void()> job;

            constexpr Storage() noexcept
                : coroutine(nullptr)
            {
            }

            ~Storage() {}
        };

        void Reset() noexcept
        {
            if (m_kind == Kind::Job)
            {
                std::destroy_at(std::addressof(m_storage.job));
            }
            m_kind             = Kind::None;
            m_storage.coroutine = nullptr;
        }

        void MoveFrom(WorkItem&& other) noexcept
        {
            m_kind = other.m_kind;
            if (m_kind == Kind::Coroutine)
            {
                m_storage.coroutine = other.m_storage.coroutine;
                other.m_storage.coroutine = nullptr;
                other.m_kind              = Kind::None;
                return;
            }
            if (m_kind == Kind::Job)
            {
                new (&m_storage.job) NGIN::Utilities::Callable<void()>(std::move(other.m_storage.job));
                std::destroy_at(std::addressof(other.m_storage.job));
                other.m_kind = Kind::None;
                other.m_storage.coroutine = nullptr;
                return;
            }
        }

        Kind m_kind {Kind::None};
        Storage m_storage {};
    };
}// namespace NGIN::Execution
