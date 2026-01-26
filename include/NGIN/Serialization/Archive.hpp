#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Serialization
{
    /// @brief Serialization direction for archives.
    enum class ArchiveMode : UInt8
    {
        Read,
        Write,
    };

    /// @brief Minimal base class for serialization archives.
    class NGIN_BASE_API Archive
    {
    public:
        virtual ~Archive() = default;
        [[nodiscard]] ArchiveMode Mode() const noexcept { return m_mode; }

    protected:
        explicit Archive(ArchiveMode mode) noexcept
            : m_mode(mode)
        {
        }

    private:
        ArchiveMode m_mode;
    };
}// namespace NGIN::Serialization
