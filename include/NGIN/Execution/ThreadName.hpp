/// @file ThreadName.hpp
/// @brief Fixed-size thread name helper (owned, truncating).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace NGIN::Execution
{
    class ThreadName final
    {
    public:
        static constexpr std::size_t MaxBytes = 63;

        constexpr ThreadName() noexcept = default;

        constexpr explicit ThreadName(std::string_view name) noexcept
        {
            Assign(name);
        }

        constexpr void Assign(std::string_view name) noexcept
        {
            const auto len = std::min<std::size_t>(name.size(), MaxBytes);
            for (std::size_t i = 0; i < len; ++i)
            {
                m_bytes[i] = name[i];
            }
            m_bytes[len] = '\0';
            m_size       = len;
        }

        [[nodiscard]] constexpr bool Empty() const noexcept
        {
            return m_size == 0;
        }

        [[nodiscard]] constexpr std::size_t Size() const noexcept
        {
            return m_size;
        }

        [[nodiscard]] constexpr std::string_view View() const noexcept
        {
            return std::string_view(m_bytes.data(), m_size);
        }

        [[nodiscard]] constexpr const char* CStr() const noexcept
        {
            return m_bytes.data();
        }

    private:
        std::array<char, MaxBytes + 1> m_bytes {};
        std::size_t                    m_size {0};
    };
}// namespace NGIN::Execution

