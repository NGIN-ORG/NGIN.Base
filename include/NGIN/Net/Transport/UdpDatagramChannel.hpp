/// @file UdpDatagramChannel.hpp
/// @brief IDatagramChannel adapter over UdpSocket.
#pragma once

#include <stdexcept>
#include <utility>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>
#include <NGIN/Net/Transport/IDatagramChannel.hpp>

namespace NGIN::Net::Transport
{
    /// @brief Datagram channel adapter that forwards to a UdpSocket.
    class UdpDatagramChannel final : public IDatagramChannel
    {
    public:
        UdpDatagramChannel(UdpSocket&& socket, NetworkDriver& driver) noexcept
            : m_socket(std::move(socket))
            , m_driver(&driver)
        {
        }

        NGIN::Async::Task<void> SendAsync(NGIN::Async::TaskContext& ctx,
                                          NGIN::Net::Endpoint remoteEndpoint,
                                          NGIN::Net::ConstByteSpan payload,
                                          NGIN::Async::CancellationToken token) override
        {
            return SendImpl(ctx, m_socket, m_driver, remoteEndpoint, payload, token);
        }

        NGIN::Async::Task<ReceivedDatagram> ReceiveAsync(NGIN::Async::TaskContext& ctx,
                                                         NGIN::Net::Buffer& receiveBuffer,
                                                         NGIN::Async::CancellationToken token) override
        {
            return ReceiveImpl(ctx, m_socket, m_driver, receiveBuffer, token);
        }

        [[nodiscard]] UdpSocket& Socket() noexcept { return m_socket; }
        [[nodiscard]] const UdpSocket& Socket() const noexcept { return m_socket; }

    private:
        static NGIN::Async::Task<void> SendImpl(NGIN::Async::TaskContext& ctx,
                                                UdpSocket& socket,
                                                NetworkDriver* driver,
                                                NGIN::Net::Endpoint remoteEndpoint,
                                                NGIN::Net::ConstByteSpan payload,
                                                NGIN::Async::CancellationToken token)
        {
            if (!driver)
            {
                throw std::logic_error("UdpDatagramChannel missing NetworkDriver");
            }
            auto task = socket.SendToAsync(ctx, *driver, remoteEndpoint, payload, token);
            task.Start(ctx);
            (void)co_await task;
        }

        static NGIN::Async::Task<ReceivedDatagram> ReceiveImpl(NGIN::Async::TaskContext& ctx,
                                                               UdpSocket& socket,
                                                               NetworkDriver* driver,
                                                               NGIN::Net::Buffer& receiveBuffer,
                                                               NGIN::Async::CancellationToken token)
        {
            if (!driver)
            {
                throw std::logic_error("UdpDatagramChannel missing NetworkDriver");
            }
            if (!receiveBuffer.data || receiveBuffer.capacity == 0)
            {
                throw std::invalid_argument("UdpDatagramChannel requires a valid receive buffer");
            }

            auto task = socket.ReceiveFromAsync(ctx,
                                                *driver,
                                                NGIN::Net::ByteSpan {receiveBuffer.data,
                                                                     receiveBuffer.capacity},
                                                token);
            task.Start(ctx);
            auto result = co_await task;

            receiveBuffer.size = result.bytesReceived;

            ReceivedDatagram datagram {};
            datagram.remoteEndpoint = result.remoteEndpoint;
            datagram.bytesReceived = result.bytesReceived;
            datagram.payload = NGIN::Net::ConstByteSpan {receiveBuffer.data, result.bytesReceived};
            co_return datagram;
        }

        UdpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
    };
}// namespace NGIN::Net::Transport
