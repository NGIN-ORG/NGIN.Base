#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <memory>

namespace NGIN::Crypto
{

    // HMAC interface
    class HMAC
    {
    public:
        virtual ~HMAC()                                      = default;
        virtual void SetKey(const std::vector<uint8_t>& key) = 0;
        virtual void Update(const void* data, size_t len)    = 0;
        virtual std::vector<uint8_t> Final()                 = 0;
        virtual void Reset()                                 = 0;
        virtual size_t OutputSize() const                    = 0;
    };

    // Factory for HMAC with different hashers
    // std::unique_ptr<HMAC> CreateHMAC_SHA256(const std::vector<uint8_t>& key);
    // ...

}// namespace NGIN::Crypto
