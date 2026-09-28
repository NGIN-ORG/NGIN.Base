#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

namespace NGIN::IO::detail
{
    class FileSystemDriver;
    [[nodiscard]] ResultVoid ValidateFileTransfer(const Path& path, bool allowed, bool writing, bool append,
                                                  UIntSize size, std::optional<UInt64> offset = {});

    // Per-file ordering with service-wide admission. Nodes live in admitted
    // coroutine frames; waiting never occupies a blocking worker.
    class FileOperationGate final
    {
    public:
        enum class Kind
        {
            Independent,
            Position,
            Flush,
            Close
        };
        enum class Status
        {
            Granted,
            Closed,
            Closing,
            Canceled,
            Fault
        };

        explicit FileOperationGate(FileSystemDriver& driver) noexcept : m_driver(driver) {}
        ~FileOperationGate();
        FileOperationGate(const FileOperationGate&)            = delete;
        FileOperationGate& operator=(const FileOperationGate&) = delete;

    private:
        enum class Phase
        {
            Preparing,
            Queued,
            Active,
            Done
        };
        struct Node final
        {
            FileOperationGate*                         gate;
            Kind                                       kind;
            Phase                                      phase {Phase::Preparing};
            Status                                     status {Status::Granted};
            bool                                       canceled {false};
            bool                                       granted {false};
            bool                                       admitted {false};
            Node*                                      previous {};
            Node*                                      next {};
            Node*                                      readyNext {};
            NGIN::Async::CancellationRegistration      registration {};
            NGIN::Async::detail::PromiseRuntimeCommon* target {};
            NGIN::Execution::WorkItem                  continuation {};
            NGIN::Async::AsyncFault                    fault {};
        };

    public:
        class Admission final
        {
        public:
            Admission(FileOperationGate& gate, NGIN::Async::TaskContext& ctx, Kind kind) noexcept
                : m_context(ctx), m_node {.gate = &gate, .kind = kind} {}
            Admission(const Admission&)            = delete;
            Admission& operator=(const Admission&) = delete;
            ~Admission() { Reset(); }
            bool await_ready() const noexcept { return false; }

            template<typename Promise>
                requires std::derived_from<Promise, NGIN::Async::detail::PromiseRuntimeCommon>
            bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                auto& promise = awaiting.promise();
                if (!promise.HasContinuation())
                {
                    m_node.status = Status::Fault;
                    m_node.fault  = NGIN::Async::detail::MakeSchedulingFault(
                            NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected);
                    return false;
                }
                promise.RetainFrameReference();
                m_node.target         = &promise;
                m_node.continuation   = NGIN::Execution::WorkItem([node = &m_node, awaiting] {
                    node->registration.Reset();
                    NGIN::Async::detail::PromiseRuntimeCommon::ResumeRetained(awaiting);
                });
                const auto registered = m_context.GetCancellationToken().Register(
                        m_node.registration, {}, {}, +[](void* raw) noexcept {
                            auto& node = *static_cast<Node*>(raw);
                            node.gate->Cancel(node);
                            return false;
                        },
                        &m_node);
                if (!registered)
                {
                    m_node.status = Status::Fault;
                    m_node.fault  = NGIN::Async::MakeAsyncFault(
                            NGIN::Async::AsyncFaultCode::CancellationRegistrationFailed, static_cast<int>(registered.error()));
                    promise.ReleaseFrameReference(awaiting);
                    return false;
                }
                // Preparing callbacks can only mark cancellation. Enqueue is the
                // final publication: a queued cancellation may retire this awaiter.
                if (m_node.gate->Enqueue(m_node))
                    return true;
                m_node.registration.Reset();
                promise.ReleaseFrameReference(awaiting);
                return false;
            }
            void                           await_resume() noexcept { m_node.registration.Reset(); }
            Status                         GetStatus() const noexcept { return m_node.canceled ? Status::Canceled : m_node.status; }
            const NGIN::Async::AsyncFault& Fault() const noexcept { return m_node.fault; }
            NGIN::Async::StopReason StopReason() const noexcept
            {
                // Cancellation callbacks set canceled. A canceled admission without
                // such a callback means the runtime rejected new handle work.
                return m_node.canceled ? m_context.GetCancellationToken().GetStopReason()
                                       : NGIN::Async::StopReason::RuntimeShutdown;
            }
            void                           Reset(bool closed = false) noexcept;

        private:
            NGIN::Async::TaskContext& m_context;
            Node                      m_node;
        };

        Admission Acquire(NGIN::Async::TaskContext& context, Kind kind) noexcept { return {*this, context, kind}; }
        bool      IsClosing() const noexcept;

    private:
        bool        Enqueue(Node& node) noexcept;
        void        Cancel(Node& node) noexcept;
        void        Release(Kind kind, bool closed) noexcept;
        void        Remove(Node& node) noexcept;
        Node*       Activate(Node* immediate = nullptr) noexcept;
        static void Dispatch(Node* ready) noexcept;

        FileSystemDriver&  m_driver;
        mutable std::mutex m_mutex;
        Node*              m_head {};
        Node*              m_tail {};
        std::size_t        m_active {};
        bool               m_positionActive {false};
        bool               m_barrierActive {false};
        bool               m_closing {false};
        bool               m_closed {false};
    };

    // Lives in the backend coroutine, before any request/rollback guard. Its
    // destruction follows backend retirement and close rollback, without a Task layer.
    template<typename State>
    class FileOperationLease final
    {
    public:
        using Outcome = NGIN::Async::Completion<void, IOError>;
        FileOperationLease(State& state, NGIN::Async::TaskContext& context, FileOperationGate::Kind kind)
            : m_state(state), m_context(context), m_kind(kind), m_admission(state.operations, context, kind) {}
        ~FileOperationLease()
        {
            m_admission.Reset(m_kind == FileOperationGate::Kind::Close && !m_state.NativeIsOpen());
        }
        FileOperationLease(const FileOperationLease&) = delete;
        FileOperationLease& operator=(const FileOperationLease&) = delete;
        bool await_ready() const noexcept { return false; }
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
        {
            return m_admission.await_suspend(awaiting);
        }
        // Empty means admitted; a completion means skip the backend, including an
        // already-closed Close. Ready completion propagation creates no coroutine.
        std::optional<Outcome> await_resume()
        {
            m_admission.await_resume();
            using Status = FileOperationGate::Status;
            switch (m_admission.GetStatus())
            {
                case Status::Canceled:
                    return Outcome::Stopped(m_admission.StopReason());
                case Status::Fault:
                    return Outcome::Faulted(m_admission.Fault());
                case Status::Closing:
                case Status::Closed: {
                    if (m_kind == FileOperationGate::Kind::Close && m_admission.GetStatus() == Status::Closed)
                        return Outcome::Success();
                    IOError error(m_admission.GetStatus() == Status::Closing ? IOErrorCode::Busy : IOErrorCode::InvalidArgument);
                    error.path = m_state.path;
                    error.message = m_admission.GetStatus() == Status::Closing ? "file close in progress" : "file is closed";
                    return Outcome::DomainFailure(std::move(error));
                }
                case Status::Granted:
                    if constexpr (requires { m_state.driver->IsStopping(); })
                        if (m_state.driver->IsStopping())
                            return Outcome::Stopped(NGIN::Async::StopReason::RuntimeShutdown);
                    return {};
            }
            std::terminate();
        }
    private:
        State& m_state;
        NGIN::Async::TaskContext& m_context;
        FileOperationGate::Kind m_kind;
        FileOperationGate::Admission m_admission;
    };
}// namespace NGIN::IO::detail
