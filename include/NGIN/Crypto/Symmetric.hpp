#pragma once

#include <string>
#include <vector>

namespace NGIN::Crypto
{

    // Example AES interface (stub)
    struct AES
    {
        static std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& key);
        static std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key);
    };

}// namespace NGIN::Crypto
