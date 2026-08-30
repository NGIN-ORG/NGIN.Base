/// @file KeyPair.hpp
/// @brief Strongly typed public/private key pair values.
#pragma once

namespace NGIN::Crypto::Asymmetric
{
    /// @brief Public/private key pair whose member types encode their algorithm.
    template<class TPublicKey, class TPrivateKey>
    struct KeyPair
    {
        TPublicKey  publicKey {};
        TPrivateKey privateKey {};
    };
}// namespace NGIN::Crypto::Asymmetric
