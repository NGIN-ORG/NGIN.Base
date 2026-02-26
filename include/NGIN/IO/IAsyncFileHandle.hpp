#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <span>

namespace NGIN::IO
{
    class NGIN_BASE_API IAsyncFileHandle
    {
    public:
        virtual ~IAsyncFileHandle() = default;

        virtual AsyncTask<UIntSize> ReadAsync(NGIN::Async::TaskContext& ctx, std::span<NGIN::Byte> destination) = 0;
        virtual AsyncTask<UIntSize> WriteAsync(NGIN::Async::TaskContext& ctx, std::span<const NGIN::Byte> source) = 0;
        virtual AsyncTask<UIntSize> ReadAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<NGIN::Byte> destination) = 0;
        virtual AsyncTask<UIntSize> WriteAtAsync(NGIN::Async::TaskContext& ctx, UInt64 offset, std::span<const NGIN::Byte> source) = 0;
        virtual AsyncTaskVoid       FlushAsync(NGIN::Async::TaskContext& ctx) = 0;
        virtual AsyncTaskVoid       CloseAsync(NGIN::Async::TaskContext& ctx) = 0;
    };
}// namespace NGIN::IO

