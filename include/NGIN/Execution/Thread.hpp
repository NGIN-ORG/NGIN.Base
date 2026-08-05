/// @file Thread.hpp
/// @brief OS-thread backed thread handle (no std::thread).
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Execution/Config.hpp>
#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Execution/ThreadName.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

#include <NGIN/Utilities/Callable.hpp>

namespace NGIN::Execution
{
    /// @brief Move-only operating-system thread handle with configurable destruction policy.
    class NGIN_EXECUTION_API Thread
    {
    public:
        /// @brief Native platform thread-handle representation.
        using NativeHandle = void*;
        /// @brief Stable numeric thread identifier.
        using ThreadId = ThisThread::ThreadId;

        /// @brief Action performed when a joinable thread object is destroyed.
        enum class OnDestruct : UInt8
        {
            Join,
            Detach,
            Terminate,
        };

        /// @brief Thread creation and destruction options.
        struct Options final
        {
            /// @brief Constructs a thread option set.
            constexpr Options(
                    ThreadName threadName = {},
                    UInt64     mask       = 0,
                    int        prio       = 0,
                    UIntSize   stack      = 0,
                    OnDestruct destruct   = OnDestruct::Terminate) noexcept
                : name {threadName},
                  affinityMask {mask},
                  priority {prio},
                  stackSize {stack},
                  onDestruct {destruct}
            {
            }

            ThreadName name;
            UInt64     affinityMask;
            int        priority;
            UIntSize   stackSize;
            OnDestruct onDestruct;
        };

        /// @brief Constructs an empty, non-joinable thread handle.
        Thread() noexcept = default;

        /// @brief Starts a new thread from an invocable object.
        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        explicit Thread(F&& entry, Options options = {})
        {
            Start(std::forward<F>(entry), options);
        }

        /// @brief Starts a new thread from a type-erased callable.
        explicit Thread(NGIN::Utilities::Callable<void()> entry, Options options = {})
        {
            Start(std::move(entry), options);
        }

        /// @brief Applies the configured destruction policy when still joinable.
        ~Thread() noexcept;

        /// @brief Thread handles are non-copyable.
        Thread(const Thread&) = delete;
        /// @brief Thread handles are non-copy-assignable.
        Thread& operator=(const Thread&) = delete;

        /// @brief Transfers ownership of a native thread handle.
        Thread(Thread&& other) noexcept;

        /// @brief Applies this handle's destruction policy and transfers another handle.
        Thread& operator=(Thread&& other) noexcept;

        /// @brief Starts an empty thread handle from an invocable object.
        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        void Start(F&& entry, Options options = {})
        {
            Start(NGIN::Utilities::Callable<void()>(std::forward<F>(entry)), options);
        }

        /// @brief Starts an empty thread handle from a type-erased callable.
        /// @warning Terminates the process if this handle is already joinable or `entry` is empty.
        void Start(NGIN::Utilities::Callable<void()> entry, Options options = {});

        /// @brief Blocks until the thread exits and makes the handle non-joinable.
        void Join() noexcept;
        /// @brief Releases ownership without waiting and makes the handle non-joinable.
        void Detach() noexcept;

        /// @brief Returns whether this object owns a thread that must be joined or detached.
        [[nodiscard]] bool IsJoinable() const noexcept
        {
            return m_joinable;
        }

        /// @brief Returns the started thread's numeric identifier, or zero when unavailable.
        [[nodiscard]] ThreadId GetId() const noexcept;

        /// @brief Returns the native platform handle value.
        [[nodiscard]] NativeHandle NativeHandleValue() noexcept;

        /// @brief Attempts to set the running thread's diagnostic name.
        [[nodiscard]] bool SetName(ThreadName name) noexcept;

        /// @brief Attempts to restrict the running thread to a processor affinity mask.
        [[nodiscard]] bool SetAffinity(UInt64 affinityMask) noexcept;

        /// @brief Attempts to set the running thread's platform priority.
        [[nodiscard]] bool SetPriority(int priority) noexcept;

    private:
        void StartImpl(NGIN::Utilities::Callable<void()> entry);
        void MoveFrom(Thread&& other) noexcept;
        void HandleDestruction() noexcept;

        Options               m_options {};
        bool                  m_joinable {false};
        std::atomic<ThreadId> m_threadId {0};
        void*                 m_handle {nullptr};
    };

    /// @brief Thread wrapper whose destruction policy is always `Join`.
    class WorkerThread final
    {
    public:
        /// @brief Constructs an empty worker thread.
        WorkerThread() noexcept = default;

        /// @brief Starts a joining worker thread from an invocable object.
        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        explicit WorkerThread(F&& entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::forward<F>(entry), options);
        }

        /// @brief Starts a joining worker thread from a type-erased callable.
        explicit WorkerThread(NGIN::Utilities::Callable<void()> entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::move(entry), options);
        }

        /// @brief Starts an empty worker thread from an invocable object.
        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        void Start(F&& entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::forward<F>(entry), options);
        }

        /// @brief Starts an empty worker thread from a type-erased callable.
        void Start(NGIN::Utilities::Callable<void()> entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::move(entry), options);
        }

        /// @brief Waits for the worker to exit.
        void Join() noexcept { m_thread.Join(); }
        /// @brief Detaches the worker from this handle.
        void Detach() noexcept { m_thread.Detach(); }
        /// @brief Returns whether the worker is joinable.
        [[nodiscard]] bool IsJoinable() const noexcept { return m_thread.IsJoinable(); }

        /// @brief Returns the worker thread's numeric identifier.
        [[nodiscard]] Thread::ThreadId GetId() const noexcept { return m_thread.GetId(); }
        /// @brief Attempts to set the worker thread's name.
        [[nodiscard]] bool SetName(ThreadName name) noexcept { return m_thread.SetName(name); }
        /// @brief Attempts to set the worker thread's affinity mask.
        [[nodiscard]] bool SetAffinity(UInt64 mask) noexcept { return m_thread.SetAffinity(mask); }
        /// @brief Attempts to set the worker thread's platform priority.
        [[nodiscard]] bool SetPriority(int priority) noexcept { return m_thread.SetPriority(priority); }

        /// @brief Returns the worker's native platform handle value.
        [[nodiscard]] Thread::NativeHandle NativeHandleValue() noexcept { return m_thread.NativeHandleValue(); }

    private:
        Thread m_thread {};
    };
}// namespace NGIN::Execution
