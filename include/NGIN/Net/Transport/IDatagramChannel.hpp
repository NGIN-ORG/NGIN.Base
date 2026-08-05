/// @file IDatagramChannel.hpp
/// @brief Async datagram channel interface.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
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
    /// @brief Result for receiving a datagram into a buffer.
    struct ReceivedDatagram final
    {
        NGIN::Net::Endpoint      remoteEndpoint {};
        NGIN::Net::ConstByteSpan payload {};
        NGIN::UInt32             bytesReceived {0};
    };

    /// @brief Async datagram channel interface for transport layers.
    class NGIN_NET_API IDatagramChannel
    {
    public:
        /// @brief Constructs the datagram-channel interface base.
        IDatagramChannel() noexcept;
        /// @brief Destroys the channel implementation.
        virtual ~IDatagramChannel();

        /// @brief Asynchronously sends one complete datagram to an endpoint.
        virtual NGIN::Async::Task<void, NGIN::Net::NetError> SendAsync(NGIN::Async::TaskContext&      ctx,
                                                                       NGIN::Net::Endpoint            remoteEndpoint,
                                                                       NGIN::Net::ConstByteSpan       payload,
                                                                       NGIN::Async::CancellationToken token) = 0;

        /// @brief Asynchronously receives one datagram into caller-provided buffer storage.
        /// @note The returned payload view borrows @p receiveBuffer.
        virtual NGIN::Async::Task<ReceivedDatagram, NGIN::Net::NetError> ReceiveAsync(NGIN::Async::TaskContext&      ctx,
                                                                                      NGIN::Net::Buffer&             receiveBuffer,
                                                                                      NGIN::Async::CancellationToken token) = 0;
    };
}// namespace NGIN::Net::Transport
