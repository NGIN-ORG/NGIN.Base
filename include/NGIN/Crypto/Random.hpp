#pragma once

#include <cstddef>
#include <vector>
#include <string>

#include <NGIN/Crypto/Random.impl.hpp>

namespace NGIN::Crypto::Random
{

    // Utility for random keys, IVs, nonces
    inline std::vector<uint8_t> GenerateKey(size_t length)
    {
        return GetBytes(length);
    }
    inline std::vector<uint8_t> GenerateIV(size_t length)
    {
        return GetBytes(length);
    }
    inline std::vector<uint8_t> GenerateNonce(size_t length)
    {
        return GetBytes(length);
    }

    inline std::vector<unsigned char> GenerateRandomBytes(size_t length)
    {
        return GetBytes(length);
    }

}// namespace NGIN::Crypto::Random
