#pragma once

#include <coroutine>
#include <exception>
#include <initializer_list>
#include <memory>
#include <utility>
#include <atomic>
#include <vector>

#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Sync/LockGuard.hpp>
#include <NGIN/Sync/SpinLock.hpp>
#include <NGIN/Time/MonotonicClock.hpp>
#include <NGIN/Time/TimePoint.hpp>
#include <NGIN/Units.hpp>
#include <NGIN/Utilities/Callable.hpp>

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
            return m_state.Get() != nullptr;
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
            other.m_index       = static_cast<UIntSize>(-1);
            other.m_armed.store(false, std::memory_order_relaxed);
            other.m_state.Reset();
        }

        Memory::Shared<detail::CancellationState> m_state {};
        NGIN::Execution::ExecutorRef               m_exec {};
        std::coroutine_handle<>                   m_handle {};
        CancellationCallback                      m_callback {nullptr};
        void*                                     m_callbackCtx {nullptr};
        UIntSize                               m_index {static_cast<UIntSize>(-1)};
        std::atomic<bool>                         m_armed {false};
    };

    // Cancellation primitives
    class CancellationToken
    {
    public:
        CancellationToken() = default;
        explicit CancellationToken(Memory::Shared<detail::CancellationState> state) noexcept
            : m_state(std::move(state))
        {
        }

        [[nodiscard]] bool HasState() const noexcept
        {
            return static_cast<bool>(m_state);
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
        Memory::Shared<detail::CancellationState> m_state {};
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
                NGIN::Sync::LockGuard guard(lock);
                registration->m_index = registrations.size();
                registrations.push_back(registration);
            }

            void Unregister(CancellationRegistration* registration) noexcept
            {
                if (!registration)
                {
                    return;
                }
                NGIN::Sync::LockGuard guard(lock);
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

                for (UIntSize i = 0; i < registrations.size(); ++i)
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
                    NGIN::Sync::LockGuard guard(lock);
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
            : m_state(Memory::MakeShared<detail::CancellationState>())
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

        void CancelAt(NGIN::Execution::ExecutorRef exec, NGIN::Time::TimePoint at) noexcept
        {
            if (IsCancellationRequested() || !exec.IsValid())
            {
                return;
            }

            auto state = m_state;
            exec.ExecuteAt(NGIN::Utilities::Callable<void()>([state]() noexcept { state->Cancel(); }), at);
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        void CancelAfter(NGIN::Execution::ExecutorRef exec, const TUnit& delay) noexcept
        {
            if (IsCancellationRequested() || !exec.IsValid())
            {
                return;
            }

            const auto nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(delay).GetValue();
            if (nsDouble <= 0.0)
            {
                Cancel();
                return;
            }

            const auto now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            auto       add = static_cast<NGIN::UInt64>(nsDouble);
            if (static_cast<double>(add) < nsDouble)
            {
                ++add;
            }
            CancelAt(exec, NGIN::Time::TimePoint::FromNanoseconds(now + add));
        }

    private:
        Memory::Shared<detail::CancellationState> m_state;
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
        if (!m_state)
        {
            return;
        }

        const bool wantsResume = exec.IsValid() && handle;
        if (!wantsResume && callback == nullptr)
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
                if (wantsResume)
                {
                    exec.Execute(handle);
                }
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

    namespace detail
    {
        [[nodiscard]] inline bool CancelLinkedSource(void* ctx) noexcept
        {
            auto* source = static_cast<CancellationSource*>(ctx);
            if (source)
            {
                source->Cancel();
            }
            return false;
        }

        struct LinkedCancellationState final
        {
            CancellationSource                    source {};
            std::vector<CancellationRegistration> registrations {};

            void Link(std::initializer_list<CancellationToken> tokens) noexcept
            {
                registrations.resize(tokens.size());

                UIntSize index = 0;
                for (const auto& token: tokens)
                {
                    if (token.IsCancellationRequested())
                    {
                        source.Cancel();
                        return;
                    }
                    token.Register(registrations[index++], {}, {}, &CancelLinkedSource, &source);
                }
            }
        };
    }// namespace detail

    /// @brief A cancellation source that is cancelled when any of the linked tokens are cancelled.
    ///
    /// This type owns the registrations required to link tokens together.
    class LinkedCancellationSource final
    {
    public:
        LinkedCancellationSource() = default;

        explicit LinkedCancellationSource(std::initializer_list<CancellationToken> tokens)
            : m_state(Memory::MakeShared<detail::LinkedCancellationState>())
        {
            m_state->Link(tokens);
        }

        template<typename... TTokens>
            requires(sizeof...(TTokens) > 0)
        explicit LinkedCancellationSource(const TTokens&... tokens)
            : LinkedCancellationSource({tokens...})
        {
        }

        [[nodiscard]] CancellationToken GetToken() const noexcept
        {
            if (!m_state)
            {
                return {};
            }
            return m_state->source.GetToken();
        }

        void Cancel() noexcept
        {
            if (m_state)
            {
                m_state->source.Cancel();
            }
        }

        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_state && m_state->source.IsCancellationRequested();
        }

    private:
        Memory::Shared<detail::LinkedCancellationState> m_state {};
    };

    /// @brief Convenience helper to create a linked cancellation source.
    [[nodiscard]] inline LinkedCancellationSource CreateLinkedTokenSource(std::initializer_list<CancellationToken> tokens)
    {
        return LinkedCancellationSource(tokens);
    }

    inline void CancellationRegistration::Reset() noexcept
    {
        if (!m_state)
        {
            return;
        }
        m_armed.store(false, std::memory_order_relaxed);
        m_state->Unregister(this);
        m_state.Reset();
        m_exec           = {};
        m_handle         = {};
        m_callback       = nullptr;
        m_callbackCtx    = nullptr;
        m_index          = static_cast<UIntSize>(-1);
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
