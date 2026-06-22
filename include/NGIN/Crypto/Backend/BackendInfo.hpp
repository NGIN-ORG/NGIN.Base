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

        constexpr BackendInfo(
                BackendKind      kind,
                std::string_view name,
                std::string_view version       = {},
                std::string_view source        = {},
                std::string_view buildOption   = {},
                std::string_view packageName   = {},
                bool             fipsCapable   = false,
                bool             fipsValidated = false) noexcept
            : m_kind {kind},
              m_name {name},
              m_version {version},
              m_source {source},
              m_buildOption {buildOption},
              m_packageName {packageName},
              m_fipsCapable {fipsCapable},
              m_fipsValidated {fipsValidated}
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

        [[nodiscard]] constexpr std::string_view Source() const noexcept
        {
            return m_source;
        }

        [[nodiscard]] constexpr std::string_view BuildOption() const noexcept
        {
            return m_buildOption;
        }

        [[nodiscard]] constexpr std::string_view PackageName() const noexcept
        {
            return m_packageName;
        }

        [[nodiscard]] constexpr bool IsFipsCapable() const noexcept
        {
            return m_fipsCapable;
        }

        [[nodiscard]] constexpr bool IsFipsValidated() const noexcept
        {
            return m_fipsValidated;
        }

    private:
        BackendKind      m_kind {BackendKind::Platform};
        std::string_view m_name {"none"};
        std::string_view m_version {};
        std::string_view m_source {};
        std::string_view m_buildOption {};
        std::string_view m_packageName {};
        bool             m_fipsCapable {false};
        bool             m_fipsValidated {false};
    };
}// namespace NGIN::Crypto::Backend
