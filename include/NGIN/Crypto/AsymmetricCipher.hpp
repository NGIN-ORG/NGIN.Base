#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace NGIN::Crypto
{

    // Abstract base for asymmetric ciphers
    class AsymmetricCipher
    {
    public:
        virtual ~AsymmetricCipher()                                                  = default;
        virtual std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plaintext)  = 0;
        virtual std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& ciphertext) = 0;
        virtual std::string SerializePublicKey() const                               = 0;
        virtual std::string SerializePrivateKey() const                              = 0;
        // ...
    };

    // Factory for common asymmetric ciphers
    // std::unique_ptr<AsymmetricCipher> CreateRSA(...);
    // std::unique_ptr<AsymmetricCipher> CreateECC(...);
    // ...

}// namespace NGIN::Crypto
