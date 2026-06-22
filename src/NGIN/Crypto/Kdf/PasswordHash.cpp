#include <NGIN/Crypto/Kdf/PasswordHash.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Kdf
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] bool IsValid(PasswordHashOptions options) noexcept
        {
            return options.memoryKiB > 0 && options.iterations > 0 && options.parallelism > 0;
        }
    }// namespace

    CryptoExpected<PasswordHashString> HashPassword(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            password,
            PasswordHashOptions                         options)
    {
        if (!IsValid(options))
        {
            return InvalidArgument();
        }

        auto supported = context.EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        auto encoded = context.HashPassword(password, options.memoryKiB, options.iterations, options.parallelism);
        if (!encoded.HasValue())
        {
            return encoded.Error();
        }

        return PasswordHashString {std::move(encoded.Value())};
    }

    CryptoExpected<void> VerifyPassword(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            password,
            std::string_view                            encodedHash) noexcept
    {
        if (encodedHash.empty())
        {
            return InvalidArgument();
        }

        auto supported = context.EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.VerifyPasswordHash(password, encodedHash);
    }

    CryptoExpected<bool> PasswordHashNeedsRehash(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            encodedHash,
            PasswordHashOptions                         options) noexcept
    {
        if (encodedHash.empty() || !IsValid(options))
        {
            return InvalidArgument();
        }

        auto supported = context.EnsureSupports(KdfAlgorithm::Argon2id);
        if (!supported.HasValue())
        {
            return supported.Error();
        }

        return context.PasswordHashNeedsRehash(encodedHash, options.memoryKiB, options.iterations, options.parallelism);
    }
}// namespace NGIN::Crypto::Kdf
