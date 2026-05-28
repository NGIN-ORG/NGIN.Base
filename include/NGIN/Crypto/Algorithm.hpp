#pragma once

namespace NGIN::Crypto
{
    /// @brief Broad algorithm families used for capability reporting and diagnostics.
    enum class AlgorithmFamily
    {
        Random,
        Hash,
        Mac,
        Kdf,
        Aead,
        KeyAgreement,
        Signature,
        Encoding,
        Certificate,
        Token,
    };
}// namespace NGIN::Crypto
