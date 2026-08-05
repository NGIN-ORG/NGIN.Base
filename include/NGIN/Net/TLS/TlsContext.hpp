/// @file TlsContext.hpp
/// @brief Immutable provider-neutral TLS context.
#pragma once

#include <memory>
#include <string_view>

#include <NGIN/Defines.hpp>
#include <NGIN/Net/TLS/TlsError.hpp>
#include <NGIN/Net/TLS/TlsTypes.hpp>

namespace NGIN::Net::TLS
{
    namespace detail
    {
        class TlsContextState;
    }

    class TlsStream;

    /// @brief Copyable immutable TLS provider configuration shared by streams.
    class NGIN_NETTLS_API TlsContext final
    {
    public:
        /// @brief Constructs an invalid context.
        TlsContext() noexcept = default;

        /// @brief Creates a client context and validates trust, credentials, protocols, and ALPN policy.
        [[nodiscard]] static TlsExpected<TlsContext> CreateClient(TlsClientContextOptions options = {});
        /// @brief Creates a server context and validates credentials, protocols, and client-authentication policy.
        [[nodiscard]] static TlsExpected<TlsContext> CreateServer(TlsServerContextOptions options);

        /// @brief Returns whether provider state was created successfully.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns whether this context was configured for client use.
        [[nodiscard]] bool IsClient() const noexcept;
        /// @brief Returns the name of the selected TLS provider.
        [[nodiscard]] std::string_view ProviderName() const noexcept;

    private:
        explicit TlsContext(std::shared_ptr<const detail::TlsContextState> state) noexcept;

        std::shared_ptr<const detail::TlsContextState> m_state;

        friend class TlsStream;
    };

    /// @brief Returns whether a supported TLS provider is available at runtime.
    [[nodiscard]] NGIN_NETTLS_API bool TlsProviderAvailable() noexcept;
}// namespace NGIN::Net::TLS
