#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Primitives.hpp>

#include <limits>
#include <span>
#include <string_view>

namespace NGIN::Serialization
{
    /// @brief Reusable parser scratch storage. Capacity is retained by Reset().
    class ParseScratch
    {
    public:
        ParseScratch() = default;

        void Reset() noexcept
        {
            m_bytes.Clear();
        }

        void Reserve(UIntSize bytes)
        {
            m_bytes.Reserve(bytes);
        }

        void Push(char value)
        {
            m_bytes.PushBack(value);
        }

        void Append(std::string_view value)
        {
            if (value.empty())
                return;
            m_bytes.Reserve(m_bytes.Size() + value.size());
            for (const char c: value)
                m_bytes.PushBack(c);
        }

        /// @brief Appends byte storage and returns its stable start.
        ///
        /// Reserve the complete record size before repeated allocation when
        /// returned pointers must remain stable.
        [[nodiscard]] char* TryAllocate(UIntSize bytes) noexcept
        {
            if (bytes == 0)
                return nullptr;
            try
            {
                const UIntSize begin = m_bytes.Size();
                if (begin > (std::numeric_limits<UIntSize>::max)() - bytes)
                    return nullptr;
                m_bytes.Reserve(begin + bytes);
                for (UIntSize index = 0; index < bytes; ++index)
                    m_bytes.PushBack('\0');
                return m_bytes.data() + begin;
            } catch (...)
            {
                return nullptr;
            }
        }

        [[nodiscard]] std::span<char> MutableSpan() noexcept
        {
            return {m_bytes.data(), m_bytes.Size()};
        }

        [[nodiscard]] std::string_view View() const noexcept
        {
            return {m_bytes.data(), m_bytes.Size()};
        }

        [[nodiscard]] UIntSize Size() const noexcept { return m_bytes.Size(); }
        [[nodiscard]] UIntSize Capacity() const noexcept { return m_bytes.Capacity(); }

    private:
        NGIN::Containers::Vector<char> m_bytes {};
    };
}// namespace NGIN::Serialization
