/// @file Thread.hpp
/// @brief OS-thread backed thread handle (no std::thread).
#pragma once

#include <NGIN/Execution/Config.hpp>
#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Execution/ThreadName.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstring>
#include <cstdint>
#include <exception>
#include <string_view>
#include <type_traits>
#include <utility>

#include <NGIN/Utilities/Callable.hpp>

#if defined(_WIN32)
extern "C"
{
    __declspec(dllimport) void* __stdcall GetCurrentThread();
    __declspec(dllimport) int __stdcall CloseHandle(void* hObject);
    __declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void* hHandle, unsigned long dwMilliseconds);

    __declspec(dllimport) void* __cdecl _beginthreadex(
            void* security,
            unsigned stackSize,
            unsigned(__stdcall* startAddress)(void*),
            void* argList,
            unsigned initFlag,
            unsigned* threadAddr);

    __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
    __declspec(dllimport) int __stdcall SwitchToThread();

    __declspec(dllimport) long __stdcall SetThreadDescription(void* hThread, const wchar_t* lpThreadDescription);
    __declspec(dllimport) int __stdcall MultiByteToWideChar(
            unsigned int codePage,
            unsigned long dwFlags,
            const char* lpMultiByteStr,
            int cbMultiByte,
            wchar_t* lpWideCharStr,
            int cchWideChar);

    __declspec(dllimport) std::uintptr_t __stdcall SetThreadAffinityMask(void* hThread, std::uintptr_t dwThreadAffinityMask);
    __declspec(dllimport) int __stdcall SetThreadPriority(void* hThread, int nPriority);
}
#else
#include <pthread.h>
#include <sched.h>
#if defined(__linux__)
#include <errno.h>
#endif
#endif

namespace NGIN::Execution
{
    class Thread
    {
    public:
#if defined(_WIN32)
        using NativeHandle = void*;
#else
        using NativeHandle = pthread_t;
#endif
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

        ~Thread() noexcept
        {
            HandleDestruction();
        }

        Thread(const Thread&)            = delete;
        Thread& operator=(const Thread&) = delete;

        Thread(Thread&& other) noexcept
        {
            MoveFrom(std::move(other));
        }

        Thread& operator=(Thread&& other) noexcept
        {
            if (this != &other)
            {
                HandleDestruction();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        template<typename F>
            requires(std::invocable<std::remove_reference_t<F>&> &&
                     std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>)
        void Start(F&& entry, Options options = {})
        {
            Start(NGIN::Utilities::Callable<void()>(std::forward<F>(entry)), options);
        }

        void Start(NGIN::Utilities::Callable<void()> entry, Options options = {})
        {
            if (IsJoinable())
            {
                std::terminate();
            }
            if (!entry)
            {
                std::terminate();
            }

            m_options = options;
            StartImpl(std::move(entry));
        }

        void Join() noexcept
        {
            if (!IsJoinable())
            {
                return;
            }

#if defined(_WIN32)
            constexpr unsigned long INFINITE = 0xFFFFFFFFul;
            (void) ::WaitForSingleObject(m_handle, INFINITE);
            (void) ::CloseHandle(m_handle);
            m_handle   = nullptr;
            m_threadId.store(0, std::memory_order_release);
            m_joinable = false;
#else
            (void) ::pthread_join(m_thread, nullptr);
            m_threadId.store(0, std::memory_order_release);
            m_joinable = false;
#endif
        }

        void Detach() noexcept
        {
            if (!IsJoinable())
            {
                return;
            }

#if defined(_WIN32)
            (void) ::CloseHandle(m_handle);
            m_handle   = nullptr;
            m_threadId.store(0, std::memory_order_release);
            m_joinable = false;
#else
            (void) ::pthread_detach(m_thread);
            m_threadId.store(0, std::memory_order_release);
            m_joinable = false;
#endif
        }

        [[nodiscard]] bool IsJoinable() const noexcept
        {
            return m_joinable;
        }

        [[nodiscard]] ThreadId GetId() const noexcept
        {
            const auto id = m_threadId.load(std::memory_order_acquire);
            if (id != 0)
            {
                return id;
            }

#if !defined(_WIN32)
            // Fallback only; prefer the OS id captured inside the thread proc.
            ThreadId out = 0;
            const auto bytes = std::min<std::size_t>(sizeof(out), sizeof(m_thread));
            std::memcpy(&out, &m_thread, bytes);
            return out;
#else
            return 0;
#endif
        }

        [[nodiscard]] NativeHandle NativeHandleValue() noexcept
        {
#if defined(_WIN32)
            return m_handle;
#else
            return m_thread;
#endif
        }

        [[nodiscard]] bool SetName(ThreadName name) noexcept
        {
            if (name.Empty() || !IsJoinable())
            {
                return false;
            }

#if defined(_WIN32)
            std::array<wchar_t, 64> wide {};
            if (!Utf8ToWide(name.View(), wide))
            {
                return false;
            }
            return ::SetThreadDescription(m_handle, wide.data()) >= 0;
#elif defined(__linux__)
            std::array<char, 16> truncated {};
            TruncateForPthreadName(name.View(), truncated);
            return ::pthread_setname_np(m_thread, truncated.data()) == 0;
#else
            (void) name;
            return false;
#endif
        }

        [[nodiscard]] bool SetAffinity(UInt64 affinityMask) noexcept
        {
            if (affinityMask == 0 || !IsJoinable())
            {
                return false;
            }

#if defined(_WIN32)
            return ::SetThreadAffinityMask(m_handle, static_cast<std::uintptr_t>(affinityMask)) != 0;
#elif defined(__linux__)
            cpu_set_t set {};
            CPU_ZERO(&set);
            for (int bit = 0; bit < static_cast<int>(sizeof(UInt64) * 8); ++bit)
            {
                if ((affinityMask & (1ull << static_cast<UInt64>(bit))) != 0)
                {
                    CPU_SET(bit, &set);
                }
            }
            return ::pthread_setaffinity_np(m_thread, sizeof(set), &set) == 0;
#else
            (void) affinityMask;
            return false;
#endif
        }

        [[nodiscard]] bool SetPriority(int priority) noexcept
        {
            if (!IsJoinable())
            {
                return false;
            }

#if defined(_WIN32)
            return ::SetThreadPriority(m_handle, priority) != 0;
#else
            (void) priority;
            return false;
#endif
        }

    private:
        struct StartContext final
        {
            NGIN::Utilities::Callable<void()> entry {};
            ThreadName                        name {};
            UInt64                            affinityMask {0};
            int                               priority {0};
            std::atomic<ThreadId>*            outThreadId {nullptr};
        };

        static constexpr void TruncateForPthreadName(std::string_view name, std::array<char, 16>& out) noexcept
        {
            const auto len = std::min<std::size_t>(name.size(), out.size() - 1);
            for (std::size_t i = 0; i < len; ++i)
            {
                out[i] = name[i];
            }
            out[len] = '\0';
        }

        static bool Utf8ToWide(std::string_view utf8, std::array<wchar_t, 64>& out) noexcept
        {
#if defined(_WIN32)
            constexpr unsigned int  CP_UTF8 = 65001u;
            constexpr unsigned long Flags   = 0ul;
            const auto              srcLen  = static_cast<int>(std::min<std::size_t>(utf8.size(), out.size() - 1));
            const int               written =
                    ::MultiByteToWideChar(CP_UTF8, Flags, utf8.data(), srcLen, out.data(), static_cast<int>(out.size() - 1));
            if (written <= 0)
            {
                return false;
            }
            out[static_cast<std::size_t>(written)] = L'\0';
            return true;
#else
            (void) utf8;
            (void) out;
            return false;
#endif
        }

#if defined(_WIN32)
        static unsigned __stdcall ThreadProc(void* param) noexcept
        {
            auto* ctx = static_cast<StartContext*>(param);
            if (ctx->outThreadId)
            {
                ctx->outThreadId->store(static_cast<ThreadId>(::GetCurrentThreadId()), std::memory_order_release);
            }
            if (!ctx->name.Empty())
            {
                std::array<wchar_t, 64> wide {};
                if (Utf8ToWide(ctx->name.View(), wide))
                {
                    (void) ::SetThreadDescription(::GetCurrentThread(), wide.data());
                }
            }

            if (ctx->affinityMask != 0)
            {
                (void) ::SetThreadAffinityMask(::GetCurrentThread(), static_cast<std::uintptr_t>(ctx->affinityMask));
            }
            if (ctx->priority != 0)
            {
                (void) ::SetThreadPriority(::GetCurrentThread(), ctx->priority);
            }

            try
            {
                ctx->entry();
            } catch (...)
            {
                std::terminate();
            }

            delete ctx;
            return 0;
        }
#else
        static void* ThreadProc(void* param) noexcept
        {
            auto* ctx = static_cast<StartContext*>(param);
            if (ctx->outThreadId)
            {
                ctx->outThreadId->store(NGIN::Execution::ThisThread::GetId(), std::memory_order_release);
            }
            if (!ctx->name.Empty())
            {
                (void) NGIN::Execution::ThisThread::SetName(ctx->name.View());
            }

#if defined(__linux__)
            if (ctx->affinityMask != 0)
            {
                cpu_set_t set {};
                CPU_ZERO(&set);
                for (int bit = 0; bit < static_cast<int>(sizeof(UInt64) * 8); ++bit)
                {
                    if ((ctx->affinityMask & (1ull << static_cast<UInt64>(bit))) != 0)
                    {
                        CPU_SET(bit, &set);
                    }
                }
                (void) ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
            }
#endif

            (void) ctx->priority;

            try
            {
                ctx->entry();
            } catch (...)
            {
                std::terminate();
            }

            delete ctx;
            return nullptr;
        }
#endif

        void StartImpl(NGIN::Utilities::Callable<void()> entry)
        {
            auto* ctx          = new StartContext();
            ctx->entry         = std::move(entry);
            ctx->name          = m_options.name;
            ctx->affinityMask  = m_options.affinityMask;
            ctx->priority      = m_options.priority;
            ctx->outThreadId   = &m_threadId;
            m_threadId.store(0, std::memory_order_release);

#if defined(_WIN32)
            unsigned threadId = 0;
            const auto stackSize = static_cast<unsigned>(m_options.stackSize);
            void*      handle =
                    ::_beginthreadex(nullptr, stackSize, &ThreadProc, ctx, 0u, &threadId);
            if (!handle)
            {
                delete ctx;
                std::terminate();
            }
            m_handle   = handle;
            m_threadId.store(static_cast<ThreadId>(threadId), std::memory_order_release);
            m_joinable = true;
#else
            pthread_attr_t attr {};
            (void) ::pthread_attr_init(&attr);
            if (m_options.stackSize != 0)
            {
                (void) ::pthread_attr_setstacksize(&attr, m_options.stackSize);
            }

            const int rc = ::pthread_create(&m_thread, &attr, &ThreadProc, ctx);
            (void) ::pthread_attr_destroy(&attr);
            if (rc != 0)
            {
                delete ctx;
                std::terminate();
            }
            m_joinable = true;
#endif
        }

        void MoveFrom(Thread&& other) noexcept
        {
            m_options  = other.m_options;
            m_joinable = other.m_joinable;
            other.m_joinable = false;

#if defined(_WIN32)
            m_handle         = other.m_handle;
            other.m_handle   = nullptr;
#else
            m_thread = other.m_thread;
            other.m_thread = {};
#endif
            m_threadId.store(other.m_threadId.load(std::memory_order_acquire), std::memory_order_release);
            other.m_threadId.store(0, std::memory_order_release);
            other.m_options = {};
        }

        void HandleDestruction() noexcept
        {
            if (!IsJoinable())
            {
                return;
            }

            switch (m_options.onDestruct)
            {
                case OnDestruct::Join: Join(); return;
                case OnDestruct::Detach: Detach(); return;
                default: std::terminate();
            }
        }

        Options m_options {};
        bool    m_joinable {false};
        std::atomic<ThreadId> m_threadId {0};

#if defined(_WIN32)
        void*    m_handle {nullptr};
#else
        pthread_t m_thread {};
#endif
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

        void Join() noexcept { m_thread.Join(); }
        void Detach() noexcept { m_thread.Detach(); }
        [[nodiscard]] bool IsJoinable() const noexcept { return m_thread.IsJoinable(); }

        [[nodiscard]] Thread::ThreadId GetId() const noexcept { return m_thread.GetId(); }
        [[nodiscard]] bool SetName(ThreadName name) noexcept { return m_thread.SetName(name); }
        [[nodiscard]] bool SetAffinity(UInt64 mask) noexcept { return m_thread.SetAffinity(mask); }
        [[nodiscard]] bool SetPriority(int priority) noexcept { return m_thread.SetPriority(priority); }

        [[nodiscard]] Thread::NativeHandle NativeHandleValue() noexcept { return m_thread.NativeHandleValue(); }

    private:
        Thread m_thread {};
    };
}// namespace NGIN::Execution
