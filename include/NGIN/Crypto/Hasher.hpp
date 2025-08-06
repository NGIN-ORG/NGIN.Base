#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <memory>

namespace NGIN::Crypto
{

    // Abstract base for hash functions
    class Hasher
    {
    public:
        virtual ~Hasher()                                 = default;
        virtual void Update(const void* data, size_t len) = 0;
        virtual std::vector<uint8_t> Final()              = 0;
        virtual void Reset()                              = 0;
        virtual size_t OutputSize() const                 = 0;
    };

    // Utility: Hash to hex string
    std::string HashToHex(const std::vector<uint8_t>& hash);
    // Utility: Hash to base64 string
    std::string HashToBase64(const std::vector<uint8_t>& hash);

    // Factory for common hashers
    std::unique_ptr<Hasher> CreateSHA256();
    std::unique_ptr<Hasher> CreateSHA512();
    std::unique_ptr<Hasher> CreateMD5();
    // Add more as needed (e.g., Blake2)

}// namespace NGIN::Crypto
