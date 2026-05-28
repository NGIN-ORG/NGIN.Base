#pragma once

#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Random/SecureRandom.hpp>

namespace NGIN::Crypto::Random
{
    /// @brief Returns `size` secure random bytes in an owned buffer.
    [[nodiscard]] inline CryptoExpected<ByteBuffer> RandomBytes(NGIN::UIntSize size)
    {
        auto buffer = MakeByteBuffer(size);
        auto fill   = Fill(ByteSpan {buffer.data(), buffer.Size()});
        if (!fill.HasValue())
        {
            return fill.Error();
        }
        return buffer;
    }

    /// @brief Returns a fixed-size secure random byte array.
    template<NGIN::UIntSize Size>
    [[nodiscard]] CryptoExpected<FixedBytes<Size>> RandomBytes() noexcept
    {
        FixedBytes<Size> output {};
        auto             fill = Fill(ByteSpan {output.data(), output.size()});
        if (!fill.HasValue())
        {
            return fill.Error();
        }
        return output;
    }
}// namespace NGIN::Crypto::Random
