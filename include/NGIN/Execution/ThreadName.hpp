/// @file ThreadName.hpp
/// @brief Fixed-size thread name helper (owned, truncating).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace NGIN::Execution
{
    /// @brief Owned, null-terminated thread name with deterministic truncation.
    class ThreadName final
    {
    public:
        /// @brief Maximum retained UTF-8 bytes, excluding the null terminator.
        static constexpr std::size_t MaxBytes = 63;

        /// @brief Constructs an empty name.
        constexpr ThreadName() noexcept = default;

        /// @brief Constructs a name, truncating it to `MaxBytes`.
        constexpr explicit ThreadName(std::string_view name) noexcept
        {
            Assign(name);
        }

        /// @brief Replaces the name, truncating it to `MaxBytes`.
        constexpr void Assign(std::string_view name) noexcept
        {
            const std::size_t len = std::min<std::size_t>(name.size(), MaxBytes);
            for (std::size_t i = 0; i < len; ++i)
            {
                m_bytes[i] = name[i];
            }
            m_bytes[len] = '\0';
            m_size       = len;
        }

        /// @brief Returns whether the name is empty.
        [[nodiscard]] constexpr bool Empty() const noexcept
        {
            return m_size == 0;
        }

        /// @brief Returns the retained byte length.
        [[nodiscard]] constexpr std::size_t Size() const noexcept
        {
            return m_size;
        }

        /// @brief Returns a view of the retained bytes.
        [[nodiscard]] constexpr std::string_view View() const noexcept
        {
            return std::string_view(m_bytes.data(), m_size);
        }

        /// @brief Returns the null-terminated name.
        [[nodiscard]] constexpr const char* CStr() const noexcept
        {
            return m_bytes.data();
        }

    private:
        std::array<char, MaxBytes + 1> m_bytes {};
        std::size_t                    m_size {0};
    };
}// namespace NGIN::Execution
