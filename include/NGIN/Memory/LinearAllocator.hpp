/// @file LinearAllocator.hpp
/// @brief Linear (bump-pointer) allocator with an owning upstream buffer and optional rollback markers.
///
/// This allocator obtains one large contiguous block from an upstream allocator at construction time,
/// then serves sub-allocations linearly by advancing a bump pointer. Individual Deallocate calls are
/// no-ops; memory can be reclaimed wholesale via Reset() or Rollback(marker).
///
/// ### Design goals
/// - **Owning slab**: acquires a single slab from `Upstream` and releases it in the destructor.
/// - **Fast hot-path**: O(1) Allocate using `std::align`, no per-allocation headers.
/// - **Deterministic**: not thread-safe by design; intended for thread-confined usage.
/// - **Customizable base alignment**: caller may request a base alignment for the slab (defaults to `alignof(std::max_align_t)`).
/// - **Extended allocation support**: implements `AllocateEx` returning `NGIN::Memory::MemoryBlock`
///   for tools/telemetry and composite allocators.
///
/// ### Typical usage
/// @code
/// using namespace NGIN::Memory;
///
/// // Stateless upstream (e.g., SystemAllocator) by value
/// LinearAllocator<> frameArena(1u << 20); // 1 MiB
///
/// void* p1 = frameArena.Allocate(1024, 16);
/// void* p2 = frameArena.Allocate(2048, 32);
/// // ...
/// frameArena.Reset(); // reclaim all
/// @endcode
///
/// To share a single global heap across many arenas, pass a lightweight handle or a reference-wrapper
/// type as the `Upstream` template parameter (so the arenas do not "own" the global heap instance).
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <new>
#include <algorithm>
#include <memory>// std::align

#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

namespace NGIN::Memory
{
    /// @class LinearAllocator
    /// @brief Simple linear (bump-pointer) allocator with an owning upstream slab.
    ///
    /// @tparam Upstream  Upstream allocator type satisfying `AllocatorConcept`. Default is `SystemAllocator`.
    ///
    /// The allocator requests a single buffer of `capacity` bytes from the upstream in the constructor,
    /// aligned to `baseAlignmentInBytes`. Subsequent allocations linearly carve memory from this buffer.
    /// Individual deallocations are ignored; `Reset()` resets the bump pointer to the start, and
    /// `Rollback(marker)` rolls the bump pointer back to a saved marker returned by `Mark()`.
    ///
    /// This allocator is **not thread-safe** and should be used by a single thread at a time.
    template<class Upstream = SystemAllocator>
    class LinearAllocator
    {
    public:
        /// @brief Convenient alias for the upstream allocator type.
        using UpstreamAllocator = Upstream;

        /// @brief Deleted default constructor. A capacity must be provided.
        LinearAllocator() = delete;

        /// @brief Construct an owning allocator by acquiring a slab from the upstream allocator.
        ///
        /// @param capacityInBytes     Number of bytes to request from the upstream allocator.
        /// @param upstream            Upstream allocator instance. For stateless allocators this is typically a cheap value type.
        ///                            For shared/global allocators, consider passing a lightweight handle/reference wrapper.
        /// @param baseAlignmentInBytes Alignment in bytes for the slab allocated from upstream. Defaults to `alignof(std::max_align_t)`.
        ///
        /// The constructor requests a single block of `capacityInBytes` aligned to `baseAlignmentInBytes`.
        /// If the upstream allocation fails, the allocator becomes empty (capacity zero) and all allocation
        /// requests will return `nullptr`.
        explicit LinearAllocator(std::size_t capacityInBytes,
                                 Upstream    upstream             = {},
                                 std::size_t baseAlignmentInBytes = std::max(std::size_t(alignof(std::max_align_t)), std::size_t(64)))
            : m_upstreamInstance(std::move(upstream)), m_baseAlignmentInBytes(baseAlignmentInBytes)
        {
            void* base        = m_upstreamInstance.Allocate(capacityInBytes, m_baseAlignmentInBytes);
            m_basePointer     = static_cast<std::byte*>(base);
            m_currentPointer  = m_basePointer;
            m_capacityInBytes = base ? capacityInBytes : 0;// guard on failure
        }

        /// @brief Deleted copy constructor.
        LinearAllocator(const LinearAllocator&) = delete;
        /// @brief Deleted copy assignment.
        LinearAllocator& operator=(const LinearAllocator&) = delete;

        /// @brief Move constructor. Transfers slab ownership and internal state.
        LinearAllocator(LinearAllocator&& other) noexcept
            : m_upstreamInstance(std::move(other.m_upstreamInstance)), m_baseAlignmentInBytes(other.m_baseAlignmentInBytes), m_basePointer(other.m_basePointer), m_currentPointer(other.m_currentPointer), m_capacityInBytes(other.m_capacityInBytes)
        {
            other.m_basePointer = other.m_currentPointer = nullptr;
            other.m_capacityInBytes                      = 0;
            other.m_baseAlignmentInBytes                 = alignof(std::max_align_t);
        }

        /// @brief Move assignment. Releases current slab (if any) and takes ownership of @p other 's slab.
        LinearAllocator& operator=(LinearAllocator&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_upstreamInstance     = std::move(other.m_upstreamInstance);
                m_baseAlignmentInBytes = other.m_baseAlignmentInBytes;
                m_basePointer          = other.m_basePointer;
                m_currentPointer       = other.m_currentPointer;
                m_capacityInBytes      = other.m_capacityInBytes;

                other.m_basePointer = other.m_currentPointer = nullptr;
                other.m_capacityInBytes                      = 0;
                other.m_baseAlignmentInBytes                 = alignof(std::max_align_t);
            }
            return *this;
        }

        /// @brief Destructor. Returns the slab to the upstream allocator if allocated.
        ~LinearAllocator() { Release(); }

        /// @brief Return whether a value is a power-of-two (helper).
        /// @param value Any non-negative size.
        /// @return True if @p value is a power-of-two.
        [[nodiscard]] static constexpr bool IsPowerOfTwo(std::size_t value) noexcept
        {
            return value && ((value & (value - 1)) == 0);
        }

        /// @brief Normalize an alignment to a power-of-two and at least `alignof(std::max_align_t)`.
        /// @param alignmentInBytes Requested alignment (may be zero or non power-of-two).
        /// @return A valid power-of-two alignment, clamped to at least `alignof(std::max_align_t)`.
        [[nodiscard]] static constexpr std::size_t NormalizeAlignment(std::size_t alignmentInBytes) noexcept
        {
            if (alignmentInBytes == 0)
                alignmentInBytes = 1;
            if (!IsPowerOfTwo(alignmentInBytes))
            {
                // Round up to next power-of-two
                std::size_t a = alignmentInBytes - 1;
                a |= a >> 1;
                a |= a >> 2;
                a |= a >> 4;
                a |= a >> 8;
                a |= a >> 16;
#if INTPTR_MAX == INT64_MAX
                a |= a >> 32;
#endif
                alignmentInBytes = a + 1;
            }
            if (alignmentInBytes < alignof(std::max_align_t))
                alignmentInBytes = alignof(std::max_align_t);
            return alignmentInBytes;
        }

        /// @brief Allocate a block of memory from the linear arena.
        ///
        /// Uses `std::align` to find an aligned region within the remaining space, then advances
        /// the bump pointer. This is O(1) and does not store per-allocation headers.
        ///
        /// @param sizeInBytes      Number of bytes to allocate.
        /// @param alignmentInBytes Alignment in bytes (may be zero or non power-of-two; it will be normalized).
        /// @return Pointer to the aligned block on success; `nullptr` if there is insufficient space or the allocator is empty.
        [[nodiscard]] void* Allocate(std::size_t sizeInBytes, std::size_t alignmentInBytes) noexcept
        {
            if (sizeInBytes == 0 || !m_basePointer)
                return nullptr;

            const std::size_t normalizedAlignment = NormalizeAlignment(alignmentInBytes);

            // Align within remaining space using std::align to avoid overflow-prone arithmetic
            std::size_t space = m_capacityInBytes - Used();
            void*       ptr   = m_currentPointer;
            if (std::align(normalizedAlignment, sizeInBytes, ptr, space) == nullptr)
                return nullptr;

            m_currentPointer = static_cast<std::byte*>(ptr) + sizeInBytes;
            return ptr;
        }

        /// @brief Extended allocation returning rich metadata.
        ///
        /// This satisfies the "extended allocator" capability probed by `AllocatorTraits`.
        /// The returned `MemoryBlock` reports the granted size (equal to the requested size for a linear allocator)
        /// and the alignment that was enforced (after normalization).
        ///
        /// @param sizeInBytes      Number of bytes to allocate.
        /// @param alignmentInBytes Alignment in bytes (may be zero or non power-of-two; it will be normalized).
        /// @return `MemoryBlock` with pointer and metadata; if allocation fails the block contains a null pointer.
        [[nodiscard]] MemoryBlock AllocateEx(std::size_t sizeInBytes, std::size_t alignmentInBytes) noexcept
        {
            const std::size_t normalizedAlignment = NormalizeAlignment(alignmentInBytes);
            void*             p                   = Allocate(sizeInBytes, normalizedAlignment);
            if (!p)
                return MemoryBlock {};
            return MemoryBlock {p, sizeInBytes, normalizedAlignment, 0};
        }

        /// @brief Deallocate is a no-op for a linear allocator.
        /// @param pointer           Ignored.
        /// @param sizeInBytes       Ignored.
        /// @param alignmentInBytes  Ignored.
        void Deallocate(void*, std::size_t, std::size_t) noexcept
        {
            // Intentionally empty: individual frees are not supported.
        }

        /// @brief Return the total capacity (bytes) of the slab.
        [[nodiscard]] std::size_t MaxSize() const noexcept { return m_capacityInBytes; }

        /// @brief Return the number of bytes remaining (free) in the slab.
        [[nodiscard]] std::size_t Remaining() const noexcept { return m_capacityInBytes - Used(); }

        /// @brief Return the number of bytes used so far in the slab.
        [[nodiscard]] std::size_t Used() const noexcept
        {
            return static_cast<std::size_t>(m_currentPointer - m_basePointer);
        }

        /// @brief Conservative ownership test: returns true if @p pointer lies within the slab range.
        /// @param pointer Pointer to test.
        /// @return True if @p pointer is within [base, base + capacity); false otherwise.
        [[nodiscard]] bool Owns(const void* pointer) const noexcept
        {
            const auto addr = reinterpret_cast<const std::byte*>(pointer);
            return addr >= m_basePointer && addr < m_basePointer + m_capacityInBytes;
        }

        /// @brief Reset the bump pointer to the beginning of the slab (reclaim all allocations).
        void Reset() noexcept { m_currentPointer = m_basePointer; }

        /// @brief Marker capturing the current bump pointer for later rollback.
        /// @return An `ArenaMarker` referring to the current bump pointer.
        [[nodiscard]] ArenaMarker Mark() const noexcept { return {m_currentPointer}; }

        /// @brief Roll back the bump pointer to a previously acquired marker.
        /// @param marker Marker returned by `Mark()`. If it does not refer into the slab, the call is ignored.
        void Rollback(ArenaMarker marker) noexcept
        {
            if (marker.ptr >= m_basePointer && marker.ptr <= m_basePointer + m_capacityInBytes)
                m_currentPointer = static_cast<std::byte*>(marker.ptr);
        }

    private:
        /// @brief Release the slab back to the upstream allocator, if present.
        void Release() noexcept
        {
            if (m_basePointer)
            {
                m_upstreamInstance.Deallocate(m_basePointer, m_capacityInBytes, m_baseAlignmentInBytes);
            }
            m_basePointer = m_currentPointer = nullptr;
            m_capacityInBytes                = 0;
        }

        // Upstream allocator instance (by value). For shared/global allocators, pass a handle/ref-wrapper here.
        [[no_unique_address]] Upstream m_upstreamInstance {};

        // Slab properties
        std::size_t m_baseAlignmentInBytes {alignof(std::max_align_t)};
        std::byte*  m_basePointer {nullptr};
        std::byte*  m_currentPointer {nullptr};
        std::size_t m_capacityInBytes {0};
    };

    // Ensure the allocator satisfies the core concept
    static_assert(AllocatorConcept<LinearAllocator<SystemAllocator>>,
                  "LinearAllocator must satisfy AllocatorConcept (Allocate/Deallocate).");

}// namespace NGIN::Memory
