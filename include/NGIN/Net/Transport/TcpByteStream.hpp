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
            : m_socket(std::move(socket))
            , m_driver(&driver)
        {
        }

        NGIN::Async::Task<NGIN::UInt32> ReadAsync(NGIN::Async::TaskContext& ctx,
                                                 NGIN::Net::ByteSpan destination,
                                                 NGIN::Async::CancellationToken token) override
        {
            return ReadImpl(ctx, m_socket, m_driver, destination, token);
        }

        NGIN::Async::Task<NGIN::UInt32> WriteAsync(NGIN::Async::TaskContext& ctx,
                                                  NGIN::Net::ConstByteSpan source,
                                                  NGIN::Async::CancellationToken token) override
        {
            return WriteImpl(ctx, m_socket, m_driver, source, token);
        }

        NGIN::Net::NetExpected<void> Close() override
        {
            m_socket.Close();
            return {};
        }

        [[nodiscard]] TcpSocket& Socket() noexcept { return m_socket; }
        [[nodiscard]] const TcpSocket& Socket() const noexcept { return m_socket; }

    private:
        static NGIN::Async::Task<NGIN::UInt32> ReadImpl(NGIN::Async::TaskContext& ctx,
                                                       TcpSocket& socket,
                                                       NetworkDriver* driver,
                                                       NGIN::Net::ByteSpan destination,
                                                       NGIN::Async::CancellationToken token)
        {
            if (!driver)
            {
                co_return std::unexpected(NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::InvalidState));
            }
            auto task = socket.ReceiveAsync(ctx, *driver, destination, token);
            task.Schedule(ctx);
            auto result = co_await task;
            if (!result)
            {
                co_return std::unexpected(result.error());
            }
            co_return *result;
        }

        static NGIN::Async::Task<NGIN::UInt32> WriteImpl(NGIN::Async::TaskContext& ctx,
                                                        TcpSocket& socket,
                                                        NetworkDriver* driver,
                                                        NGIN::Net::ConstByteSpan source,
                                                        NGIN::Async::CancellationToken token)
        {
            if (!driver)
            {
                co_return std::unexpected(NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::InvalidState));
            }
            auto task = socket.SendAsync(ctx, *driver, source, token);
            task.Schedule(ctx);
            auto result = co_await task;
            if (!result)
            {
                co_return std::unexpected(result.error());
            }
            co_return *result;
        }

        TcpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
    };
}// namespace NGIN::Net::Transport
