/// @file Cancellation.hpp
/// @brief Cancellation tokens, registrations, sources, and linked cancellation ownership.
#pragma once

#include <atomic>
#include <coroutine>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include <NGIN/Async/TaskCanceled.hpp>
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
    namespace detail
    {
        struct CancellationState;
    }// namespace detail

    /// @brief Callback invoked once when a cancellation registration fires.
    /// @return Whether the associated coroutine handle should also be resumed.
    using CancellationCallback = bool (*)(void*) noexcept;

    /// @brief Move-only ownership handle for one callback registered with a cancellation token.
    class CancellationRegistration final
    {
    public:
        /// @brief Constructs an empty registration.
        CancellationRegistration() noexcept = default;

        /// @brief Transfers ownership of an active registration.
        CancellationRegistration(CancellationRegistration&& other) noexcept
        {
            MoveFrom(std::move(other));
        }

        /// @brief Replaces this registration with another registration.
        CancellationRegistration& operator=(CancellationRegistration&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        /// @brief Registrations are non-copyable because one object owns callback removal.
        CancellationRegistration(const CancellationRegistration&) = delete;
        /// @brief Registrations are non-copy-assignable because one object owns callback removal.
        CancellationRegistration& operator=(const CancellationRegistration&) = delete;

        /// @brief Unregisters the callback when this object is destroyed.
        ~CancellationRegistration()
        {
            Reset();
        }

        /// @brief Unregisters the callback and returns this object to an empty state.
        void Reset() noexcept;

        /// @brief Returns whether this object owns a callback registration.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_state.Get() != nullptr;
        }

    private:
        friend struct detail::CancellationState;
        friend class CancellationToken;

        void Fire() noexcept;

        void MoveFrom(CancellationRegistration&& other) noexcept;

        Memory::Shared<detail::CancellationState> m_state {};
        NGIN::Execution::ExecutorRef              m_exec {};
        std::coroutine_handle<>                   m_handle {};
        CancellationCallback                      m_callback {nullptr};
        void*                                     m_callbackCtx {nullptr};
        UIntSize                                  m_index {static_cast<UIntSize>(-1)};
        std::atomic<bool>                         m_armed {false};
    };

    /// @brief Copyable observation handle for shared cancellation state.
    class CancellationToken
    {
    public:
        /// @brief Constructs a token with no cancellation state.
        CancellationToken() = default;
        /// @brief Constructs a token that observes shared cancellation state.
        explicit CancellationToken(Memory::Shared<detail::CancellationState> state) noexcept
            : m_state(std::move(state))
        {
        }

        /// @brief Returns whether this token is associated with cancellation state.
        [[nodiscard]] bool HasState() const noexcept
        {
            return static_cast<bool>(m_state);
        }

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] bool IsCancellationRequested() const noexcept;

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return IsCancellationRequested();
        }

        /// @brief Registers a callback and optional coroutine continuation for cancellation.
        void Register(CancellationRegistration&    outRegistration,
                      NGIN::Execution::ExecutorRef exec,
                      std::coroutine_handle<>      handle,
                      CancellationCallback         callback    = nullptr,
                      void*                        callbackCtx = nullptr) const noexcept;

    private:
        Memory::Shared<detail::CancellationState> m_state {};
        friend class CancellationSource;
    };

    namespace detail
    {
        struct CancellationState final
        {
            std::atomic<bool>                      canceled {false};
            NGIN::Sync::SpinLock                   lock {};
            std::vector<CancellationRegistration*> registrations {};

            CancellationState()
            {
                registrations.reserve(8);
            }

            [[nodiscard]] bool TryRegister(CancellationRegistration* registration) noexcept
            {
                if (!registration)
                {
                    return false;
                }
                NGIN::Sync::LockGuard guard(lock);
                if (canceled.load(std::memory_order_acquire))
                {
                    return false;
                }
                registration->m_index = registrations.size();
                registrations.push_back(registration);
                return true;
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
                        auto* moved                          = registrations[lastIndex];
                        registrations[registration->m_index] = moved;
                        moved->m_index                       = registration->m_index;
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
                            auto* moved      = registrations[lastIndex];
                            registrations[i] = moved;
                            moved->m_index   = i;
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
                    local.swap(registrations);
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

    /// @brief Owns mutable cancellation state and creates observation tokens.
    class CancellationSource
    {
    public:
        /// @brief Constructs a new, independently cancelable source.
        CancellationSource()
            : m_state(Memory::MakeShared<detail::CancellationState>())
        {
        }

        /// @brief Requests cancellation and fires registered callbacks once.
        void Cancel() noexcept
        {
            m_state->Cancel();
        }

        /// @brief Returns a token that observes this source.
        [[nodiscard]] CancellationToken GetToken() const noexcept
        {
            return CancellationToken(m_state);
        }

        /// @brief Returns whether cancellation has been requested.
        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_state->canceled.load(std::memory_order_acquire);
        }

        /// @brief Schedules cancellation at an absolute monotonic time.
        void CancelAt(NGIN::Execution::ExecutorRef exec, NGIN::Time::TimePoint at) noexcept
        {
            if (IsCancellationRequested() || !exec.IsValid())
            {
                return;
            }

            auto state = m_state;
            exec.ExecuteAt(NGIN::Utilities::Callable<void()>([state]() noexcept { state->Cancel(); }), at);
        }

        /// @brief Schedules cancellation after a duration.
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

    inline void CancellationToken::Register(CancellationRegistration&    outRegistration,
                                            NGIN::Execution::ExecutorRef exec,
                                            std::coroutine_handle<>      handle,
                                            CancellationCallback         callback,
                                            void*                        callbackCtx) const noexcept
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

        outRegistration.m_state       = m_state;
        outRegistration.m_exec        = exec;
        outRegistration.m_handle      = handle;
        outRegistration.m_callback    = callback;
        outRegistration.m_callbackCtx = callbackCtx;
        outRegistration.m_armed.store(true, std::memory_order_relaxed);

        if (m_state->TryRegister(&outRegistration))
        {
            return;
        }

        outRegistration.m_state.Reset();
        outRegistration.m_exec        = {};
        outRegistration.m_handle      = {};
        outRegistration.m_callback    = nullptr;
        outRegistration.m_callbackCtx = nullptr;
        outRegistration.m_index       = static_cast<UIntSize>(-1);
        outRegistration.m_armed.store(false, std::memory_order_relaxed);

        bool shouldResume = true;
        if (callback)
        {
            shouldResume = callback(callbackCtx);
        }
        if (shouldResume && wantsResume)
        {
            exec.Execute(handle);
        }
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
        /// @brief Constructs an empty linked source.
        LinkedCancellationSource() = default;

        /// @brief Links cancellation to any token in the supplied list.
        explicit LinkedCancellationSource(std::initializer_list<CancellationToken> tokens)
            : m_state(Memory::MakeShared<detail::LinkedCancellationState>())
        {
            m_state->Link(tokens);
        }

        /// @brief Links cancellation to any token in the supplied parameter pack.
        template<typename... TTokens>
            requires(sizeof...(TTokens) > 0)
        explicit LinkedCancellationSource(const TTokens&... tokens)
            : LinkedCancellationSource({tokens...})
        {
        }

        /// @brief Returns a token that observes the linked source.
        [[nodiscard]] CancellationToken GetToken() const noexcept
        {
            if (!m_state)
            {
                return {};
            }
            return m_state->source.GetToken();
        }

        /// @brief Explicitly requests cancellation of the linked source.
        void Cancel() noexcept
        {
            if (m_state)
            {
                m_state->source.Cancel();
            }
        }

        /// @brief Returns whether this source or any linked token requested cancellation.
        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return m_state && m_state->source.IsCancellationRequested();
        }

    private:
        Memory::Shared<detail::LinkedCancellationState> m_state {};
    };

    /// @brief Convenience helper to create a linked cancellation source.
    [[nodiscard]] inline LinkedCancellationSource CreateLinkedCancellationSource(std::initializer_list<CancellationToken> tokens)
    {
        return LinkedCancellationSource(tokens);
    }

    inline void CancellationRegistration::MoveFrom(CancellationRegistration&& other) noexcept
    {
        m_state       = std::move(other.m_state);
        m_exec        = other.m_exec;
        m_handle      = other.m_handle;
        m_callback    = other.m_callback;
        m_callbackCtx = other.m_callbackCtx;
        m_index       = other.m_index;
        m_armed.store(other.m_armed.exchange(false, std::memory_order_acq_rel), std::memory_order_relaxed);

        if (m_state)
        {
            NGIN::Sync::LockGuard guard(m_state->lock);
            if (m_index < m_state->registrations.size() && m_state->registrations[m_index] == &other)
            {
                m_state->registrations[m_index] = this;
            }
            else
            {
                for (UIntSize i = 0; i < m_state->registrations.size(); ++i)
                {
                    if (m_state->registrations[i] == &other)
                    {
                        m_state->registrations[i] = this;
                        m_index                   = i;
                        break;
                    }
                }
            }
        }

        other.m_exec        = {};
        other.m_handle      = {};
        other.m_callback    = nullptr;
        other.m_callbackCtx = nullptr;
        other.m_index       = static_cast<UIntSize>(-1);
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
        m_exec        = {};
        m_handle      = {};
        m_callback    = nullptr;
        m_callbackCtx = nullptr;
        m_index       = static_cast<UIntSize>(-1);
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
}// namespace NGIN::Async
