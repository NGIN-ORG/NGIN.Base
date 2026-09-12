#pragma once

#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <span>

namespace NGIN::IO
{
    /// @brief Move-only type-erased handle for cancellation-aware asynchronous file IO.
    /// @details Operation calls snapshot shared backend state before returning a cold task.
    /// Moving or releasing this handle does not invalidate an existing task. The caller
    /// keeps its context, executor, and borrowed buffers alive through completion.
    /// Local filesystem handles serialize sequential operations in task admission order;
    /// explicit offsets may overlap. Callers coordinate conflicting buffers and ranges.
    /// Local queued and active operations share the runtime's files.queueDepthHint
    /// budget across handles and executors. Saturation reports ResourceExhausted.
    /// Local transfers are limited to UINT32_MAX bytes and offsets to INT64_MAX.
    class NGIN_IO_API AsyncFileHandle
    {
    public:
        using ReadFn = AsyncTask<UIntSize> (*)(
                const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination);
        using WriteFn = AsyncTask<UIntSize> (*)(
                const std::shared_ptr<void>& state,
                NGIN::Async::TaskContext&    ctx,
                std::span<const NGIN::Byte>  source);
        using ReadAtFn = AsyncTask<UIntSize> (*)(
                const std::shared_ptr<void>& state,
                NGIN::Async::TaskContext&    ctx,
                UInt64                       offset,
                std::span<NGIN::Byte>        destination);
        using WriteAtFn = AsyncTask<UIntSize> (*)(
                const std::shared_ptr<void>& state,
                NGIN::Async::TaskContext&    ctx,
                UInt64                       offset,
                std::span<const NGIN::Byte>  source);
        using FlushFn  = AsyncTaskVoid (*)(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx);
        using CloseFn  = AsyncTaskVoid (*)(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx);
        using IsOpenFn = bool (*)(const std::shared_ptr<void>& state) noexcept;

        /// @brief Function table implemented by an asynchronous file backend.
        struct Operations
        {
            ReadFn    read {};
            WriteFn   write {};
            ReadAtFn  readAt {};
            WriteAtFn writeAt {};
            FlushFn   flush {};
            CloseFn   close {};
            IsOpenFn  isOpen {};
        };

        /// @brief Constructs an empty asynchronous handle.
        AsyncFileHandle() noexcept = default;
        /// @brief Binds shared backend state to a static operation table.
        /// @note The operation table must outlive this handle and all of its moves.
        AsyncFileHandle(std::shared_ptr<void> state, const Operations* operations) noexcept
            : m_state(std::move(state)), m_operations(operations)
        {
        }

        /// @brief Asynchronous handles are non-copyable to keep ownership explicit.
        AsyncFileHandle(const AsyncFileHandle&) = delete;
        /// @brief Asynchronous handles are non-copy-assignable to keep ownership explicit.
        AsyncFileHandle& operator=(const AsyncFileHandle&) = delete;
        /// @brief Transfers shared backend state and its operation table.
        AsyncFileHandle(AsyncFileHandle&&) noexcept = default;
        /// @brief Transfers shared backend state and its operation table.
        AsyncFileHandle& operator=(AsyncFileHandle&&) noexcept = default;
        /// @brief Releases this handle's reference to the backend state.
        ~AsyncFileHandle() = default;

        /// @brief Returns whether state and an operation table are both bound.
        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_state) && m_operations != nullptr; }
        /// @brief Returns whether state and an operation table are both bound.
        explicit operator bool() const noexcept { return IsValid(); }

        /// @brief Asynchronously reads from the backend's sequential position.
        AsyncTask<UIntSize> ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination);

        /// @brief Asynchronously writes at the backend's sequential position.
        AsyncTask<UIntSize> WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source);

        /// @brief Asynchronously reads at an absolute offset without changing sequential position.
        AsyncTask<UIntSize> ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<NGIN::Byte> destination);

        /// @brief Asynchronously writes at an absolute offset without changing sequential position.
        /// @note Local append handles reject this operation with NotSupported.
        AsyncTask<UIntSize> WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<const NGIN::Byte> source);

        /// @brief Asynchronously flushes buffered contents to storage.
        /// @note Local handles wait for earlier admitted work and block later work.
        AsyncTaskVoid FlushAsync(NGIN::Async::TaskContext& ctx);

        /// @brief Asynchronously closes the backend file resource.
        /// @note Local handles close admission, drain earlier backend work, then close.
        /// New operations report Busy while closing; repeated close after closure succeeds.
        /// Cancellation of a queued close reopens admission. Submitted close may still close.
        /// Capacity rejection leaves the handle open and does not close admission.
        AsyncTaskVoid CloseAsync(NGIN::Async::TaskContext& ctx);

        /// @brief Returns whether the bound backend reports an open file resource.
        [[nodiscard]] bool IsOpen() const noexcept
        {
            return IsValid() && m_operations->isOpen != nullptr && m_operations->isOpen(m_state);
        }

    private:
        std::shared_ptr<void> m_state {};
        const Operations*     m_operations {nullptr};
    };
}// namespace NGIN::IO
