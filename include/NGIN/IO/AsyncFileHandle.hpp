#pragma once

#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <span>

namespace NGIN::IO
{
    /// @brief Move-only type-erased handle for cancellation-aware asynchronous file IO.
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
        AsyncTask<UIntSize> ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination)
        {
            if (!IsValid() || m_operations->read == nullptr)
                co_return MakeInvalidHandleError("async file handle is empty");
            co_return co_await m_operations->read(m_state, ctx, destination);
        }

        /// @brief Asynchronously writes at the backend's sequential position.
        AsyncTask<UIntSize> WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source)
        {
            if (!IsValid() || m_operations->write == nullptr)
                co_return MakeInvalidHandleError("async file handle is empty");
            co_return co_await m_operations->write(m_state, ctx, source);
        }

        /// @brief Asynchronously reads at an absolute offset without changing sequential position.
        AsyncTask<UIntSize> ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<NGIN::Byte> destination)
        {
            if (!IsValid() || m_operations->readAt == nullptr)
                co_return MakeInvalidHandleError("async file handle is empty");
            co_return co_await m_operations->readAt(m_state, ctx, offset, destination);
        }

        /// @brief Asynchronously writes at an absolute offset without changing sequential position.
        AsyncTask<UIntSize> WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<const NGIN::Byte> source)
        {
            if (!IsValid() || m_operations->writeAt == nullptr)
                co_return MakeInvalidHandleError("async file handle is empty");
            co_return co_await m_operations->writeAt(m_state, ctx, offset, source);
        }

        /// @brief Asynchronously flushes buffered contents to storage.
        AsyncTaskVoid FlushAsync(NGIN::Async::TaskContext& ctx)
        {
            if (!IsValid() || m_operations->flush == nullptr)
            {
                co_await NGIN::Async::DomainFailure(MakeInvalidHandleError("async file handle is empty"));
                co_return;
            }
            co_await m_operations->flush(m_state, ctx);
            co_return;
        }

        /// @brief Asynchronously closes the backend file resource.
        AsyncTaskVoid CloseAsync(NGIN::Async::TaskContext& ctx)
        {
            if (!IsValid() || m_operations->close == nullptr)
            {
                co_await NGIN::Async::DomainFailure(MakeInvalidHandleError("async file handle is empty"));
                co_return;
            }
            co_await m_operations->close(m_state, ctx);
            co_return;
        }

        /// @brief Returns whether the bound backend reports an open file resource.
        [[nodiscard]] bool IsOpen() const noexcept
        {
            return IsValid() && m_operations->isOpen != nullptr && m_operations->isOpen(m_state);
        }

    private:
        [[nodiscard]] static IOError MakeInvalidHandleError(const char* message) noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = message;
            return error;
        }

        std::shared_ptr<void> m_state {};
        const Operations*     m_operations {nullptr};
    };
}// namespace NGIN::IO
