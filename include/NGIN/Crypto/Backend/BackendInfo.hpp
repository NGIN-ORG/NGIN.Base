#pragma once

#include <NGIN/Primitives.hpp>

#include <string_view>

namespace NGIN::Crypto::Backend
{
    /// @brief Neutral backend families reported for diagnostics and policy checks.
    enum class BackendKind : NGIN::UInt8
    {
        Platform,
        ExternalPackage,
        Test,
    };

    /// @brief Stable diagnostic metadata for a crypto backend.
    class BackendInfo
    {
    public:
        constexpr BackendInfo() noexcept = default;

        constexpr BackendInfo(BackendKind kind, std::string_view name, std::string_view version = {}) noexcept
            : m_kind {kind}, m_name {name}, m_version {version}
        {
        }

        [[nodiscard]] constexpr BackendKind Kind() const noexcept
        {
            return m_kind;
        }

        [[nodiscard]] constexpr std::string_view Name() const noexcept
        {
            return m_name;
        }

        [[nodiscard]] constexpr std::string_view Version() const noexcept
        {
            return m_version;
        }

    private:
        BackendKind      m_kind {BackendKind::Platform};
        std::string_view m_name {"none"};
        std::string_view m_version {};
    };
}// namespace NGIN::Crypto::Backend
