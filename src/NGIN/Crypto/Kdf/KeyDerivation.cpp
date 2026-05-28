#include <NGIN/Crypto/Kdf/KeyDerivation.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Kdf
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] bool IsValid(const KeyDerivationParameters& parameters) noexcept
        {
            switch (parameters.Algorithm())
            {
                case KdfAlgorithm::HkdfSha256:
                case KdfAlgorithm::HkdfSha512:
                    return parameters.Hkdf() != nullptr;
                case KdfAlgorithm::Pbkdf2Sha256:
                case KdfAlgorithm::Pbkdf2Sha512:
                    return parameters.Pbkdf2() != nullptr && parameters.Pbkdf2()->iterations > 0;
                case KdfAlgorithm::Argon2id:
                    return parameters.Argon2id() != nullptr &&
                           parameters.Argon2id()->memoryKiB > 0 &&
                           parameters.Argon2id()->iterations > 0 &&
                           parameters.Argon2id()->parallelism > 0;
            }

            return false;
        }
    }// namespace

    CryptoExpected<void> DeriveKeyInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const KeyDerivationParameters&              parameters,
            ByteSpan                                    output) noexcept
    {
        if (output.empty())
        {
            return OutputBufferTooSmall();
        }
        if (!IsValid(parameters))
        {
            return InvalidArgument();
        }

        auto supported = context.EnsureSupports(parameters.Algorithm());
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        switch (parameters.Algorithm())
        {
            case KdfAlgorithm::HkdfSha256:
            case KdfAlgorithm::HkdfSha512:
                return context.HkdfInto(
                        parameters.Algorithm(),
                        parameters.Hkdf()->inputKeyMaterial,
                        parameters.Hkdf()->salt,
                        parameters.Hkdf()->info,
                        output);
            case KdfAlgorithm::Pbkdf2Sha256:
            case KdfAlgorithm::Pbkdf2Sha512:
                return context.Pbkdf2Into(
                        parameters.Algorithm(),
                        parameters.Pbkdf2()->password,
                        parameters.Pbkdf2()->salt,
                        parameters.Pbkdf2()->iterations,
                        output);
            case KdfAlgorithm::Argon2id:
                return UnsupportedAlgorithm();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<ByteBuffer> DeriveKey(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const KeyDerivationParameters&              parameters,
            NGIN::UIntSize                              outputSize)
    {
        if (outputSize == 0)
        {
            return OutputBufferTooSmall();
        }

        auto output = MakeByteBuffer(outputSize);
        auto result = DeriveKeyInto(context, parameters, ByteSpan {output.data(), output.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }
}// namespace NGIN::Crypto::Kdf
