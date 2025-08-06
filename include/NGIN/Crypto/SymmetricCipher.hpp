#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace NGIN::Crypto
{

    // Abstract base for symmetric ciphers
    class SymmetricCipher
    {
    public:
        virtual ~SymmetricCipher()                                                   = default;
        virtual std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plaintext)  = 0;
        virtual std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& ciphertext) = 0;
        virtual void SetKey(const std::vector<uint8_t>& key)                         = 0;
        virtual void SetIV(const std::vector<uint8_t>& iv)                           = 0;
        virtual size_t BlockSize() const                                             = 0;
    };

    // Factory for common ciphers
    // AES, ChaCha20, etc. (implementations can be added later)
    // std::unique_ptr<SymmetricCipher> CreateAES_ECB(const std::vector<uint8_t>& key);
    // std::unique_ptr<SymmetricCipher> CreateAES_CBC(const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv);
    // ...

}// namespace NGIN::Crypto
