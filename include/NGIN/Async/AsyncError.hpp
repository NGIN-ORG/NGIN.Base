/// @file AsyncError.hpp
/// @brief Typed domain or infrastructure payload within the asynchronous error channel.
#pragma once
#include <NGIN/Async/AsyncFault.hpp>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>

namespace NGIN::Async
{
    /// @brief Owns a domain/fault payload inline, or explicitly retains immutable error storage.
    /// @details Lvalue inspection is immutable for both representations. Rvalue extraction moves an inline
    /// payload or copies a retained copyable payload. Forward retained move-only errors as a whole.
    template<typename E>
    class AsyncError final
    {
    public:
        static AsyncError FromDomain(E value) { return AsyncError(std::in_place_index<0>, std::move(value)); }
        static AsyncError FromFault(AsyncFault value) noexcept { return AsyncError(std::in_place_index<1>, std::move(value)); }
        /// Retains immutable error storage owned elsewhere. Do not store this alias back in its owner.
        static AsyncError Retain(std::shared_ptr<const AsyncError> value)
        {
            if (!value)
                throw std::invalid_argument("Retained async error requires an owner");
            return AsyncError(std::in_place_index<2>, std::move(value));
        }
        bool     IsDomainError() const noexcept { return Payload().m_payload.index() == 0; }
        bool     IsFault() const noexcept { return Payload().m_payload.index() == 1; }
        bool     IsRetained() const noexcept { return m_payload.index() == 2; }
        const E& DomainError() const&
        {
            assert(IsDomainError());
            return std::get<0>(Payload().m_payload);
        }
        E DomainError() &&
        {
            assert(IsDomainError());
            if (!IsRetained())
                return std::move(std::get<0>(m_payload));
            if constexpr (std::is_copy_constructible_v<E>)
                return std::get<0>(Payload().m_payload);
            else
                throw std::logic_error("Retained move-only error must be forwarded as AsyncError");
        }
        const AsyncFault& Fault() const&
        {
            assert(IsFault());
            return std::get<1>(Payload().m_payload);
        }
        AsyncFault Fault() &&
        {
            assert(IsFault());
            if (IsRetained())
                return std::get<1>(Payload().m_payload);
            return std::move(std::get<1>(m_payload));
        }

    private:
        template<std::size_t Index, typename V>
        AsyncError(std::in_place_index_t<Index> tag, V&& value) : m_payload(tag, std::forward<V>(value))
        {}
        const AsyncError& Payload() const noexcept
        {
            const AsyncError* value = this;
            while (value->IsRetained())
                value = std::get<2>(value->m_payload).get();
            return *value;
        }
        std::variant<E, AsyncFault, std::shared_ptr<const AsyncError>> m_payload;
    };
}// namespace NGIN::Async
