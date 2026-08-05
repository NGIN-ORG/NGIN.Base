/// @file SymbolTable.hpp
/// @brief Mapping between interned strings and compact symbol identifiers.
#pragma once

#include <NGIN/Meta/SymbolId.hpp>
#include <NGIN/Utilities/StringInterner.hpp>

#include <limits>
#include <string_view>

namespace NGIN::Utilities
{
    /// @brief Owns interned symbol text and provides stable numeric identifiers.
    /// @tparam Allocator Allocator used for interned string storage.
    /// @tparam ThreadPolicy Locking policy used to synchronize access.
    template<class Allocator    = NGIN::Memory::SystemAllocator,
             class ThreadPolicy = detail::NullMutex>
    class SymbolTable
    {
    public:
        /// @brief Underlying string interner type.
        using InternerType = StringInterner<Allocator, ThreadPolicy>;

        /// @brief Returns the existing identifier for text or interns a new entry.
        /// @param value Symbol text copied into the table when absent.
        /// @return Valid symbol identifier, or an invalid identifier if capacity is exhausted.
        [[nodiscard]] NGIN::Meta::SymbolId Intern(std::string_view value)
        {
            const typename InternerType::IdType id = m_interner.InsertOrGet(value);
            if (id == InternerType::INVALID_ID)
                return {};

            if (id >= std::numeric_limits<NGIN::Meta::SymbolId::ValueType>::max() - 1u)
                return {};

            return NGIN::Meta::SymbolId {
                    static_cast<NGIN::Meta::SymbolId::ValueType>(id + 1u)};
        }

        /// @brief Looks up an already interned symbol without inserting it.
        /// @param value Symbol text to find.
        /// @param out Receives the identifier on success and is unchanged on failure.
        /// @return `true` when the symbol exists.
        [[nodiscard]] bool TryGet(std::string_view value, NGIN::Meta::SymbolId& out) const noexcept
        {
            typename InternerType::IdType id = InternerType::INVALID_ID;
            if (!m_interner.TryGetId(value, id))
                return false;

            out = NGIN::Meta::SymbolId {
                    static_cast<NGIN::Meta::SymbolId::ValueType>(id + 1u)};
            return true;
        }

        /// @brief Returns the text associated with an identifier.
        /// @param id Identifier produced by this table.
        /// @return Interned text, or an empty view for an invalid or unknown identifier.
        /// @warning The view is invalidated by `Clear` or destruction of the table.
        [[nodiscard]] std::string_view View(NGIN::Meta::SymbolId id) const noexcept
        {
            if (!id.IsValid())
                return {};

            return m_interner.View(static_cast<typename InternerType::IdType>(id.value - 1u));
        }

        /// @brief Returns the number of interned symbols.
        [[nodiscard]] UIntSize Size() const noexcept
        {
            return m_interner.Size();
        }

        /// @brief Removes all symbols and invalidates outstanding text views.
        void Clear() noexcept
        {
            m_interner.Clear();
        }

    private:
        InternerType m_interner {};
    };
}// namespace NGIN::Utilities
