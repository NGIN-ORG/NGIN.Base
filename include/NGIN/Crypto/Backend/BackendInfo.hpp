/// @file BackendInfo.hpp
/// @brief Stable diagnostic metadata describing a crypto backend.
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
        /// @brief Constructs metadata for the absence of a selected backend.
        constexpr BackendInfo() noexcept = default;

        /// @brief Constructs backend metadata from non-owning string views.
        /// @warning The referenced strings must outlive this object.
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

        /// @brief Returns the backend family.
        [[nodiscard]] constexpr BackendKind Kind() const noexcept
        {
            return m_kind;
        }

        /// @brief Returns the backend's diagnostic name.
        [[nodiscard]] constexpr std::string_view Name() const noexcept
        {
            return m_name;
        }

        /// @brief Returns the backend or provider version.
        [[nodiscard]] constexpr std::string_view Version() const noexcept
        {
            return m_version;
        }

        /// @brief Returns the source or implementation description.
        [[nodiscard]] constexpr std::string_view Source() const noexcept
        {
            return m_source;
        }

        /// @brief Returns the build option that enabled this backend.
        [[nodiscard]] constexpr std::string_view BuildOption() const noexcept
        {
            return m_buildOption;
        }

        /// @brief Returns the package-provider name for an external backend.
        [[nodiscard]] constexpr std::string_view PackageName() const noexcept
        {
            return m_packageName;
        }

        /// @brief Returns whether the backend can operate in a FIPS-capable configuration.
        [[nodiscard]] constexpr bool IsFipsCapable() const noexcept
        {
            return m_fipsCapable;
        }

        /// @brief Returns whether the active backend configuration is FIPS validated.
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
