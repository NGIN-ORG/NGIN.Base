#include <NGIN/IO/AsyncFileHandle.hpp>

namespace NGIN::IO
{
    namespace
    {
        IOError InvalidHandleError() noexcept
        {
            IOError error;
            error.code    = IOErrorCode::InvalidArgument;
            error.message = "async file handle is empty";
            return error;
        }

        // The parameter owns the state before initial suspension. Backend functions
        // may borrow this stable shared_ptr through their existing operation table.
        template<typename Function, typename... Args>
        AsyncTask<UIntSize> DispatchData(NGIN::Async::TaskContext& ctx, std::shared_ptr<void> state,
                                         Function function, Args... args)
        {
            if (!state || !function)
                co_return InvalidHandleError();
            co_return co_await function(state, ctx, args...);
        }

        AsyncTaskVoid DispatchControl(NGIN::Async::TaskContext& ctx, std::shared_ptr<void> state,
                                      AsyncFileHandle::FlushFn function)
        {
            if (!state || !function)
            {
                co_await NGIN::Async::DomainFailure(InvalidHandleError());
                co_return;
            }
            co_await function(state, ctx);
        }
    }// namespace

    AsyncTask<UIntSize> AsyncFileHandle::ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination)
    {
        return DispatchData(ctx, m_state, IsValid() ? m_operations->read : nullptr, destination);
    }

    AsyncTask<UIntSize> AsyncFileHandle::WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source)
    {
        return DispatchData(ctx, m_state, IsValid() ? m_operations->write : nullptr, source);
    }

    AsyncTask<UIntSize> AsyncFileHandle::ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset,
                                                     std::span<NGIN::Byte> destination)
    {
        return DispatchData(ctx, m_state, IsValid() ? m_operations->readAt : nullptr, offset, destination);
    }

    AsyncTask<UIntSize> AsyncFileHandle::WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset,
                                                      std::span<const NGIN::Byte> source)
    {
        return DispatchData(ctx, m_state, IsValid() ? m_operations->writeAt : nullptr, offset, source);
    }

    AsyncTaskVoid AsyncFileHandle::FlushAsync(NGIN::Async::TaskContext& ctx)
    {
        return DispatchControl(ctx, m_state, IsValid() ? m_operations->flush : nullptr);
    }

    AsyncTaskVoid AsyncFileHandle::CloseAsync(NGIN::Async::TaskContext& ctx)
    {
        return DispatchControl(ctx, m_state, IsValid() ? m_operations->close : nullptr);
    }
}// namespace NGIN::IO
