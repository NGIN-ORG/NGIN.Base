#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <atomic>
#include <vector>

#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Sync/SpinLock.hpp>

namespace NGIN::Async
{
    /// @brief Exception thrown when an async operation observes cancellation.
    class TaskCanceled : public std::exception
    {
    public:
        const char* what() const noexcept override
        {
            return "Task was canceled";
        }
    };

    namespace detail
    {
        struct CancellationState;
    }// namespace detail

    using CancellationCallback = bool (*)(void*) noexcept;

    class CancellationRegistration final
    {
    public:
        CancellationRegistration() noexcept = default;

        CancellationRegistration(CancellationRegistration&& other) noexcept
        {
            MoveFrom(std::move(other));
        }

        CancellationRegistration& operator=(CancellationRegistration&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        CancellationRegistration(const CancellationRegistration&)            = delete;
        CancellationRegistration& operator=(const CancellationRegistration&) = delete;

        ~CancellationRegistration()
        {
            Reset();
        }

        void Reset() noexcept;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_state != nullptr;
        }

    private:
        friend struct detail::CancellationState;
        friend class CancellationToken;

        void Fire() noexcept;

        void MoveFrom(CancellationRegistration&& other) noexcept
        {
            if (other.m_state)
            {
                other.Reset();
            }

            m_state          = std::move(other.m_state);
            m_exec           = other.m_exec;
            m_handle         = other.m_handle;
            m_callback       = other.m_callback;
            m_callbackCtx    = other.m_callbackCtx;
            m_index          = other.m_index;
            m_armed.store(other.m_armed.load(std::memory_order_relaxed), std::memory_order_relaxed);

            other.m_exec        = {};
            other.m_handle      = {};
            other.m_callback    = nullptr;
            other.m_callbackCtx = nullptr;
            other.m_index       = static_cast<std::size_t>(-1);
            other.m_armed.store(false, std::memory_order_relaxed);
            other.m_state.reset();
        }

        std::shared_ptr<detail::CancellationState> m_state {};
        NGIN::Execution::ExecutorRef               m_exec {};
        std::coroutine_handle<>                   m_handle {};
        CancellationCallback                      m_callback {nullptr};
        void*                                     m_callbackCtx {nullptr};
        std::size_t                               m_index {static_cast<std::size_t>(-1)};
        std::atomic<bool>                         m_armed {false};
    };

    // Cancellation primitives
    class CancellationToken
    {
    public:
        CancellationToken() = default;
        explicit CancellationToken(std::shared_ptr<detail::CancellationState> state) noexcept
            : m_state(std::move(state))
        {
        }

        [[nodiscard]] bool IsCancellationRequested() const noexcept;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return IsCancellationRequested();
        }

        void Register(CancellationRegistration& outRegistration,
                      NGIN::Execution::ExecutorRef exec,
                      std::coroutine_handle<> handle,
                      CancellationCallback callback = nullptr,
                      void* callbackCtx = nullptr) const noexcept;

    private:
        std::shared_ptr<detail::CancellationState> m_state {};
        friend class CancellationSource;
    };

    namespace detail
    {
        struct CancellationState final
        {
            std::atomic<bool> canceled {false};
            NGIN::Sync::SpinLock lock {};
            std::vector<CancellationRegistration*> registrations {};

            CancellationState()
            {
                registrations.reserve(8);
            }

            void Register(CancellationRegistration* registration) noexcept
            {
                if (!registration)
                {
                    return;
                }
                std::lock_guard guard(lock);
                registration->m_index = registrations.size();
                registrations.push_back(registration);
            }

            void Unregister(CancellationRegistration* registration) noexcept
            {
                if (!registration)
                {
                    return;
                }
                std::lock_guard guard(lock);
                if (registration->m_index < registrations.size() && registrations[registration->m_index] == registration)
                {
                    const auto lastIndex = registrations.size() - 1;
                    if (registration->m_index != lastIndex)
                    {
                        auto* moved = registrations[lastIndex];
                        registrations[registration->m_index] = moved;
                        moved->m_index = registration->m_index;
                    }
                    registrations.pop_back();
                    return;
                }

                for (std::size_t i = 0; i < registrations.size(); ++i)
                {
                    if (registrations[i] == registration)
                    {
                        const auto lastIndex = registrations.size() - 1;
                        if (i != lastIndex)
                        {
                            auto* moved = registrations[lastIndex];
                            registrations[i] = moved;
                            moved->m_index = i;
                        }
                        registrations.pop_back();
                        return;
                    }
                }
            }

            void Cancel() noexcept
            {
                const bool already = canceled.exchange(true, std::memory_order_acq_rel);
                if (already)
                {
                    return;
                }

                std::vector<CancellationRegistration*> local;
                {
                    std::lock_guard guard(lock);
                    local = registrations;
                }

                for (auto* reg: local)
                {
                    if (reg)
                    {
                        reg->Fire();
                    }
                }
            }
        };
    }// namespace detail

    class CancellationSource
    {
    public:
        CancellationSource()
            : m_state(std::make_shared<detail::CancellationState>())
        {
        }

        void Cancel() noexcept
        {
            m_state->Cancel();
        }

        [[nodiscard]] CancellationToken GetToken() const noexcept
        {
            return CancellationToken(m_state);
        }

        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_state->canceled.load(std::memory_order_acquire);
        }

    private:
        std::shared_ptr<detail::CancellationState> m_state;
    };

    inline bool CancellationToken::IsCancellationRequested() const noexcept
    {
        return m_state && m_state->canceled.load(std::memory_order_acquire);
    }

    inline void CancellationToken::Register(CancellationRegistration& outRegistration,
                                            NGIN::Execution::ExecutorRef exec,
                                            std::coroutine_handle<> handle,
                                            CancellationCallback callback,
                                            void* callbackCtx) const noexcept
    {
        outRegistration.Reset();
        if (!m_state || !exec.IsValid() || !handle)
        {
            return;
        }

        if (m_state->canceled.load(std::memory_order_acquire))
        {
            bool shouldResume = true;
            if (callback)
            {
                shouldResume = callback(callbackCtx);
            }
            if (shouldResume)
            {
                exec.Execute(handle);
            }
            return;
        }

        outRegistration.m_state       = m_state;
        outRegistration.m_exec        = exec;
        outRegistration.m_handle      = handle;
        outRegistration.m_callback    = callback;
        outRegistration.m_callbackCtx = callbackCtx;
        outRegistration.m_armed.store(true, std::memory_order_relaxed);
        m_state->Register(&outRegistration);
    }

    inline void CancellationRegistration::Reset() noexcept
    {
        if (!m_state)
        {
            return;
        }
        m_armed.store(false, std::memory_order_relaxed);
        m_state->Unregister(this);
        m_state.reset();
        m_exec           = {};
        m_handle         = {};
        m_callback       = nullptr;
        m_callbackCtx    = nullptr;
        m_index          = static_cast<std::size_t>(-1);
    }

    inline void CancellationRegistration::Fire() noexcept
    {
        bool shouldResume = true;
        if (m_callback)
        {
            shouldResume = m_callback(m_callbackCtx);
        }

        bool expected = true;
        if (m_armed.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
        {
            if (shouldResume && m_exec.IsValid() && m_handle)
            {
                m_exec.Execute(m_handle);
            }
        }
    }
}
