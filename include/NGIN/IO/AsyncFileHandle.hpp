#pragma once

#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <span>

namespace NGIN::IO
{
    class NGIN_BASE_API AsyncFileHandle
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
        using FlushFn = AsyncTaskVoid (*)(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx);
        using CloseFn = AsyncTaskVoid (*)(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx);
        using IsOpenFn = bool (*)(const std::shared_ptr<void>& state) noexcept;

        struct Operations
        {
            ReadFn   read {};
            WriteFn  write {};
            ReadAtFn readAt {};
            WriteAtFn writeAt {};
            FlushFn  flush {};
            CloseFn  close {};
            IsOpenFn isOpen {};
        };

        AsyncFileHandle() noexcept = default;
        AsyncFileHandle(std::shared_ptr<void> state, const Operations* operations) noexcept
            : m_state(std::move(state))
            , m_operations(operations)
        {
        }

        AsyncFileHandle(const AsyncFileHandle&)                = delete;
        AsyncFileHandle& operator=(const AsyncFileHandle&)     = delete;
        AsyncFileHandle(AsyncFileHandle&&) noexcept            = default;
        AsyncFileHandle& operator=(AsyncFileHandle&&) noexcept = default;
        ~AsyncFileHandle()                                     = default;

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_state) && m_operations != nullptr; }
        explicit           operator bool() const noexcept { return IsValid(); }

        AsyncTask<UIntSize> ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination)
        {
            if (!IsValid() || m_operations->read == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async file handle is empty"));
            co_return co_await m_operations->read(m_state, ctx, destination);
        }

        AsyncTask<UIntSize> WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source)
        {
            if (!IsValid() || m_operations->write == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async file handle is empty"));
            co_return co_await m_operations->write(m_state, ctx, source);
        }

        AsyncTask<UIntSize> ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<NGIN::Byte> destination)
        {
            if (!IsValid() || m_operations->readAt == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async file handle is empty"));
            co_return co_await m_operations->readAt(m_state, ctx, offset, destination);
        }

        AsyncTask<UIntSize> WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<const NGIN::Byte> source)
        {
            if (!IsValid() || m_operations->writeAt == nullptr)
                co_return NGIN::Utilities::Unexpected<IOError>(MakeInvalidHandleError("async file handle is empty"));
            co_return co_await m_operations->writeAt(m_state, ctx, offset, source);
        }

        AsyncTaskVoid FlushAsync(NGIN::Async::TaskContext& ctx)
        {
            if (!IsValid() || m_operations->flush == nullptr)
            {
                co_await AsyncTaskVoid::ReturnError(MakeInvalidHandleError("async file handle is empty"));
                co_return;
            }
            co_await m_operations->flush(m_state, ctx);
            co_return;
        }

        AsyncTaskVoid CloseAsync(NGIN::Async::TaskContext& ctx)
        {
            if (!IsValid() || m_operations->close == nullptr)
            {
                co_await AsyncTaskVoid::ReturnError(MakeInvalidHandleError("async file handle is empty"));
                co_return;
            }
            co_await m_operations->close(m_state, ctx);
            co_return;
        }

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

        std::shared_ptr<void>  m_state {};
        const Operations*      m_operations {nullptr};
    };
}// namespace NGIN::IO
