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

    class NGIN_NETTLS_API TlsContext final
    {
    public:
        TlsContext() noexcept = default;

        [[nodiscard]] static TlsExpected<TlsContext> CreateClient(TlsClientContextOptions options = {});
        [[nodiscard]] static TlsExpected<TlsContext> CreateServer(TlsServerContextOptions options);

        [[nodiscard]] bool             IsValid() const noexcept;
        [[nodiscard]] bool             IsClient() const noexcept;
        [[nodiscard]] std::string_view ProviderName() const noexcept;

    private:
        explicit TlsContext(std::shared_ptr<const detail::TlsContextState> state) noexcept;

        std::shared_ptr<const detail::TlsContextState> m_state;

        friend class TlsStream;
    };

    [[nodiscard]] NGIN_NETTLS_API bool TlsProviderAvailable() noexcept;
}// namespace NGIN::Net::TLS
