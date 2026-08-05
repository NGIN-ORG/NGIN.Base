/// @file IByteStream.hpp
/// @brief Async byte stream interface.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/NetError.hpp>

namespace NGIN::Async
{
    class TaskContext;
    class CancellationToken;
    template<typename T, typename E>
    class Task;
}// namespace NGIN::Async

namespace NGIN::Net::Transport
{
    /// @brief Async byte stream interface for transport layers.
    class NGIN_NET_API IByteStream
    {
    public:
        /// @brief Constructs the byte-stream interface base.
        IByteStream() noexcept;
        /// @brief Destroys the stream implementation.
        virtual ~IByteStream();

        /// @brief Asynchronously reads up to the destination size.
        /// @return The number of bytes read; zero indicates end of stream.
        virtual NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(NGIN::Async::TaskContext&      ctx,
                                                                               NGIN::Net::ByteSpan            destination,
                                                                               NGIN::Async::CancellationToken token) = 0;
        /// @brief Asynchronously writes up to the source size.
        /// @return The number of bytes written; callers handle partial progress.
        virtual NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(NGIN::Async::TaskContext&      ctx,
                                                                                NGIN::Net::ConstByteSpan       source,
                                                                                NGIN::Async::CancellationToken token) = 0;
        /// @brief Closes the stream and releases transport resources.
        virtual NGIN::Net::NetExpected<void> Close() = 0;
    };
}// namespace NGIN::Net::Transport
