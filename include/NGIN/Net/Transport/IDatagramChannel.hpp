/// @file IDatagramChannel.hpp
/// @brief Async datagram channel interface.
#pragma once

#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
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
    /// @brief Result for receiving a datagram into a buffer.
    struct ReceivedDatagram final
    {
        NGIN::Net::Endpoint     remoteEndpoint {};
        NGIN::Net::ConstByteSpan payload {};
        NGIN::UInt32            bytesReceived {0};
    };

    /// @brief Async datagram channel interface for transport layers.
    class IDatagramChannel
    {
    public:
        virtual ~IDatagramChannel() = default;

        virtual NGIN::Async::Task<void> SendAsync(NGIN::Async::TaskContext& ctx,
                                                  NGIN::Net::Endpoint remoteEndpoint,
                                                  NGIN::Net::ConstByteSpan payload,
                                                  NGIN::Async::CancellationToken token) = 0;

        virtual NGIN::Async::Task<ReceivedDatagram> ReceiveAsync(NGIN::Async::TaskContext& ctx,
                                                                 NGIN::Net::Buffer& receiveBuffer,
                                                                 NGIN::Async::CancellationToken token) = 0;
    };
}// namespace NGIN::Net::Transport
