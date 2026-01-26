#pragma once

#include <NGIN/Serialization/Archive.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include <string_view>

namespace NGIN::Serialization
{
    /// @brief XML archive for DOM-backed serialization.
    class NGIN_BASE_API XmlArchive final : public Archive
    {
    public:
        explicit XmlArchive(XmlDocument& document) noexcept;
        explicit XmlArchive(const XmlDocument& document) noexcept;

        bool BeginElement(std::string_view name) noexcept;
        bool EndElement() noexcept;

        bool Attribute(std::string_view name, std::string_view& value) noexcept;
        bool Attribute(std::string_view name, std::string_view value) noexcept;

        bool Text(std::string_view& value) noexcept;
        bool Text(std::string_view value) noexcept;

    private:
        struct Frame
        {
            const XmlElement* readElement {nullptr};
            XmlElement*       writeElement {nullptr};
            UIntSize          childIndex {0};
        };

        XmlElement*      CreateElement(std::string_view name) noexcept;
        std::string_view CopyString(std::string_view value) noexcept;

        const XmlElement* m_rootRead {nullptr};
        XmlElement*       m_rootWrite {nullptr};
        XmlDocument*      m_document {nullptr};

        NGIN::Containers::Vector<Frame> m_stack;
    };
}// namespace NGIN::Serialization
