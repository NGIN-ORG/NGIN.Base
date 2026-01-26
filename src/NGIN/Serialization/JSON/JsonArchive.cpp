#include <NGIN/Serialization/JSON/JsonArchive.hpp>

#include <cstring>

namespace NGIN::Serialization
{
    namespace
    {
        std::string_view CopyString(JsonDocument* document, std::string_view value) noexcept
        {
            if (!document || value.empty())
                return value;
            void* memory = document->Arena().Allocate(value.size(), alignof(char));
            if (!memory)
                return {};
            std::memcpy(memory, value.data(), value.size());
            return std::string_view(static_cast<const char*>(memory), value.size());
        }
    }// namespace

    JsonArchive::JsonArchive(JsonDocument& document) noexcept
        : Archive(ArchiveMode::Write), m_rootWrite(&document.Root()), m_document(&document)
    {
    }

    JsonArchive::JsonArchive(const JsonDocument& document) noexcept
        : Archive(ArchiveMode::Read), m_rootRead(&document.Root())
    {
    }

    JsonValue* JsonArchive::ResolveWriteTarget() noexcept
    {
        if (m_pendingWrite)
        {
            JsonValue* value = m_pendingWrite;
            m_pendingWrite   = nullptr;
            return value;
        }
        if (m_stack.Size() > 0)
            return m_stack[m_stack.Size() - 1].writeValue;
        return m_rootWrite;
    }

    const JsonValue* JsonArchive::ResolveReadTarget() noexcept
    {
        if (m_pendingRead)
        {
            const JsonValue* value = m_pendingRead;
            m_pendingRead          = nullptr;
            return value;
        }
        if (m_stack.Size() > 0)
            return m_stack[m_stack.Size() - 1].readValue;
        return m_rootRead;
    }

    JsonObject* JsonArchive::CreateObject() noexcept
    {
        if (!m_document)
            return nullptr;
        void* memory = m_document->Arena().Allocate(sizeof(JsonObject), alignof(JsonObject));
        if (!memory)
            return nullptr;
        return new (memory) JsonObject(m_document->Allocator());
    }

    JsonArray* JsonArchive::CreateArray() noexcept
    {
        if (!m_document)
            return nullptr;
        void* memory = m_document->Arena().Allocate(sizeof(JsonArray), alignof(JsonArray));
        if (!memory)
            return nullptr;
        return new (memory) JsonArray(m_document->Allocator());
    }

    bool JsonArchive::BeginObject() noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            const JsonValue* value = ResolveReadTarget();
            if (!value || !value->IsObject())
                return false;
            Frame frame;
            frame.kind      = Frame::Kind::Object;
            frame.readValue = value;
            frame.index     = 0;
            m_stack.PushBack(frame);
            return true;
        }

        JsonValue* value = ResolveWriteTarget();
        if (!value)
            return false;
        if (value->IsNull())
        {
            JsonObject* object = CreateObject();
            if (!object)
                return false;
            *value = JsonValue::MakeObject(object);
        }
        if (!value->IsObject())
            return false;

        Frame frame;
        frame.kind       = Frame::Kind::Object;
        frame.writeValue = value;
        frame.index      = 0;
        m_stack.PushBack(frame);
        return true;
    }

    bool JsonArchive::EndObject() noexcept
    {
        if (m_stack.Size() == 0)
            return false;
        const Frame& frame = m_stack[m_stack.Size() - 1];
        if (frame.kind != Frame::Kind::Object)
            return false;
        m_stack.PopBack();
        return true;
    }

    bool JsonArchive::BeginArray() noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            const JsonValue* value = ResolveReadTarget();
            if (!value || !value->IsArray())
                return false;
            Frame frame;
            frame.kind      = Frame::Kind::Array;
            frame.readValue = value;
            frame.index     = 0;
            m_stack.PushBack(frame);
            return true;
        }

        JsonValue* value = ResolveWriteTarget();
        if (!value)
            return false;
        if (value->IsNull())
        {
            JsonArray* array = CreateArray();
            if (!array)
                return false;
            *value = JsonValue::MakeArray(array);
        }
        if (!value->IsArray())
            return false;

        Frame frame;
        frame.kind       = Frame::Kind::Array;
        frame.writeValue = value;
        frame.index      = 0;
        m_stack.PushBack(frame);
        return true;
    }

    bool JsonArchive::EndArray() noexcept
    {
        if (m_stack.Size() == 0)
            return false;
        const Frame& frame = m_stack[m_stack.Size() - 1];
        if (frame.kind != Frame::Kind::Array)
            return false;
        m_stack.PopBack();
        return true;
    }

    bool JsonArchive::Key(std::string_view key) noexcept
    {
        if (m_stack.Size() == 0)
            return false;
        Frame& frame = m_stack[m_stack.Size() - 1];
        if (frame.kind != Frame::Kind::Object)
            return false;

        if (Mode() == ArchiveMode::Read)
        {
            const JsonObject& object = frame.readValue->AsObject();
            const JsonValue*  value  = object.Find(key);
            if (!value)
                return false;
            m_pendingRead = value;
            return true;
        }

        JsonObject& object = frame.writeValue->AsObject();
        JsonValue*  value  = object.Find(key);
        if (!value)
        {
            const std::string_view storedKey = CopyString(m_document, key);
            if (!storedKey.data() && !key.empty())
                return false;
            object.members.PushBack(JsonMember {storedKey, JsonValue::MakeNull()});
            value = &object.members[object.members.Size() - 1].value;
        }
        m_pendingWrite = value;
        return true;
    }

    bool JsonArchive::NextElement() noexcept
    {
        if (m_stack.Size() == 0)
            return false;
        Frame& frame = m_stack[m_stack.Size() - 1];
        if (frame.kind != Frame::Kind::Array)
            return false;

        if (Mode() == ArchiveMode::Read)
        {
            const JsonArray& array = frame.readValue->AsArray();
            if (frame.index >= array.values.Size())
                return false;
            m_pendingRead = &array.values[frame.index++];
            return true;
        }

        JsonArray& array = frame.writeValue->AsArray();
        array.values.PushBack(JsonValue::MakeNull());
        m_pendingWrite = &array.values[array.values.Size() - 1];
        return true;
    }

    bool JsonArchive::Null() noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            const JsonValue* value = ResolveReadTarget();
            return value && value->IsNull();
        }

        JsonValue* value = ResolveWriteTarget();
        if (!value)
            return false;
        *value = JsonValue::MakeNull();
        return true;
    }

    bool JsonArchive::Value(bool& value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            const JsonValue* target = ResolveReadTarget();
            if (!target || !target->IsBool())
                return false;
            value = target->AsBool();
            return true;
        }

        JsonValue* target = ResolveWriteTarget();
        if (!target)
            return false;
        *target = JsonValue::MakeBool(value);
        return true;
    }

    bool JsonArchive::Value(F64& value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            const JsonValue* target = ResolveReadTarget();
            if (!target || !target->IsNumber())
                return false;
            value = target->AsNumber();
            return true;
        }

        JsonValue* target = ResolveWriteTarget();
        if (!target)
            return false;
        *target = JsonValue::MakeNumber(value);
        return true;
    }

    bool JsonArchive::Value(std::string_view& value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
        {
            const JsonValue* target = ResolveReadTarget();
            if (!target || !target->IsString())
                return false;
            value = target->AsString();
            return true;
        }

        JsonValue* target = ResolveWriteTarget();
        if (!target)
            return false;
        const std::string_view stored = CopyString(m_document, value);
        if (!stored.data() && !value.empty())
            return false;
        *target = JsonValue::MakeString(stored);
        return true;
    }

    bool JsonArchive::Value(std::string_view value) noexcept
    {
        if (Mode() == ArchiveMode::Read)
            return false;
        JsonValue* target = ResolveWriteTarget();
        if (!target)
            return false;
        const std::string_view stored = CopyString(m_document, value);
        if (!stored.data() && !value.empty())
            return false;
        *target = JsonValue::MakeString(stored);
        return true;
    }

}// namespace NGIN::Serialization
