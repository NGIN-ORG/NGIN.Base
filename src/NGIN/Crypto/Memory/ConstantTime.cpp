#include <NGIN/Crypto/Memory/ConstantTime.hpp>

#include <cstddef>

namespace NGIN::Crypto::Memory
{
    bool ConstantTimeEqual(ConstByteSpan left, ConstByteSpan right) noexcept
    {
        if (left.size() != right.size())
        {
            return false;
        }

        NGIN::UInt8 difference = 0;
        for (NGIN::UIntSize i = 0; i < left.size(); ++i)
        {
            difference = static_cast<NGIN::UInt8>(
                    difference |
                    (std::to_integer<NGIN::UInt8>(left[i]) ^ std::to_integer<NGIN::UInt8>(right[i])));
        }

        return difference == 0;
    }
}// namespace NGIN::Crypto::Memory
