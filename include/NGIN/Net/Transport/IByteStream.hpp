/// @file IByteStream.hpp
/// @brief Async byte stream interface.
#pragma once

#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/NetError.hpp>

namespace NGIN::Async
{
    class TaskContext;
    class CancellationToken;
    template<typename T>
    class Task;
}// namespace NGIN::Async

namespace NGIN::Net::Transport
{
    /// @brief Async byte stream interface for transport layers.
    class IByteStream
    {
    public:
        virtual ~IByteStream() = default;

        virtual NGIN::Async::Task<NGIN::UInt32> ReadAsync(NGIN::Async::TaskContext& ctx,
                                                         NGIN::Net::ByteSpan destination,
                                                         NGIN::Async::CancellationToken token) = 0;
        virtual NGIN::Async::Task<NGIN::UInt32> WriteAsync(NGIN::Async::TaskContext& ctx,
                                                          NGIN::Net::ConstByteSpan source,
                                                          NGIN::Async::CancellationToken token) = 0;
        virtual NGIN::Net::NetExpected<void> Close() = 0;
    };
}// namespace NGIN::Net::Transport
