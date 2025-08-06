#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace NGIN::Crypto
{

    // PBKDF2, scrypt, HKDF interfaces
    class PBKDF2
    {
    public:
        static std::vector<uint8_t> DeriveKey(const std::vector<uint8_t>& password, const std::vector<uint8_t>& salt, size_t iterations, size_t keyLen);
    };

    // ...

}// namespace NGIN::Crypto
