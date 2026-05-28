#include <NGIN/Crypto/Mac/Hmac.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Mac/HmacSha256.hpp>
#include <NGIN/Crypto/Mac/HmacSha512.hpp>
#include <NGIN/Crypto/Memory/ConstantTime.hpp>

namespace NGIN::Crypto::Mac
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        [[nodiscard]] constexpr CryptoError InvalidTag() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidTag};
        }

        [[nodiscard]] constexpr CryptoError AuthenticationFailed() noexcept
        {
            return CryptoError {CryptoErrorCode::AuthenticationFailed};
        }
    }// namespace

    CryptoExpected<void> MacInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            ByteSpan                                    output) noexcept
    {
        if (output.size() != MacTagSize(algorithm))
        {
            return OutputBufferTooSmall();
        }

        return context.MacInto(algorithm, key, input, output);
    }

    CryptoExpected<ByteBuffer> ComputeMac(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input)
    {
        auto output = MakeByteBuffer(MacTagSize(algorithm));
        auto result = MacInto(context, algorithm, key, input, ByteSpan {output.data(), output.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<void> VerifyMac(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            ConstByteSpan                               expectedTag) noexcept
    {
        if (expectedTag.size() != MacTagSize(algorithm))
        {
            return InvalidTag();
        }

        auto computed = MakeByteBuffer(MacTagSize(algorithm));
        auto result   = MacInto(context, algorithm, key, input, ByteSpan {computed.data(), computed.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        if (!NGIN::Crypto::Memory::ConstantTimeEqual(
                    ConstByteSpan {computed.data(), computed.Size()},
                    expectedTag))
        {
            return AuthenticationFailed();
        }

        return {};
    }

    CryptoExpected<HmacSha256Tag> HmacSha256(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input)
    {
        HmacSha256Tag output {};
        auto          result = HmacSha256Into(context, key, input, output);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<HmacSha512Tag> HmacSha512(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input)
    {
        HmacSha512Tag output {};
        auto          result = HmacSha512Into(context, key, input, output);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }
}// namespace NGIN::Crypto::Mac
