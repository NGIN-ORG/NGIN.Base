#pragma once

#include <NGIN/IO/Runtime.hpp>
#include <memory>

namespace NGIN::IO::detail
{
    class RuntimeLoop;
    // Private attachment boundary. Backends are constructed by their owning
    // component; the runtime never links a filesystem or network factory.
    enum class RuntimeServiceKind
    {
        Files,
        Network
    };

    class RuntimeService
    {
    public:
        virtual ~RuntimeService() = default;
        virtual void                 Stop() noexcept = 0;
        virtual Runtime::FileBackend GetFileBackend() const noexcept { return Runtime::FileBackend::None; }
    };

    struct RuntimeAccess
    {
        using Factory = std::shared_ptr<RuntimeService> (*)(Runtime&);
        static NGIN_IORUNTIME_API std::shared_ptr<RuntimeService> Acquire(
                Runtime& runtime, RuntimeServiceKind kind, Factory factory);
        static NGIN_IORUNTIME_API RuntimeLoop& Loop(Runtime& runtime) noexcept;
        static NGIN_IORUNTIME_API std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
        ReserveOperation(Runtime& runtime, NGIN::Execution::WorkItem completion) noexcept;
        static NGIN_IORUNTIME_API void Run(Runtime& runtime, NGIN::Execution::WorkItem entered);
    };
}// namespace NGIN::IO::detail
