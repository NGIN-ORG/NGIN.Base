#include <NGIN/Execution/Thread.hpp>

#include <pthread.h>

#if defined(__linux__)
#include <sched.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>

namespace NGIN::Execution
{
    namespace
    {
        struct StartContext final
        {
            NGIN::Utilities::Callable<void()> entry {};
            ThreadName                        name {};
            UInt64                            affinityMask {0};
            int                               priority {0};
            std::atomic<Thread::ThreadId>*    outThreadId {nullptr};
        };

        constexpr void TruncateForPthreadName(std::string_view name, std::array<char, 16>& output) noexcept
        {
            const auto length = std::min<std::size_t>(name.size(), output.size() - 1);
            for (std::size_t i = 0; i < length; ++i)
            {
                output[i] = name[i];
            }
            output[length] = '\0';
        }

        pthread_t* GetNativeThreadHandle(void* handle) noexcept
        {
            return static_cast<pthread_t*>(handle);
        }

        void* ThreadProc(void* parameter) noexcept
        {
            auto* context = static_cast<StartContext*>(parameter);
            if (context->outThreadId != nullptr)
            {
                context->outThreadId->store(NGIN::Execution::ThisThread::GetId(), std::memory_order_release);
            }
            if (!context->name.Empty())
            {
                (void)NGIN::Execution::ThisThread::SetName(context->name.View());
            }

#if defined(__linux__)
            if (context->affinityMask != 0)
            {
                cpu_set_t set {};
                CPU_ZERO(&set);
                for (std::size_t bit = 0; bit < (sizeof(UInt64) * 8u); ++bit)
                {
                    if ((context->affinityMask & (1ull << static_cast<UInt64>(bit))) != 0)
                    {
                        CPU_SET(bit, &set);
                    }
                }
                (void)::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
            }
#endif

            (void)context->priority;

            try
            {
                context->entry();
            } catch (...)
            {
                std::terminate();
            }

            delete context;
            return nullptr;
        }
    }// namespace

    Thread::~Thread() noexcept
    {
        HandleDestruction();
    }

    Thread::Thread(Thread&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    Thread& Thread::operator=(Thread&& other) noexcept
    {
        if (this != &other)
        {
            HandleDestruction();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    void Thread::Start(NGIN::Utilities::Callable<void()> entry, Options options)
    {
        if (IsJoinable() || !entry)
        {
            std::terminate();
        }

        m_options = options;
        StartImpl(std::move(entry));
    }

    void Thread::Join() noexcept
    {
        if (!IsJoinable())
        {
            return;
        }

        auto* nativeHandle = GetNativeThreadHandle(m_handle);
        (void)::pthread_join(*nativeHandle, nullptr);
        delete nativeHandle;
        m_handle = nullptr;
        m_threadId.store(0, std::memory_order_release);
        m_joinable = false;
    }

    void Thread::Detach() noexcept
    {
        if (!IsJoinable())
        {
            return;
        }

        auto* nativeHandle = GetNativeThreadHandle(m_handle);
        (void)::pthread_detach(*nativeHandle);
        delete nativeHandle;
        m_handle = nullptr;
        m_threadId.store(0, std::memory_order_release);
        m_joinable = false;
    }

    Thread::ThreadId Thread::GetId() const noexcept
    {
        const auto threadId = m_threadId.load(std::memory_order_acquire);
        if (threadId != 0)
        {
            return threadId;
        }

        const auto* nativeHandle = GetNativeThreadHandle(m_handle);
        if (nativeHandle == nullptr)
        {
            return 0;
        }

        ThreadId   fallback = 0;
        const auto bytes    = std::min<std::size_t>(sizeof(fallback), sizeof(*nativeHandle));
        std::memcpy(&fallback, nativeHandle, bytes);
        return fallback;
    }

    Thread::NativeHandle Thread::NativeHandleValue() noexcept
    {
        return m_handle;
    }

    bool Thread::SetName(ThreadName name) noexcept
    {
        if (name.Empty() || !IsJoinable())
        {
            return false;
        }

#if defined(__linux__)
        std::array<char, 16> truncated {};
        TruncateForPthreadName(name.View(), truncated);
        return ::pthread_setname_np(*GetNativeThreadHandle(m_handle), truncated.data()) == 0;
#else
        (void)name;
        return false;
#endif
    }

    bool Thread::SetAffinity(UInt64 affinityMask) noexcept
    {
        if (affinityMask == 0 || !IsJoinable())
        {
            return false;
        }

#if defined(__linux__)
        cpu_set_t set {};
        CPU_ZERO(&set);
        for (std::size_t bit = 0; bit < (sizeof(UInt64) * 8u); ++bit)
        {
            if ((affinityMask & (1ull << static_cast<UInt64>(bit))) != 0)
            {
                CPU_SET(bit, &set);
            }
        }
        return ::pthread_setaffinity_np(*GetNativeThreadHandle(m_handle), sizeof(set), &set) == 0;
#else
        (void)affinityMask;
        return false;
#endif
    }

    bool Thread::SetPriority(int priority) noexcept
    {
        (void)priority;
        return false;
    }

    void Thread::StartImpl(NGIN::Utilities::Callable<void()> entry)
    {
        auto* context         = new StartContext();
        auto* nativeHandle    = new pthread_t {};
        context->entry        = std::move(entry);
        context->name         = m_options.name;
        context->affinityMask = m_options.affinityMask;
        context->priority     = m_options.priority;
        context->outThreadId  = &m_threadId;
        m_threadId.store(0, std::memory_order_release);

        pthread_attr_t attributes {};
        (void)::pthread_attr_init(&attributes);
        if (m_options.stackSize != 0)
        {
            (void)::pthread_attr_setstacksize(&attributes, m_options.stackSize);
        }

        const int result = ::pthread_create(nativeHandle, &attributes, &ThreadProc, context);
        (void)::pthread_attr_destroy(&attributes);
        if (result != 0)
        {
            delete context;
            delete nativeHandle;
            std::terminate();
        }

        m_handle = nativeHandle;
        m_joinable = true;
    }

    void Thread::MoveFrom(Thread&& other) noexcept
    {
        m_options        = other.m_options;
        m_joinable       = other.m_joinable;
        other.m_joinable = false;
        m_handle         = other.m_handle;
        other.m_handle   = nullptr;
        m_threadId.store(other.m_threadId.load(std::memory_order_acquire), std::memory_order_release);
        other.m_threadId.store(0, std::memory_order_release);
        other.m_options = {};
    }

    void Thread::HandleDestruction() noexcept
    {
        if (!IsJoinable())
        {
            return;
        }

        switch (m_options.onDestruct)
        {
            case OnDestruct::Join:
                Join();
                return;
            case OnDestruct::Detach:
                Detach();
                return;
            default:
                std::terminate();
        }
    }
}// namespace NGIN::Execution
