#pragma once

#include <NGIN/Crypto/Asymmetric/KeyTypes.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Signatures
{
    /// @brief Fixed-size signature bytes.
    template<NGIN::UIntSize Size>
    using Signature = FixedBytes<Size>;

    using Ed25519Signature = Signature<64>;

    /// @brief Returns the fixed signature size in bytes, or zero for variable/unsupported signatures.
    [[nodiscard]] constexpr NGIN::UIntSize SignatureSize(SignatureAlgorithm algorithm) noexcept
    {
        return NGIN::Crypto::Asymmetric::GetSignatureKeySizes(algorithm).signatureSize;
    }
}// namespace NGIN::Crypto::Signatures
