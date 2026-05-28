#include <NGIN/Crypto/Mac/Hmac.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Mac/HmacSha256.hpp>
#include <NGIN/Crypto/Mac/HmacSha512.hpp>

namespace NGIN::Crypto::Mac
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        [[nodiscard]] constexpr CryptoError InvalidTag() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidTag};
        }
    }// namespace

    CryptoExpected<void> MacInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            MacAlgorithm                                algorithm,
            NGIN::Crypto::Memory::SecretView            key,
            ConstByteSpan                               input,
            ByteSpan                                    output) noexcept
    {
        (void) key;
        (void) input;

        if (output.size() != MacTagSize(algorithm))
        {
            return OutputBufferTooSmall();
        }

        auto supported = context.EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return UnsupportedAlgorithm();
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
        (void) key;
        (void) input;

        if (expectedTag.size() != MacTagSize(algorithm))
        {
            return InvalidTag();
        }

        auto supported = context.EnsureSupports(algorithm);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return UnsupportedAlgorithm();
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
