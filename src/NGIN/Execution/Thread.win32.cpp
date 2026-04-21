#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Text/Unicode/Convert.hpp>

#include <Windows.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <exception>
#include <string_view>

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

        bool Utf8ToWide(std::string_view utf8, std::array<wchar_t, 64>& output) noexcept
        {
            static_assert(sizeof(wchar_t) == sizeof(char16_t), "Windows thread naming expects 16-bit wchar_t.");

            const std::string_view clipped = utf8.substr(0, output.size() - 1);
            const auto             wideResult =
                    NGIN::Text::Unicode::ToUtf16(clipped, NGIN::Text::Unicode::ErrorPolicy::Strict);
            if (!wideResult.HasValue())
                return false;

            const auto& wide = wideResult.Value();
            if (wide.Size() > output.size() - 1)
                return false;

            std::fill(output.begin(), output.end(), L'\0');
            for (std::size_t index = 0; index < wide.Size(); ++index)
                output[index] = static_cast<wchar_t>(wide[index]);

            return true;
        }

        unsigned __stdcall ThreadProc(void* parameter) noexcept
        {
            auto* context = static_cast<StartContext*>(parameter);
            if (context->outThreadId != nullptr)
            {
                context->outThreadId->store(static_cast<Thread::ThreadId>(::GetCurrentThreadId()), std::memory_order_release);
            }

            if (!context->name.Empty())
            {
                std::array<wchar_t, 64> wide {};
                if (Utf8ToWide(context->name.View(), wide))
                {
                    (void)::SetThreadDescription(::GetCurrentThread(), wide.data());
                }
            }

            if (context->affinityMask != 0)
            {
                (void)::SetThreadAffinityMask(::GetCurrentThread(), static_cast<std::uintptr_t>(context->affinityMask));
            }
            if (context->priority != 0)
            {
                (void)::SetThreadPriority(::GetCurrentThread(), context->priority);
            }

            try
            {
                context->entry();
            } catch (...)
            {
                std::terminate();
            }

            delete context;
            return 0;
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

        (void)::WaitForSingleObject(m_handle, INFINITE);
        (void)::CloseHandle(static_cast<HANDLE>(m_handle));
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

        (void)::CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
        m_threadId.store(0, std::memory_order_release);
        m_joinable = false;
    }

    Thread::ThreadId Thread::GetId() const noexcept
    {
        return m_threadId.load(std::memory_order_acquire);
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

        std::array<wchar_t, 64> wide {};
        if (!Utf8ToWide(name.View(), wide))
        {
            return false;
        }

        return ::SetThreadDescription(static_cast<HANDLE>(m_handle), wide.data()) >= 0;
    }

    bool Thread::SetAffinity(UInt64 affinityMask) noexcept
    {
        if (affinityMask == 0 || !IsJoinable())
        {
            return false;
        }

        return ::SetThreadAffinityMask(static_cast<HANDLE>(m_handle), static_cast<std::uintptr_t>(affinityMask)) != 0;
    }

    bool Thread::SetPriority(int priority) noexcept
    {
        return IsJoinable() && (::SetThreadPriority(static_cast<HANDLE>(m_handle), priority) != 0);
    }

    void Thread::StartImpl(NGIN::Utilities::Callable<void()> entry)
    {
        auto* context         = new StartContext();
        context->entry        = std::move(entry);
        context->name         = m_options.name;
        context->affinityMask = m_options.affinityMask;
        context->priority     = m_options.priority;
        context->outThreadId  = &m_threadId;
        m_threadId.store(0, std::memory_order_release);

        unsigned        threadId     = 0;
        const auto      stackSize    = static_cast<unsigned>(m_options.stackSize);
        const uintptr_t threadHandle = ::_beginthreadex(nullptr, stackSize, &ThreadProc, context, 0u, &threadId);
        if (threadHandle == 0)
        {
            delete context;
            std::terminate();
        }

        m_handle = reinterpret_cast<void*>(threadHandle);
        m_threadId.store(static_cast<ThreadId>(threadId), std::memory_order_release);
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
