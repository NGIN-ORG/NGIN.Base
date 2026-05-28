#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Crypto
{
    /// @brief Owned non-secret byte buffer for ordinary crypto inputs and outputs.
    using ByteBuffer = NGIN::Containers::Vector<NGIN::Byte>;

    /// @brief Creates a byte buffer with `size` zero-initialized bytes.
    [[nodiscard]] inline ByteBuffer MakeByteBuffer(NGIN::UIntSize size)
    {
        ByteBuffer buffer;
        buffer.Reserve(size);
        for (NGIN::UIntSize i = 0; i < size; ++i)
        {
            buffer.PushBack(NGIN::Byte {0});
        }
        return buffer;
    }
}// namespace NGIN::Crypto
