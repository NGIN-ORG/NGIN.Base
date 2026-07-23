#pragma once

#include <NGIN/Memory/PolyAllocator.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Serialization
{
    /// @brief Optional allocation resources shared by parsers and builders.
    ///
    /// The referenced allocator must outlive the returned document. An empty
    /// allocator selects the normal system-backed path.
    struct ParseResources
    {
        NGIN::Memory::PolyAllocatorRef allocator {};
        UIntSize                      initialArenaBlockBytes {4096};
    };
}// namespace NGIN::Serialization
