#include <NGIN/Serialization/XML/XmlArchive.hpp>

#include <cstring>

namespace NGIN::Serialization
{
    XmlArchive::XmlArchive(XmlDocument& document) noexcept
        : Archive(ArchiveMode::Write), m_rootWrite(document.Root()), m_document(&document)
    {
    }

    XmlArchive::XmlArchive(const XmlDocument& document) noexcept
        : Archive(ArchiveMode::Read), m_rootRead(document.Root())
    {
    }

    std::string_view XmlArchive::CopyString(std::string_view value) noexcept
    {
        if (!m_document || value.empty())
            return value;
        void* memory = m_document->Arena().Allocate(value.size(), alignof(char));
        if (!memory)
            return {};
        std::memcpy(memory, value.data(), value.size());
        return std::string_view(static_cast<const char*>(memory), value.size());
    }

    XmlElement* XmlArchive::CreateElement(std::string_view name) noexcept
    {
        if (!m_document)
            return nullptr;
        void* memory = m_document->Arena().Allocate(sizeof(XmlElement), alignof(XmlElement));
        if (!memory)
            return nullptr;
        auto* element = new (memory) XmlElement(m_document->Allocator());
        element->name = CopyString(name);
        if (!element->name.data() && !name.empty())
            return nullptr;
        return element;
    }

    bool XmlArchive::BeginElement(std::string_view name) noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            if (m_stack.Size() == 0)
            {
                if (!m_rootRead || m_rootRead->name != name)
                    return false;
                Frame rootFrame;
                rootFrame.readElement = m_rootRead;
                m_stack.PushBack(rootFrame);
                return true;
            }

            Frame&            frame  = m_stack[m_stack.Size() - 1];
            const XmlElement* parent = frame.readElement;
            if (!parent)
                return false;

            const auto& children = parent->children;
            for (UIntSize i = frame.childIndex; i < children.Size(); ++i)
            {
                if (children[i].type != XmlNode::Type::Element)
                    continue;
                if (children[i].element->name != name)
                    continue;
                frame.childIndex = i + 1;
                Frame childFrame;
                childFrame.readElement = children[i].element;
                m_stack.PushBack(childFrame);
                return true;
            }
            return false;
        }

        if (m_stack.Size() == 0 && m_rootWrite && m_rootWrite->name == name)
        {
            Frame frame;
            frame.writeElement = m_rootWrite;
            m_stack.PushBack(frame);
            return true;
        }

        XmlElement* parent = nullptr;
        if (m_stack.Size() == 0)
            parent = m_rootWrite;
        else
            parent = m_stack[m_stack.Size() - 1].writeElement;

        XmlElement* element = CreateElement(name);
        if (!element)
            return false;

        if (!parent)
        {
            m_rootWrite = element;
            if (m_document)
                m_document->SetRoot(element);
        }
        else
        {
            parent->children.PushBack(XmlNode::MakeElement(element));
        }

        Frame frame;
        frame.writeElement = element;
        m_stack.PushBack(frame);
        return true;
    }

    bool XmlArchive::EndElement() noexcept
    {
        if (m_stack.Size() == 0)
            return false;
        m_stack.PopBack();
        return true;
    }

    bool XmlArchive::Attribute(std::string_view name, std::string_view& value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            if (m_stack.Size() == 0)
                return false;
            const XmlElement* element = m_stack[m_stack.Size() - 1].readElement;
            if (!element)
                return false;
            const XmlAttribute* attr = element->FindAttribute(name);
            if (!attr)
                return false;
            value = attr->value;
            return true;
        }

        if (m_stack.Size() == 0)
            return false;
        XmlElement* element = m_stack[m_stack.Size() - 1].writeElement;
        if (!element)
            return false;
        const std::string_view storedName  = CopyString(name);
        const std::string_view storedValue = CopyString(value);
        if ((!storedName.data() && !name.empty()) || (!storedValue.data() && !value.empty()))
            return false;
        element->attributes.PushBack(XmlAttribute {storedName, storedValue});
        return true;
    }

    bool XmlArchive::Attribute(std::string_view name, std::string_view value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
            return false;
        if (m_stack.Size() == 0)
            return false;
        XmlElement* element = m_stack[m_stack.Size() - 1].writeElement;
        if (!element)
            return false;
        const std::string_view storedName  = CopyString(name);
        const std::string_view storedValue = CopyString(value);
        if ((!storedName.data() && !name.empty()) || (!storedValue.data() && !value.empty()))
            return false;
        element->attributes.PushBack(XmlAttribute {storedName, storedValue});
        return true;
    }

    bool XmlArchive::Text(std::string_view& value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            if (m_stack.Size() == 0)
                return false;
            const XmlElement* element = m_stack[m_stack.Size() - 1].readElement;
            if (!element)
                return false;
            for (UIntSize i = 0; i < element->children.Size(); ++i)
            {
                if (element->children[i].type == XmlNode::Type::Text)
                {
                    value = element->children[i].text;
                    return true;
                }
            }
            return false;
        }

        if (m_stack.Size() == 0)
            return false;
        XmlElement* element = m_stack[m_stack.Size() - 1].writeElement;
        if (!element)
            return false;
        const std::string_view storedValue = CopyString(value);
        if (!storedValue.data() && !value.empty())
            return false;
        element->children.PushBack(XmlNode::MakeText(storedValue));
        return true;
    }

    bool XmlArchive::Text(std::string_view value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
            return false;
        if (m_stack.Size() == 0)
            return false;
        XmlElement* element = m_stack[m_stack.Size() - 1].writeElement;
        if (!element)
            return false;
        const std::string_view storedValue = CopyString(value);
        if (!storedValue.data() && !value.empty())
            return false;
        element->children.PushBack(XmlNode::MakeText(storedValue));
        return true;
    }

}// namespace NGIN::Serialization
