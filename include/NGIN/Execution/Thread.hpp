/// @file Thread.hpp
/// @brief OS-thread backed thread handle (no std::thread).
#pragma once

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
    class Thread
    {
    public:
        using NativeHandle = void*;
        using ThreadId = ThisThread::ThreadId;

        enum class OnDestruct : UInt8
        {
            Join,
            Detach,
            Terminate,
        };

        struct Options final
        {
            constexpr Options() noexcept = default;

            ThreadName name {};
            UInt64     affinityMask {0};
            int        priority {0};
            UIntSize   stackSize {0};
            OnDestruct onDestruct {OnDestruct::Terminate};
        };

        Thread() noexcept = default;

        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        explicit Thread(F&& entry, Options options = {})
        {
            Start(std::forward<F>(entry), options);
        }

        explicit Thread(NGIN::Utilities::Callable<void()> entry, Options options = {})
        {
            Start(std::move(entry), options);
        }

        ~Thread() noexcept;

        Thread(const Thread&)            = delete;
        Thread& operator=(const Thread&) = delete;

        Thread(Thread&& other) noexcept;

        Thread& operator=(Thread&& other) noexcept;

        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        void Start(F&& entry, Options options = {})
        {
            Start(NGIN::Utilities::Callable<void()>(std::forward<F>(entry)), options);
        }

        void Start(NGIN::Utilities::Callable<void()> entry, Options options = {});

        void Join() noexcept;
        void Detach() noexcept;

        [[nodiscard]] bool IsJoinable() const noexcept
        {
            return m_joinable;
        }

        [[nodiscard]] ThreadId GetId() const noexcept;

        [[nodiscard]] NativeHandle NativeHandleValue() noexcept;

        [[nodiscard]] bool SetName(ThreadName name) noexcept;

        [[nodiscard]] bool SetAffinity(UInt64 affinityMask) noexcept;

        [[nodiscard]] bool SetPriority(int priority) noexcept;

    private:
        void StartImpl(NGIN::Utilities::Callable<void()> entry);
        void MoveFrom(Thread&& other) noexcept;
        void HandleDestruction() noexcept;

        Options               m_options {};
        bool                  m_joinable {false};
        std::atomic<ThreadId> m_threadId {0};
        void* m_handle {nullptr};
    };

    class WorkerThread final
    {
    public:
        WorkerThread() noexcept = default;

        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        explicit WorkerThread(F&& entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::forward<F>(entry), options);
        }

        explicit WorkerThread(NGIN::Utilities::Callable<void()> entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::move(entry), options);
        }

        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        void Start(F&& entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::forward<F>(entry), options);
        }

        void Start(NGIN::Utilities::Callable<void()> entry, Thread::Options options = {})
        {
            options.onDestruct = Thread::OnDestruct::Join;
            m_thread.Start(std::move(entry), options);
        }

        void               Join() noexcept { m_thread.Join(); }
        void               Detach() noexcept { m_thread.Detach(); }
        [[nodiscard]] bool IsJoinable() const noexcept { return m_thread.IsJoinable(); }

        [[nodiscard]] Thread::ThreadId GetId() const noexcept { return m_thread.GetId(); }
        [[nodiscard]] bool             SetName(ThreadName name) noexcept { return m_thread.SetName(name); }
        [[nodiscard]] bool             SetAffinity(UInt64 mask) noexcept { return m_thread.SetAffinity(mask); }
        [[nodiscard]] bool             SetPriority(int priority) noexcept { return m_thread.SetPriority(priority); }

        [[nodiscard]] Thread::NativeHandle NativeHandleValue() noexcept { return m_thread.NativeHandleValue(); }

    private:
        Thread m_thread {};
    };
}// namespace NGIN::Execution
