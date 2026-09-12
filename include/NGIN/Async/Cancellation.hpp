/// @file Cancellation.hpp
/// @brief Cancellation tokens, registrations, sources, and linked cancellation ownership.
#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <expected>
#include <initializer_list>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <thread>
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
        struct CancellationNode;
    }// namespace detail

    /// @brief Callback invoked once when a cancellation registration fires.
    /// @return Whether the associated coroutine handle should also be resumed.
    using CancellationCallback = bool (*)(void*) noexcept;

    /// @brief Reason a cancellation callback could not be registered.
    enum class CancellationRegistrationError : std::uint8_t
    {
        /// @brief Neither a callback nor a resumable coroutine was supplied.
        InvalidTarget,
        /// @brief Stable registration-node allocation failed.
        ResourceExhausted,
        /// @brief The executor cannot reserve continuation delivery (unsupported, stopped, or rejected).
        CompletionUnavailable,
    };

    /// @brief Success or a recoverable cancellation-registration failure.
    using CancellationRegistrationResult = std::expected<void, CancellationRegistrationError>;

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
        /// @details Joins an invocation on another thread, including its completion handoff.
        /// A continuation already handed to its executor must still be joined by its owner.
        void Reset() noexcept;

        /// @brief Returns whether this object owns a callback registration.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_node);
        }

    private:
        friend struct detail::CancellationState;
        friend class CancellationToken;

        void MoveFrom(CancellationRegistration&& other) noexcept;

        Memory::Shared<detail::CancellationState> m_state {};
        std::shared_ptr<detail::CancellationNode> m_node {};
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
        /// @details A supplied coroutine requires a valid executor with completion reservations.
        /// Delivery is reserved before registration and never resumes inline. The caller owns
        /// the coroutine lifetime through delivery and keeps its executor operational until then.
        [[nodiscard]] CancellationRegistrationResult Register(
                CancellationRegistration&    outRegistration,
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
        enum class CancellationNodeStatus : std::uint8_t
        {
            Registered,
            Invoking,
            Unregistered,
            Completed,
        };

        /// @brief Stable state-owned callback node shared by cancellation and registration handles.
        struct CancellationNode final
        {
            NGIN::Execution::CompletionReservation delivery {};
            CancellationCallback                   callback {nullptr};
            void*                                  callbackContext {nullptr};

            std::mutex              mutex {};
            std::condition_variable completedCondition {};
            CancellationNodeStatus  status {CancellationNodeStatus::Registered};
            std::thread::id         invokingThread {};

            void Invoke() noexcept
            {
                NGIN::Execution::CompletionReservation continuation;
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    if (status != CancellationNodeStatus::Registered)
                        return;
                    status         = CancellationNodeStatus::Invoking;
                    invokingThread = std::this_thread::get_id();
                    continuation   = std::move(delivery);
                }

                bool shouldResume = true;
                if (callback)
                    shouldResume = callback(callbackContext);

                // Complete the handoff before Reset on another thread returns.
                // The callback may reset its own registration; this local ticket
                // remains valid until its decision to resume has been consumed.
                if (shouldResume)
                    continuation.Dispatch();
                else
                    continuation.Reset();

                {
                    std::lock_guard<std::mutex> guard(mutex);
                    status         = CancellationNodeStatus::Completed;
                    invokingThread = {};
                }
                completedCondition.notify_all();
            }

            /// @brief Prevents future invocation and waits for a callback already running on another thread.
            /// @details Self-unregistration returns immediately; the cancellation pass retains node lifetime.
            void Unregister() noexcept
            {
                std::unique_lock<std::mutex> guard(mutex);
                if (status == CancellationNodeStatus::Registered)
                {
                    status      = CancellationNodeStatus::Unregistered;
                    auto unused = std::move(delivery);
                    guard.unlock();
                    unused.Reset();
                    return;
                }
                if (status == CancellationNodeStatus::Invoking && invokingThread != std::this_thread::get_id())
                {
                    completedCondition.wait(guard, [this] {
                        return status != CancellationNodeStatus::Invoking;
                    });
                }
            }
        };

        struct CancellationState final
        {
            explicit CancellationState(std::pmr::memory_resource* memoryResource)
                : resource(memoryResource ? memoryResource : std::pmr::get_default_resource()), registrations(resource)
            {
                registrations.reserve(8);
            }

            std::atomic<bool>                                   canceled {false};
            NGIN::Sync::SpinLock                                lock {};
            std::pmr::memory_resource*                          resource;
            std::pmr::vector<std::shared_ptr<CancellationNode>> registrations;

            [[nodiscard]] bool TryRegister(const std::shared_ptr<CancellationNode>& node)
            {
                NGIN::Sync::LockGuard guard(lock);
                if (canceled.load(std::memory_order_acquire))
                    return false;
                registrations.push_back(node);
                return true;
            }

            void Unregister(const std::shared_ptr<CancellationNode>& node) noexcept
            {
                if (!node)
                    return;
                NGIN::Sync::LockGuard guard(lock);
                for (std::size_t index = 0; index < registrations.size(); ++index)
                {
                    if (registrations[index] == node)
                    {
                        registrations[index] = std::move(registrations.back());
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

                std::pmr::vector<std::shared_ptr<CancellationNode>> local(resource);
                {
                    NGIN::Sync::LockGuard guard(lock);
                    local.swap(registrations);
                }

                for (const std::shared_ptr<CancellationNode>& node: local)
                {
                    if (node)
                        node->Invoke();
                }
            }
        };
    }// namespace detail

    /// @brief Owns mutable cancellation state and creates observation tokens.
    class CancellationSource
    {
    public:
        /// @brief Constructs a new, independently cancelable source.
        /// @param memoryResource Resource used by registration nodes and the state registration table. It must
        /// outlive the source and every token or registration created from it.
        explicit CancellationSource(std::pmr::memory_resource* memoryResource = std::pmr::get_default_resource())
            : m_state(Memory::MakeShared<detail::CancellationState>(memoryResource))
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
        /// @return Submission status; an already-canceled source is a successful no-op.
        [[nodiscard]] NGIN::Execution::ScheduleResult CancelAt(
                NGIN::Execution::ExecutorRef exec,
                NGIN::Time::TimePoint        at) noexcept
        {
            if (IsCancellationRequested())
                return {};
            if (!exec.IsValid())
                return std::unexpected(NGIN::Execution::ScheduleError::InvalidExecutor);

            Memory::Shared<detail::CancellationState> state = m_state;
            return exec.ExecuteAt(NGIN::Utilities::Callable<void()>([state]() noexcept { state->Cancel(); }), at);
        }

        /// @brief Schedules cancellation after a duration.
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] NGIN::Execution::ScheduleResult CancelAfter(
                NGIN::Execution::ExecutorRef exec,
                const TUnit&                 delay) noexcept
        {
            if (IsCancellationRequested())
                return {};
            if (!exec.IsValid())
                return std::unexpected(NGIN::Execution::ScheduleError::InvalidExecutor);

            const double nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(delay).GetValue();
            if (nsDouble <= 0.0)
            {
                Cancel();
                return {};
            }

            const NGIN::UInt64 now = NGIN::Time::MonotonicClock::Now().ToNanoseconds();
            NGIN::UInt64       add = static_cast<NGIN::UInt64>(nsDouble);
            if (static_cast<double>(add) < nsDouble)
            {
                ++add;
            }
            const NGIN::UInt64 maximum = (std::numeric_limits<NGIN::UInt64>::max)();
            const NGIN::UInt64 target  = add > maximum - now ? maximum : now + add;
            return CancelAt(exec, NGIN::Time::TimePoint::FromNanoseconds(target));
        }

    private:
        Memory::Shared<detail::CancellationState> m_state;
    };

    inline bool CancellationToken::IsCancellationRequested() const noexcept
    {
        return m_state && m_state->canceled.load(std::memory_order_acquire);
    }

    inline CancellationRegistrationResult CancellationToken::Register(
            CancellationRegistration&    outRegistration,
            NGIN::Execution::ExecutorRef exec,
            std::coroutine_handle<>      handle,
            CancellationCallback         callback,
            void*                        callbackCtx) const noexcept
    {
        outRegistration.Reset();
        if (!m_state)
            return {};

        const bool wantsResume = static_cast<bool>(handle);
        if (wantsResume && !exec.IsValid())
            return std::unexpected(CancellationRegistrationError::InvalidTarget);
        if (!wantsResume && callback == nullptr)
            return std::unexpected(CancellationRegistrationError::InvalidTarget);

        std::shared_ptr<detail::CancellationNode> node;
        Memory::Shared<detail::CancellationState> state = m_state;
        try
        {
            node = std::allocate_shared<detail::CancellationNode>(
                    std::pmr::polymorphic_allocator<detail::CancellationNode> {state->resource});
            node->callback        = callback;
            node->callbackContext = callbackCtx;
            if (wantsResume)
            {
                auto delivery = exec.ReserveCompletion(NGIN::Execution::WorkItem(handle));
                if (!delivery)
                    return std::unexpected(delivery.error() == NGIN::Execution::ScheduleError::ResourceExhausted
                                                   ? CancellationRegistrationError::ResourceExhausted
                                                   : CancellationRegistrationError::CompletionUnavailable);
                node->delivery = std::move(*delivery);
            }

            // Publish ownership before making the callback visible. Cancellation
            // may run it immediately, reset the registration, and destroy its owner.
            // Do not access outRegistration again after successful publication.
            outRegistration.m_state = state;
            outRegistration.m_node  = node;
            if (state->TryRegister(node))
                return {};
        } catch (const std::bad_alloc&)
        {
            outRegistration.Reset();
            return std::unexpected(CancellationRegistrationError::ResourceExhausted);
        }

        // Cancellation won registration under the state lock. Invoke the stable node immediately.
        node->Invoke();
        return {};
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

            void Link(std::initializer_list<CancellationToken> tokens)
            {
                registrations.resize(tokens.size());

                UIntSize index = 0;
                for (const CancellationToken& token: tokens)
                {
                    if (token.IsCancellationRequested())
                    {
                        source.Cancel();
                        return;
                    }
                    const CancellationRegistrationResult result =
                            token.Register(registrations[index++], {}, {}, &CancelLinkedSource, &source);
                    if (!result && result.error() == CancellationRegistrationError::ResourceExhausted)
                        throw std::bad_alloc {};
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
        m_state = std::move(other.m_state);
        m_node  = std::move(other.m_node);
    }

    inline void CancellationRegistration::Reset() noexcept
    {
        if (!m_node)
            return;

        Memory::Shared<detail::CancellationState> state = std::move(m_state);
        std::shared_ptr<detail::CancellationNode> node  = std::move(m_node);
        if (state)
            state->Unregister(node);
        node->Unregister();
    }
}// namespace NGIN::Async
