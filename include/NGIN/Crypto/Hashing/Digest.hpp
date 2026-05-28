#pragma once

#include <NGIN/Crypto/Memory/Secret.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Hashing
{
    /// @brief Fixed-size public hash digest bytes.
    template<NGIN::UIntSize Size>
    using Digest = FixedBytes<Size>;

    using Sha256Digest = Digest<32>;
    using Sha512Digest = Digest<64>;

    /// @brief Fixed-size secret digest-like storage for keyed derivation internals.
    template<NGIN::UIntSize Size>
    using SecretDigest = NGIN::Crypto::Memory::FixedSecret<Size>;
}// namespace NGIN::Crypto::Hashing
