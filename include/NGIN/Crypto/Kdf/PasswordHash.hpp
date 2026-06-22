#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Result.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace NGIN::Crypto::Kdf
{
    /// @brief Parameters for backend-backed Argon2id password hash strings.
    struct PasswordHashOptions
    {
        NGIN::UInt32 memoryKiB {64 * 1024};
        NGIN::UInt32 iterations {3};
        NGIN::UInt32 parallelism {1};
    };

    /// @brief PHC-style encoded password hash string suitable for storage.
    class PasswordHashString
    {
    public:
        PasswordHashString() = default;

        explicit PasswordHashString(std::string value) noexcept
            : m_value {std::move(value)}
        {
        }

        [[nodiscard]] std::string_view Value() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] const std::string& String() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return m_value.empty();
        }

    private:
        std::string m_value;
    };

    /// @brief Hashes a password into a backend-owned PHC-style Argon2id string.
    [[nodiscard]] CryptoExpected<PasswordHashString> HashPassword(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            password,
            PasswordHashOptions                         options = {});

    /// @brief Verifies a password against a PHC-style Argon2id hash string.
    [[nodiscard]] CryptoExpected<void> VerifyPassword(
            const NGIN::Crypto::Backend::CryptoContext& context,
            NGIN::Crypto::Memory::SecretView            password,
            std::string_view                            encodedHash) noexcept;

    /// @brief Reports whether a PHC-style Argon2id hash should be regenerated with the supplied cost options.
    [[nodiscard]] CryptoExpected<bool> PasswordHashNeedsRehash(
            const NGIN::Crypto::Backend::CryptoContext& context,
            std::string_view                            encodedHash,
            PasswordHashOptions                         options = {}) noexcept;
}// namespace NGIN::Crypto::Kdf
