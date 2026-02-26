/// @file LengthPrefixedMessageStream.hpp
/// @brief Length-prefixed message framing over an IByteStream.
#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Net/Transport/IByteStream.hpp>
#include <NGIN/Net/Types/Buffer.hpp>
#include <NGIN/Net/Types/NetError.hpp>

namespace NGIN::Net::Transport::Filters
{
    /// @brief Framing filter that prefixes each message with a 32-bit big-endian length.
    class LengthPrefixedMessageStream final
    {
    public:
        static constexpr std::size_t LengthBytes = 4;

        explicit LengthPrefixedMessageStream(std::unique_ptr<IByteStream> inner) noexcept
            : m_inner(std::move(inner))
        {
        }

        [[nodiscard]] IByteStream* Inner() noexcept { return m_inner.get(); }
        [[nodiscard]] const IByteStream* Inner() const noexcept { return m_inner.get(); }

        NGIN::Async::Task<void> WriteMessageAsync(NGIN::Async::TaskContext& ctx,
                                                  NGIN::Net::ConstByteSpan message,
                                                  NGIN::Async::CancellationToken token)
        {
            if (!m_inner)
            {
                co_await NGIN::Async::Task<void>::ReturnError(
                        NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::InvalidState));
                co_return;
            }

            if (message.size() > std::numeric_limits<NGIN::UInt32>::max())
            {
                co_await NGIN::Async::Task<void>::ReturnError(
                        NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::InvalidArgument));
                co_return;
            }

            std::array<NGIN::Byte, LengthBytes> header {};
            EncodeLength(static_cast<NGIN::UInt32>(message.size()), header);

            auto headerResult = co_await WriteAll(ctx,
                                                  *m_inner,
                                                  NGIN::Net::ConstByteSpan {header.data(), header.size()},
                                                  token);
            if (!headerResult)
            {
                co_await NGIN::Async::Task<void>::ReturnError(headerResult.error());
                co_return;
            }
            auto bodyResult = co_await WriteAll(ctx, *m_inner, message, token);
            if (!bodyResult)
            {
                co_await NGIN::Async::Task<void>::ReturnError(bodyResult.error());
                co_return;
            }
            co_return;
        }

        NGIN::Async::Task<NGIN::Net::ConstByteSpan> ReadMessageAsync(NGIN::Async::TaskContext& ctx,
                                                                     NGIN::Net::Buffer& messageBuffer,
                                                                     NGIN::Async::CancellationToken token)
        {
            if (!m_inner)
            {
                co_return std::unexpected(NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::InvalidState));
            }

            std::array<NGIN::Byte, LengthBytes> header {};
            auto headerResult = co_await ReadExact(ctx,
                                                   *m_inner,
                                                   NGIN::Net::ByteSpan {header.data(), header.size()},
                                                   token);
            if (!headerResult)
            {
                co_return std::unexpected(headerResult.error());
            }

            const auto messageSize = DecodeLength(header);
            if (messageSize == 0)
            {
                messageBuffer.size = 0;
                co_return NGIN::Net::ConstByteSpan {messageBuffer.data, 0};
            }

            if (!messageBuffer.data || messageBuffer.capacity < messageSize)
            {
                co_return std::unexpected(NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::InvalidArgument));
            }

            auto payloadResult = co_await ReadExact(ctx,
                                                    *m_inner,
                                                    NGIN::Net::ByteSpan {messageBuffer.data, messageSize},
                                                    token);
            if (!payloadResult)
            {
                co_return std::unexpected(payloadResult.error());
            }
            messageBuffer.size = messageSize;
            co_return NGIN::Net::ConstByteSpan {messageBuffer.data, messageSize};
        }

        NGIN::Net::NetExpected<void> Close()
        {
            if (!m_inner)
            {
                return std::unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            return m_inner->Close();
        }

    private:
        static void EncodeLength(NGIN::UInt32 length, std::array<NGIN::Byte, LengthBytes>& header) noexcept
        {
            header[0] = static_cast<NGIN::Byte>((length >> 24) & 0xFF);
            header[1] = static_cast<NGIN::Byte>((length >> 16) & 0xFF);
            header[2] = static_cast<NGIN::Byte>((length >> 8) & 0xFF);
            header[3] = static_cast<NGIN::Byte>(length & 0xFF);
        }

        static NGIN::UInt32 DecodeLength(const std::array<NGIN::Byte, LengthBytes>& header) noexcept
        {
            const auto b0 = static_cast<NGIN::UInt32>(std::to_integer<NGIN::UInt8>(header[0]));
            const auto b1 = static_cast<NGIN::UInt32>(std::to_integer<NGIN::UInt8>(header[1]));
            const auto b2 = static_cast<NGIN::UInt32>(std::to_integer<NGIN::UInt8>(header[2]));
            const auto b3 = static_cast<NGIN::UInt32>(std::to_integer<NGIN::UInt8>(header[3]));
            return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        }

        static NGIN::Async::Task<void> WriteAll(NGIN::Async::TaskContext& ctx,
                                                IByteStream& stream,
                                                NGIN::Net::ConstByteSpan data,
                                                NGIN::Async::CancellationToken token)
        {
            std::size_t offset = 0;
            while (offset < data.size())
            {
                auto task = stream.WriteAsync(ctx, data.subspan(offset), token);
                task.Schedule(ctx);
                const auto bytes = co_await task;
                if (!bytes)
                {
                    co_await NGIN::Async::Task<void>::ReturnError(bytes.error());
                    co_return;
                }
                if (*bytes == 0)
                {
                    co_await NGIN::Async::Task<void>::ReturnError(
                            NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Fault,
                                                        static_cast<int>(NetErrorCode::Disconnected)));
                    co_return;
                }
                offset += *bytes;
            }
            co_return;
        }

        static NGIN::Async::Task<void> ReadExact(NGIN::Async::TaskContext& ctx,
                                                 IByteStream& stream,
                                                 NGIN::Net::ByteSpan destination,
                                                 NGIN::Async::CancellationToken token)
        {
            std::size_t offset = 0;
            while (offset < destination.size())
            {
                auto task = stream.ReadAsync(ctx, destination.subspan(offset), token);
                task.Schedule(ctx);
                const auto bytes = co_await task;
                if (!bytes)
                {
                    co_await NGIN::Async::Task<void>::ReturnError(bytes.error());
                    co_return;
                }
                if (*bytes == 0)
                {
                    co_await NGIN::Async::Task<void>::ReturnError(
                            NGIN::Async::MakeAsyncError(NGIN::Async::AsyncErrorCode::Fault,
                                                        static_cast<int>(NetErrorCode::Disconnected)));
                    co_return;
                }
                offset += *bytes;
            }
            co_return;
        }

        std::unique_ptr<IByteStream> m_inner {};
    };
}// namespace NGIN::Net::Transport::Filters

