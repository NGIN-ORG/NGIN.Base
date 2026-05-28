#pragma once

#include <NGIN/Crypto/Symmetric/Aead.hpp>

namespace NGIN::Crypto::Symmetric
{
    using ChaCha20Poly1305Key   = NGIN::Crypto::Memory::FixedSecret<32>;
    using ChaCha20Poly1305Nonce = FixedBytes<12>;
    using ChaCha20Poly1305Tag   = StandardAeadTag;
}// namespace NGIN::Crypto::Symmetric
