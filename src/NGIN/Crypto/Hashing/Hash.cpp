#include <NGIN/Crypto/Hashing/Hash.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Hashing
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
    }// namespace

    CryptoExpected<void> HashInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            HashAlgorithm                               algorithm,
            ConstByteSpan                               input,
            ByteSpan                                    output) noexcept
    {
        (void) input;

        if (output.size() != DigestSize(algorithm))
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

    CryptoExpected<ByteBuffer> Hash(
            const NGIN::Crypto::Backend::CryptoContext& context,
            HashAlgorithm                               algorithm,
            ConstByteSpan                               input)
    {
        auto output = MakeByteBuffer(DigestSize(algorithm));
        auto result = HashInto(context, algorithm, input, ByteSpan {output.data(), output.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<void> Sha256Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            ConstByteSpan                               input,
            Sha256Digest&                               output) noexcept
    {
        return HashInto(context, HashAlgorithm::Sha256, input, ByteSpan {output.data(), output.size()});
    }

    CryptoExpected<void> Sha512Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            ConstByteSpan                               input,
            Sha512Digest&                               output) noexcept
    {
        return HashInto(context, HashAlgorithm::Sha512, input, ByteSpan {output.data(), output.size()});
    }

    CryptoExpected<Sha256Digest> Sha256(const NGIN::Crypto::Backend::CryptoContext& context, ConstByteSpan input)
    {
        Sha256Digest output {};
        auto         result = Sha256Into(context, input, output);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<Sha512Digest> Sha512(const NGIN::Crypto::Backend::CryptoContext& context, ConstByteSpan input)
    {
        Sha512Digest output {};
        auto         result = Sha512Into(context, input, output);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }
}// namespace NGIN::Crypto::Hashing
