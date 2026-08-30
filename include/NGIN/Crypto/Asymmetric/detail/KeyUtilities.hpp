/// @file KeyUtilities.hpp
/// @brief Internal construction helpers shared by asymmetric key value types.
#pragma once

#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Asymmetric::detail
{
    [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
    {
        return CryptoError {CryptoErrorCode::InvalidKey};
    }

    template<NGIN::UIntSize Size>
    [[nodiscard]] constexpr FixedBytes<Size> CopyFixedBytes(ConstByteSpan bytes) noexcept
    {
        FixedBytes<Size> output {};
        for (NGIN::UIntSize i = 0; i < Size; ++i)
        {
            output[i] = bytes[i];
        }
        return output;
    }
}// namespace NGIN::Crypto::Asymmetric::detail
