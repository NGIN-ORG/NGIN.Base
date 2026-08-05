#include <NGIN/Net/TLS/TlsContext.hpp>

#include "TlsProvider.hpp"

#include <utility>

namespace NGIN::Net::TLS
{
    namespace
    {
#if !defined(NGIN_BASE_TLS_HAS_OPENSSL)
        [[nodiscard]] TlsError ProviderUnavailable()
        {
            return detail::MakeTlsError(
                    TlsErrorCategory::Provider,
                    TlsErrorCode::ProviderUnavailable,
                    "no TLS provider is enabled; configure NGIN_BASE_TLS_WITH_OPENSSL=ON");
        }
#endif
    }// namespace

    TlsContext::TlsContext(std::shared_ptr<const detail::TlsContextState> state) noexcept
        : m_state(std::move(state))
    {
    }

    TlsExpected<TlsContext> TlsContext::CreateClient(TlsClientContextOptions options)
    {
#if defined(NGIN_BASE_TLS_HAS_OPENSSL)
        auto state = detail::CreateOpenSslClientContext(std::move(options));
        if (!state.HasValue())
        {
            return NGIN::Utilities::Unexpected(state.Error());
        }
        return TlsContext {std::move(state.Value())};
#else
        static_cast<void>(options);
        return NGIN::Utilities::Unexpected(ProviderUnavailable());
#endif
    }

    TlsExpected<TlsContext> TlsContext::CreateServer(TlsServerContextOptions options)
    {
#if defined(NGIN_BASE_TLS_HAS_OPENSSL)
        auto state = detail::CreateOpenSslServerContext(std::move(options));
        if (!state.HasValue())
        {
            return NGIN::Utilities::Unexpected(state.Error());
        }
        return TlsContext {std::move(state.Value())};
#else
        static_cast<void>(options);
        return NGIN::Utilities::Unexpected(ProviderUnavailable());
#endif
    }

    bool TlsContext::IsValid() const noexcept
    {
        return static_cast<bool>(m_state);
    }

    bool TlsContext::IsClient() const noexcept
    {
        return m_state && m_state->IsClient();
    }

    std::string_view TlsContext::ProviderName() const noexcept
    {
        return m_state ? m_state->ProviderName() : std::string_view {};
    }

    bool TlsProviderAvailable() noexcept
    {
#if defined(NGIN_BASE_TLS_HAS_OPENSSL)
        return true;
#else
        return false;
#endif
    }
}// namespace NGIN::Net::TLS
