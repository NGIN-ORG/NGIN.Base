/// @file TcpByteStream.hpp
/// @brief IByteStream adapter over TcpSocket.
#pragma once

#include <utility>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Net/Sockets/TcpSocket.hpp>
#include <NGIN/Net/Transport/IByteStream.hpp>

namespace NGIN::Net::Transport
{
    /// @brief Byte stream adapter that forwards to a TcpSocket.
    class TcpByteStream final : public IByteStream
    {
    public:
        /// @brief Takes ownership of a TCP socket with its existing runtime binding.
        /// @note The runtime must outlive this stream and all outstanding operations.
        TcpByteStream(TcpSocket&& socket) noexcept
            : m_socket(std::move(socket))
        {
        }

        /// @copydoc IByteStream::ReadAsync
        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> ReadAsync(NGIN::Async::TaskContext&      ctx,
                                                                       NGIN::Net::ByteSpan            destination,
                                                                       NGIN::Async::CancellationToken token) override
        {
            return m_socket.ReceiveAsync(ctx, destination, token);
        }

        /// @copydoc IByteStream::WriteAsync
        NGIN::Async::Task<NGIN::UInt32, NGIN::Net::NetError> WriteAsync(NGIN::Async::TaskContext&      ctx,
                                                                        NGIN::Net::ConstByteSpan       source,
                                                                        NGIN::Async::CancellationToken token) override
        {
            return m_socket.SendAsync(ctx, source, token);
        }

        /// @copydoc IByteStream::Close
        NGIN::Net::NetExpected<void> Close() override
        {
            m_socket.Close();
            return {};
        }

        /// @brief Returns mutable access to the owned TCP socket.
        [[nodiscard]] TcpSocket& Socket() noexcept { return m_socket; }
        /// @brief Returns the owned TCP socket.
        [[nodiscard]] const TcpSocket& Socket() const noexcept { return m_socket; }

    private:
        TcpSocket m_socket {};
    };
}// namespace NGIN::Net::Transport
