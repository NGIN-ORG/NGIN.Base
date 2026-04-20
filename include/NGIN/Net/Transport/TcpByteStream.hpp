/// @file TcpByteStream.hpp
/// @brief IByteStream adapter over TcpSocket.
#pragma once

#include <utility>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Net/Runtime/NetworkDriver.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Transport/IByteStream.hpp>

namespace NGIN::Net::Transport
{
    /// @brief Byte stream adapter that forwards to a TcpSocket.
    class TcpByteStream final : public IByteStream
    {
    public:
        TcpByteStream(TcpSocket&& socket, NetworkDriver& driver) noexcept
            : m_socket(std::move(socket)), m_driver(&driver)
        {
        }

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(NGIN::Async::TaskContext&      ctx,
                                                                        NGIN::Net::ByteSpan            destination,
                                                                        NGIN::Async::CancellationToken token) override
        {
            return ReadImpl(ctx, m_socket, m_driver, destination, token);
        }

        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(NGIN::Async::TaskContext&      ctx,
                                                                         NGIN::Net::ConstByteSpan       source,
                                                                         NGIN::Async::CancellationToken token) override
        {
            return WriteImpl(ctx, m_socket, m_driver, source, token);
        }

        NGIN::Net::NetExpected<void> Close() override
        {
            m_socket.Close();
            return {};
        }

        [[nodiscard]] TcpSocket&       Socket() noexcept { return m_socket; }
        [[nodiscard]] const TcpSocket& Socket() const noexcept { return m_socket; }

    private:
        static NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadImpl(NGIN::Async::TaskContext&      ctx,
                                                                              TcpSocket&                     socket,
                                                                              NetworkDriver*                 driver,
                                                                              NGIN::Net::ByteSpan            destination,
                                                                              NGIN::Async::CancellationToken token)
        {
            if (!driver)
            {
                co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Faulted(
                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidState));
            }
            co_return co_await socket.ReceiveAsync(ctx, *driver, destination, token);
        }

        static NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteImpl(NGIN::Async::TaskContext&      ctx,
                                                                               TcpSocket&                     socket,
                                                                               NetworkDriver*                 driver,
                                                                               NGIN::Net::ConstByteSpan       source,
                                                                               NGIN::Async::CancellationToken token)
        {
            if (!driver)
            {
                co_return NGIN::Async::Completion<NGIN::UInt32, NGIN::Net::NetError>::Faulted(
                        NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidState));
            }
            co_return co_await socket.SendAsync(ctx, *driver, source, token);
        }

        TcpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
    };
}// namespace NGIN::Net::Transport
