#pragma once

#include <NGIN/Crypto/Symmetric/Aead.hpp>

namespace NGIN::Crypto::Symmetric
{
    using Aes128GcmKey = NGIN::Crypto::Memory::FixedSecret<16>;
    using Aes256GcmKey = NGIN::Crypto::Memory::FixedSecret<32>;
    using AesGcmNonce  = FixedBytes<12>;
    using AesGcmTag    = StandardAeadTag;
}// namespace NGIN::Crypto::Symmetric
