#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Primitives.hpp>

#include <string_view>

namespace NGIN::Serialization
{
    struct XmlElement;

    using XmlArena     = NGIN::Memory::LinearAllocator<>;
    using XmlAllocator = NGIN::Memory::AllocatorRef<XmlArena>;

    /// @brief XML attribute name/value pair.
    struct XmlAttribute
    {
        std::string_view name {};
        std::string_view value {};
    };

    /// @brief XML node (element or text/CDATA).
    struct XmlNode
    {
        enum class Type : UInt8
        {
            Element,
            Text,
            CData,
        };

        static XmlNode MakeElement(XmlElement* element) noexcept
        {
            XmlNode node;
            node.type    = Type::Element;
            node.element = element;
            node.text    = {};
            return node;
        }

        static XmlNode MakeText(std::string_view text) noexcept
        {
            XmlNode node;
            node.type    = Type::Text;
            node.element = nullptr;
            node.text    = text;
            return node;
        }

        static XmlNode MakeCData(std::string_view text) noexcept
        {
            XmlNode node;
            node.type    = Type::CData;
            node.element = nullptr;
            node.text    = text;
            return node;
        }

        Type             type {Type::Text};
        XmlElement*      element {nullptr};
        std::string_view text {};
    };

    /// @brief XML element with attributes and child nodes.
    struct XmlElement
    {
        explicit XmlElement(XmlAllocator allocator)
            : attributes(0, allocator), children(0, allocator)
        {
        }

        std::string_view                                     name {};
        NGIN::Containers::Vector<XmlAttribute, XmlAllocator> attributes;
        NGIN::Containers::Vector<XmlNode, XmlAllocator>      children;

        [[nodiscard]] const XmlAttribute* FindAttribute(std::string_view key) const noexcept;
    };

    /// @brief XML document owning an arena for parsed nodes.
    class NGIN_BASE_API XmlDocument
    {
    public:
        explicit XmlDocument(UIntSize arenaBytes);

        XmlDocument(XmlDocument&&) noexcept            = default;
        XmlDocument& operator=(XmlDocument&&) noexcept = default;
        XmlDocument(const XmlDocument&)                = delete;
        XmlDocument& operator=(const XmlDocument&)     = delete;

        [[nodiscard]] XmlElement*       Root() noexcept { return m_root; }
        [[nodiscard]] const XmlElement* Root() const noexcept { return m_root; }

        [[nodiscard]] XmlAllocator Allocator() noexcept { return XmlAllocator {m_arena}; }
        [[nodiscard]] XmlArena&    Arena() noexcept { return m_arena; }

        void SetRoot(XmlElement* root) noexcept { m_root = root; }
        void AdoptInput(NGIN::Containers::Vector<NGIN::Byte>&& input) noexcept { m_inputStorage = std::move(input); }

    private:
        XmlArena                             m_arena;
        XmlElement*                          m_root {nullptr};
        NGIN::Containers::Vector<NGIN::Byte> m_inputStorage {};
    };
}// namespace NGIN::Serialization
