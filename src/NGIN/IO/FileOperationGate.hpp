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
                if (!promise.m_taskContinuation.IsValid())
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

    // The backend operation is observed as an Operation so its failure cannot
    // bypass lease retirement. Destroy its frame before admitting close, including
    // a native close's rollback guard when backend admission failed.
    template<typename T, typename State, typename Function, typename... Args>
    AsyncTask<T> RunFileOperation(NGIN::Async::TaskContext& ctx, std::shared_ptr<void> rawState,
                                  FileOperationGate::Kind kind, Function function, Args... args)
    {
        using Outcome  = NGIN::Async::Completion<T, IOError>;
        using Status   = FileOperationGate::Status;
        auto state     = std::static_pointer_cast<State>(rawState);
        auto admission = state->operations.Acquire(ctx, kind);
        co_await admission;
        std::optional<Outcome> result;
        switch (admission.GetStatus())
        {
            case Status::Canceled:
                result.emplace(Outcome::Canceled());
                break;
            case Status::Fault:
                result.emplace(Outcome::Faulted(admission.Fault()));
                break;
            case Status::Closing:
            case Status::Closed: {
                if constexpr (std::is_void_v<T>)
                {
                    if (kind == FileOperationGate::Kind::Close && admission.GetStatus() == Status::Closed)
                    {
                        result.emplace(Outcome::Success());
                        break;
                    }
                }
                IOError error(admission.GetStatus() == Status::Closing ? IOErrorCode::Busy : IOErrorCode::InvalidArgument);
                error.path    = state->path;
                error.message = admission.GetStatus() == Status::Closing ? "file close in progress" : "file is closed";
                result.emplace(Outcome::DomainFailure(std::move(error)));
                break;
            }
            case Status::Granted: {
                if constexpr (requires { state->driver->IsStopping(); })
                {
                    if (state->driver->IsStopping())
                    {
                        result.emplace(Outcome::Canceled());
                        break;
                    }
                }
                auto operation = NGIN::Async::Spawn(ctx, function(rawState, ctx, args...));
                result.emplace(co_await operation);
                operation = {};
                break;
            }
        }
        const bool closed = kind == FileOperationGate::Kind::Close && !state->NativeIsOpen();
        admission.Reset(closed);
        if constexpr (std::is_void_v<T>)
        {
            if (result->IsCanceled())
                co_await NGIN::Async::Canceled();
            else if (result->IsFault())
                co_await NGIN::Async::Faulted(std::move(*result).Fault());
            else if (result->IsDomainError())
                co_await NGIN::Async::DomainFailure(std::move(*result).DomainError());
            co_return;
        }
        else
            co_return std::move(*result);
    }

    template<typename T, typename State, FileOperationGate::Kind Kind, auto Function, typename... Args>
    AsyncTask<T> WithFileOperation(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx, Args... args)
    {
        return RunFileOperation<T, State>(ctx, state, Kind, Function, args...);
    }
}// namespace NGIN::IO::detail
