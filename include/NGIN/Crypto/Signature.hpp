#pragma once

#include <cstddef>
#include <vector>
#include <string>

namespace NGIN::Crypto
{

    // Abstract base for digital signatures
    class Signer
    {
    public:
        virtual ~Signer()                                                      = default;
        virtual std::vector<uint8_t> Sign(const std::vector<uint8_t>& message) = 0;
    };

    class Verifier
    {
    public:
        virtual ~Verifier()                                                                             = default;
        virtual bool Verify(const std::vector<uint8_t>& message, const std::vector<uint8_t>& signature) = 0;
    };

    // Factory for common signers/verifiers
    // std::unique_ptr<Signer> CreateRSASigner(...);
    // std::unique_ptr<Verifier> CreateRSAVerifier(...);
    // ...

}// namespace NGIN::Crypto
