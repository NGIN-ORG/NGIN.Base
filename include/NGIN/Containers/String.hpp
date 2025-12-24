#pragma once
//
// NGIN::Containers::BasicString — refactored with robust SBO-bytes math,
// allocator concept integration, overlap-safe appends, and a clean memory model.
//
// - SBO sized in BYTES (works for CharT of any width)
// - Single last byte in SBO stores the small size (in CHARS); null terminator is stored in-band
// - No endian tricks; explicit discriminant
// - Growth policy pluggable
// - Uses NGIN::Memory::AllocatorConcept (e.g., SystemAllocator)
//

#include <NGIN/Defines.hpp>   // UIntSize, UInt8, etc.
#include <NGIN/Primitives.hpp>// integral aliases
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace NGIN::Containers
{
    //--------------------------------------------------------------------------
    // Growth policy
    //--------------------------------------------------------------------------

    struct DefaultGrowthPolicy
    {
        static constexpr UIntSize smallCapThreshold = 64;

        static constexpr UIntSize NextPow2(UIntSize v) noexcept
        {
            if (v <= 1)
                return 1;
            v -= 1;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
#if INTPTR_MAX == INT64_MAX
            v |= v >> 32;
#endif
            return v + 1;
        }

        static constexpr UIntSize Grow(UIntSize oldCap, UIntSize required) noexcept
        {
            UIntSize newCap;
            if (oldCap < smallCapThreshold)
            {
                newCap = std::max(NextPow2(required), required);
            }
            else
            {
                // 1.5x growth
                UIntSize g = oldCap + oldCap / 2;
                if (g < required)
                    g = required;
                newCap = g;
            }
            return newCap;
        }
    };

    //--------------------------------------------------------------------------
    // BasicString
    //--------------------------------------------------------------------------

    template<typename CharT,
             UIntSize SBOBytes,
             class Alloc  = NGIN::Memory::SystemAllocator,
             class Growth = DefaultGrowthPolicy>
    class BasicString
    {
        static_assert(std::is_trivial_v<CharT>, "BasicString requires a trivial character type.");
        static_assert(SBOBytes >= sizeof(CharT) + 1,
                      "SBOBytes must be large enough to store at least a CharT terminator and the size byte.");

        // Compute usable small-buffer capacity in CHARS:
        //  - reserve 1 byte (last) to store the small size (in chars)
        //  - remaining bytes must fit (size chars + 1 terminator char)
        static constexpr UIntSize sbo_bytes = SBOBytes;
        static constexpr UIntSize sbo_chars =
                ((SBOBytes > 1) && ((SBOBytes - 1) >= sizeof(CharT)))
                        ? (((SBOBytes - 1) / sizeof(CharT)) - 1)
                        : 0;

        // We store the small size in one byte (0..255). Ensure capacity fits.
        static_assert(sbo_chars <= 255,
                      "SBO char capacity exceeds what fits in a single size byte. "
                      "Reduce SBOBytes or switch to a different small-size encoding.");

        // Storage union: either heap (ptr, size, cap) or small (bytes)
        union Storage
        {
            struct Heap
            {
                CharT*   ptr;
                UIntSize size;// chars (excluding terminator)
                UIntSize cap; // chars (excluding terminator)
            };

            struct Small
            {
                // Align SBO bytes to CharT to allow CharT* aliasing into the buffer
                alignas(CharT) std::byte bytes[SBOBytes];
            };

            Heap  heap;
            Small small;

            Storage() noexcept {}// trivial
        };

    public:
        using ThisType    = BasicString<CharT, SBOBytes, Alloc, Growth>;
        using value_type  = CharT;
        using size_type   = UIntSize;
        using traits_type = std::char_traits<CharT>;
        using view_type   = std::basic_string_view<CharT>;

        //--------------------------------------------------------------------------
        // CTORS / DTOR
        //--------------------------------------------------------------------------

        BasicString() noexcept(std::is_nothrow_default_constructible_v<Alloc>)
            : m_allocator()
        {
            SetSmall();
            SetSmallSize(0);
            if constexpr (sbo_chars > 0)
            {
                SmallData()[0] = CharT(0);
            }
        }

        explicit BasicString(const Alloc& alloc) noexcept
            : m_allocator(alloc)
        {
            SetSmall();
            SetSmallSize(0);
            if constexpr (sbo_chars > 0)
            {
                SmallData()[0] = CharT(0);
            }
        }

        BasicString(const CharT* cstr, const Alloc& alloc = Alloc())
            : m_allocator(alloc)
        {
            if (!cstr)
            {
                SetSmall();
                SetSmallSize(0);
                if constexpr (sbo_chars > 0)
                    SmallData()[0] = CharT(0);
                return;
            }
            const size_type len = traits_type::length(cstr);
            InitFromView(view_type {cstr, len});
        }

        BasicString(view_type sv, const Alloc& alloc = Alloc())
            : m_allocator(alloc)
        {
            InitFromView(sv);
        }

        BasicString(size_type count, CharT ch, const Alloc& alloc = Alloc())
            : m_allocator(alloc)
        {
            if (count <= sbo_chars)
            {
                SetSmall();
                CharT* d = SmallData();
                for (size_type i = 0; i < count; ++i)
                    d[i] = ch;
                d[count] = CharT(0);
                SetSmallSize(static_cast<UInt8>(count));
            }
            else
            {
                const size_type cap = Growth::Grow(0, count);
                AllocateHeap(cap);
                for (size_type i = 0; i < count; ++i)
                    m_storage.heap.ptr[i] = ch;
                m_storage.heap.ptr[count] = CharT(0);
                m_storage.heap.size       = count;
            }
        }

        BasicString(const ThisType& other)
            : m_allocator(other.m_allocator)
        {
            if (other.m_isSmall)
            {
                SetSmall();
                // Copy SBO bytes (including size byte)
                std::memcpy(m_storage.small.bytes, other.m_storage.small.bytes, SBOBytes);
            }
            else
            {
                AllocateHeap(other.m_storage.heap.cap);
                std::memcpy(m_storage.heap.ptr, other.m_storage.heap.ptr,
                            (other.m_storage.heap.size + 1) * sizeof(CharT));
                m_storage.heap.size = other.m_storage.heap.size;
            }
        }

        BasicString(ThisType&& other) noexcept
            : m_allocator(std::move(other.m_allocator))
        {
            if (other.m_isSmall)
            {
                SetSmall();
                std::memcpy(m_storage.small.bytes, other.m_storage.small.bytes, SBOBytes);
            }
            else
            {
                m_isSmall           = false;
                m_storage.heap.ptr  = other.m_storage.heap.ptr;
                m_storage.heap.size = other.m_storage.heap.size;
                m_storage.heap.cap  = other.m_storage.heap.cap;

                other.SetSmall();
                other.SetSmallSize(0);
                if constexpr (sbo_chars > 0)
                    other.SmallData()[0] = CharT(0);
            }
        }

        ~BasicString()
        {
            if (!m_isSmall && m_storage.heap.ptr)
            {
                DeallocateHeap();
            }
        }

        //--------------------------------------------------------------------------
        // Assignment
        //--------------------------------------------------------------------------

        ThisType& operator=(const ThisType& other)
        {
            if (this == &other)
                return *this;

            if (!m_isSmall && m_storage.heap.ptr)
                DeallocateHeap();

            if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnCopyAssignment)
            {
                m_allocator = other.m_allocator;
            }

            if (other.m_isSmall)
            {
                SetSmall();
                std::memcpy(m_storage.small.bytes, other.m_storage.small.bytes, SBOBytes);
            }
            else
            {
                AllocateHeap(other.m_storage.heap.cap);
                std::memcpy(m_storage.heap.ptr, other.m_storage.heap.ptr,
                            (other.m_storage.heap.size + 1) * sizeof(CharT));
                m_storage.heap.size = other.m_storage.heap.size;
            }
            return *this;
        }

        ThisType& operator=(ThisType&& other) noexcept
        {
            if (this == &other)
                return *this;

            if (!m_isSmall && m_storage.heap.ptr)
                DeallocateHeap();

            if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment)
            {
                m_allocator = std::move(other.m_allocator);
            }

            if (other.m_isSmall)
            {
                SetSmall();
                std::memcpy(m_storage.small.bytes, other.m_storage.small.bytes, SBOBytes);
                other.SetSize(0);
                if constexpr (sbo_chars > 0)
                    other.SmallData()[0] = CharT(0);
            }
            else if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment ||
                               NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual)
            {
                m_isSmall           = false;
                m_storage.heap.ptr  = other.m_storage.heap.ptr;
                m_storage.heap.size = other.m_storage.heap.size;
                m_storage.heap.cap  = other.m_storage.heap.cap;

                other.SetSmall();
                other.SetSize(0);
                if constexpr (sbo_chars > 0)
                    other.SmallData()[0] = CharT(0);
            }
            else
            {
                InitFromView(view_type {other.Data(), other.Size()});
                other.SetSize(0);
            }
            return *this;
        }

        ThisType& operator=(view_type sv)
        {
            Assign(sv);
            return *this;
        }
        ThisType& operator=(const CharT* cstr) { return operator=(view_type {cstr ? cstr : empty_sv_()}); }

        //--------------------------------------------------------------------------
        // Observers
        //--------------------------------------------------------------------------

        size_type Size() const noexcept
        {
            return m_isSmall ? size_type(GetSmallSize()) : m_storage.heap.size;
        }

        bool Empty() const noexcept { return Size() == 0; }

        size_type Capacity() const noexcept
        {
            return m_isSmall ? sbo_chars : m_storage.heap.cap;
        }

        const CharT* c_str() const noexcept { return Data(); }
        const CharT* CStr() const noexcept { return c_str(); }// compatibility alias

        const CharT* Data() const noexcept
        {
            return m_isSmall ? SmallData() : m_storage.heap.ptr;
        }

        CharT* Data() noexcept
        {
            return m_isSmall ? SmallData() : m_storage.heap.ptr;
        }

        // legacy naming compatibility
        UIntSize GetSize() const noexcept { return Size(); }
        UIntSize GetCapacity() const noexcept { return Capacity(); }
        //CharT*   Data() noexcept { return Data(); }
        //  const CharT* CStr() noexcept = delete;// prevent accidental non-const; use const overload

        // Indexing
        const CharT& operator[](size_type i) const noexcept { return Data()[i]; }
        CharT&       operator[](size_type i) noexcept { return Data()[i]; }

        const CharT& at(size_type i) const
        {
            if (i >= Size())
                throw std::out_of_range("BasicString::at");
            return Data()[i];
        }
        CharT& At(size_type i)
        {
            if (i >= Size())
                throw std::out_of_range("BasicString::at");
            return Data()[i];
        }

        const CharT& front() const { return At(0); }
        CharT&       front() { return At(0); }

        const CharT& back() const { return At(Size() - 1); }
        CharT&       back() { return At(Size() - 1); }

        // Iteration
        const CharT* begin() const noexcept { return Data(); }
        const CharT* end() const noexcept { return Data() + Size(); }
        CharT*       begin() noexcept { return Data(); }
        CharT*       end() noexcept { return Data() + Size(); }

        // View conversion
        operator view_type() const noexcept { return view_type {Data(), Size()}; }

        //--------------------------------------------------------------------------
        // Capacity / memory
        //--------------------------------------------------------------------------

        void clear() noexcept
        {
            if (m_isSmall)
            {
                SetSmallSize(0);
                if constexpr (sbo_chars > 0)
                    SmallData()[0] = CharT(0);
            }
            else
            {
                m_storage.heap.size   = 0;
                m_storage.heap.ptr[0] = CharT(0);
            }
        }

        void Reserve(size_type newCap)
        {
            if (newCap <= Capacity())
                return;
            ReallocateTo(newCap);
        }

        // Reserve exactly (no growth policy involvement)
        void ReserveExact(size_type newCap)
        {
            if (newCap <= Capacity())
                return;
            reallocate_to_exact_(newCap);
        }

        void ShrinkToFit()
        {
            if (m_isSmall)
                return;
            const size_type currentSize     = m_storage.heap.size;
            const size_type currentCapacity = m_storage.heap.cap;
            if (currentSize <= sbo_chars)
            {
                // move back to small
                CharT* old = m_storage.heap.ptr;
                // Copy into SBO (including terminator)
                std::memcpy(SmallData(), old, (currentSize + 1) * sizeof(CharT));
                SetSmall();
                SetSmallSize(static_cast<UInt8>(currentSize));
                Deallocate(old, currentCapacity);
            }
            else if (currentSize < currentCapacity)
            {
                // fit tightly
                CharT* old = m_storage.heap.ptr;
                AllocateHeap(currentSize);
                std::memcpy(m_storage.heap.ptr, old, (currentSize + 1) * sizeof(CharT));
                m_storage.heap.size = currentSize;
                Deallocate(old, currentCapacity);
            }
        }

        //--------------------------------------------------------------------------
        // Modifiers
        //--------------------------------------------------------------------------

        void Resize(size_type n)
        {
            Resize(n, CharT());
        }

        void Resize(size_type n, CharT ch)
        {
            const size_type sz = Size();
            if (n <= sz)
            {
                SetSize(n);
                return;
            }

            // grow
            const size_type needed = n;
            if (needed > Capacity())
            {
                const size_type newCap = Growth::Grow(Capacity(), needed);
                ReallocateTo(newCap);
            }

            CharT* d = Data();
            for (size_type i = sz; i < n; ++i)
                d[i] = ch;
            SetSize(n);
        }

        void PushBack(CharT ch)
        {
            const size_type sz = Size();
            if (sz + 1 > Capacity())
            {
                const size_type newCap = Growth::Grow(Capacity(), sz + 1);
                ReallocateTo(newCap);
            }
            CharT* d = Data();
            d[sz]    = ch;
            SetSize(sz + 1);
        }

        void PopBack()
        {
            const size_type sz = Size();
            if (sz == 0)
                return;
            SetSize(sz - 1);
        }

        void Assign(view_type sv)
        {
            const size_type n = sv.size();
            if (n <= sbo_chars)
            {
                if (!m_isSmall)
                {
                    if (m_storage.heap.ptr)
                        DeallocateHeap();
                    SetSmall();
                }
                traits_type::copy(SmallData(), sv.data(), n);
                SmallData()[n] = CharT(0);
                SetSmallSize(static_cast<UInt8>(n));
            }
            else
            {
                if (m_isSmall)
                {
                    AllocateHeap(Growth::Grow(0, n));
                }
                else if (n > m_storage.heap.cap)
                {
                    ReallocateTo(Growth::Grow(m_storage.heap.cap, n));
                }
                traits_type::copy(m_storage.heap.ptr, sv.data(), n);
                m_storage.heap.ptr[n] = CharT(0);
                m_storage.heap.size   = n;
                m_isSmall             = false;
            }
        }

        // Append APIs
        void Append(const ThisType& other) { AppendView(view_type(other)); }
        void Append(view_type sv) { AppendView(sv); }
        void Append(const CharT* cstr) { AppendView(view_type {cstr ? cstr : empty_sv_()}); }
        void Append(CharT ch) { PushBack(ch); }

        ThisType& operator+=(const ThisType& other)
        {
            Append(other);
            return *this;
        }
        ThisType& operator+=(view_type sv)
        {
            Append(sv);
            return *this;
        }
        ThisType& operator+=(const CharT* cstr)
        {
            Append(cstr);
            return *this;
        }
        ThisType& operator+=(CharT ch)
        {
            Append(ch);
            return *this;
        }


        void Swap(ThisType& other) noexcept
        {
            using std::swap;
            if (this == &other)
                return;

            if (m_isSmall && other.m_isSmall)
            {
                std::byte tmp[SBOBytes];
                std::memcpy(tmp, m_storage.small.bytes, SBOBytes);
                std::memcpy(m_storage.small.bytes, other.m_storage.small.bytes, SBOBytes);
                std::memcpy(other.m_storage.small.bytes, tmp, SBOBytes);
            }
            else if (!m_isSmall && !other.m_isSmall)
            {
                swap(m_storage.heap.ptr, other.m_storage.heap.ptr);
                swap(m_storage.heap.size, other.m_storage.heap.size);
                swap(m_storage.heap.cap, other.m_storage.heap.cap);
            }
            else
            {
                // Small <-> Heap swap
                if (m_isSmall)
                {
                    // this small, other heap
                    Storage tmpSmall {};
                    std::memcpy(tmpSmall.small.bytes, m_storage.small.bytes, SBOBytes);

                    // move other's heap into this
                    m_storage.heap.ptr  = other.m_storage.heap.ptr;
                    m_storage.heap.size = other.m_storage.heap.size;
                    m_storage.heap.cap  = other.m_storage.heap.cap;

                    // move tmp small into other
                    std::memcpy(other.m_storage.small.bytes, tmpSmall.small.bytes, SBOBytes);

                    std::swap(m_isSmall, other.m_isSmall);
                }
                else
                {
                    other.Swap(*this);
                }
            }

            // swap allocators as well (engine allocators often store routing)
            std::swap(m_allocator, other.m_allocator);
        }

        //--------------------------------------------------------------------------
        // Search / compare (lightweight set)
        //--------------------------------------------------------------------------

        int Compare(view_type rhs) const noexcept
        {
            const view_type lhs {Data(), Size()};
            const int       cmp = traits_type::compare(lhs.data(),
                                                       rhs.data(),
                                                       std::min(lhs.size(), rhs.size()));
            if (cmp != 0)
                return cmp;
            if (lhs.size() < rhs.size())
                return -1;
            if (lhs.size() > rhs.size())
                return 1;
            return 0;
        }

        bool StartsWith(view_type p) const noexcept
        {
            const auto n = p.size();
            return Size() >= n && traits_type::compare(Data(), p.data(), n) == 0;
        }

        bool EndsWith(view_type s) const noexcept
        {
            const auto n = s.size();
            const auto m = Size();
            return m >= n && traits_type::compare(Data() + (m - n), s.data(), n) == 0;
        }

        bool Contains(view_type s) const noexcept
        {
            return Find(s) != view_type::npos;
        }

        size_type Find(view_type s, size_type pos = 0) const noexcept
        {
            const view_type hay {Data(), Size()};
            if (s.size() == 0)
                return std::min(pos, hay.size());
            if (s.size() > hay.size() || pos > hay.size() - s.size())
                return view_type::npos;

            const CharT*    h = hay.data();
            const CharT*    n = s.data();
            const size_type N = hay.size();
            const size_type M = s.size();

            for (size_type i = pos; i <= N - M; ++i)
            {
                if (traits_type::compare(h + i, n, M) == 0)
                    return i;
            }
            return view_type::npos;
        }

        // Equality
        friend bool operator==(const ThisType& a, view_type b) noexcept
        {
            return a.Size() == b.size() && traits_type::compare(a.Data(), b.data(), a.Size()) == 0;
        }
        friend bool operator==(view_type a, const ThisType& b) noexcept { return b == a; }
        friend bool operator==(const ThisType& a, const ThisType& b) noexcept { return a == view_type(b); }
        friend bool operator!=(const ThisType& a, const ThisType& b) noexcept { return !(a == b); }

    private:
        //--------------------------------------------------------------------------
        // Internals
        //--------------------------------------------------------------------------

        static constexpr view_type empty_sv_() noexcept { return view_type {}; }

        // Discriminant
        bool m_isSmall {true};
        // Storage union
        Storage m_storage {};
        // Allocator instance
        Alloc m_allocator {};

        // Small helpers
        static constexpr UIntSize SboSizeByteIndex() noexcept { return SBOBytes - 1; }

        UInt8 GetSmallSize() const noexcept
        {
            return static_cast<UInt8>(m_storage.small.bytes[SboSizeByteIndex()]);
        }
        void SetSmallSize(UInt8 n) noexcept
        {
            m_storage.small.bytes[SboSizeByteIndex()] = static_cast<std::byte>(n);
        }

        CharT* SmallData() noexcept
        {
            return reinterpret_cast<CharT*>(m_storage.small.bytes);
        }
        const CharT* SmallData() const noexcept
        {
            return reinterpret_cast<const CharT*>(m_storage.small.bytes);
        }

        void SetSmall() noexcept { m_isSmall = true; }

        // Heap allocation helpers
        void AllocateHeap(size_type capacity)
        {
            m_isSmall            = false;
            const UIntSize bytes = (capacity + 1) * sizeof(CharT);
            void*          p     = m_allocator.Allocate(bytes, alignof(CharT));
            if (!p)
                throw std::bad_alloc {};
            m_storage.heap.ptr    = reinterpret_cast<CharT*>(p);
            m_storage.heap.cap    = capacity;
            m_storage.heap.size   = 0;
            m_storage.heap.ptr[0] = CharT(0);
        }

        void Deallocate(CharT* ptr, size_type cap) noexcept
        {
            if (!ptr)
                return;
            const UIntSize bytes = (cap + 1) * sizeof(CharT);
            m_allocator.Deallocate(reinterpret_cast<void*>(ptr), bytes, alignof(CharT));
        }

        void DeallocateHeap() noexcept
        {
            Deallocate(m_storage.heap.ptr, m_storage.heap.cap);
            m_storage.heap.ptr  = nullptr;
            m_storage.heap.cap  = 0;
            m_storage.heap.size = 0;
        }

        void SetSize(size_type n) noexcept
        {
            if (m_isSmall)
            {
                SetSmallSize(static_cast<UInt8>(n));
                SmallData()[n] = CharT(0);
            }
            else
            {
                m_storage.heap.size   = n;
                m_storage.heap.ptr[n] = CharT(0);
            }
        }

        void InitFromView(view_type sv)
        {
            const size_type n = sv.size();
            if (n <= sbo_chars)
            {
                SetSmall();
                traits_type::copy(SmallData(), sv.data(), n);
                SmallData()[n] = CharT(0);
                SetSmallSize(static_cast<UInt8>(n));
            }
            else
            {
                const size_type cap = Growth::Grow(0, n);
                AllocateHeap(cap);
                traits_type::copy(m_storage.heap.ptr, sv.data(), n);
                m_storage.heap.ptr[n] = CharT(0);
                m_storage.heap.size   = n;
            }
        }

        // Reallocate using growth policy (strong guarantee)
        void ReallocateTo(size_type newCap)
        {
            // allocate new
            const UIntSize bytes = (newCap + 1) * sizeof(CharT);
            void*          p     = m_allocator.Allocate(bytes, alignof(CharT));
            if (!p)
                throw std::bad_alloc {};
            CharT* newPtr = reinterpret_cast<CharT*>(p);

            const size_type oldSize = Size();

            // copy old payload including terminator
            if (m_isSmall)
                std::memcpy(newPtr, SmallData(), (oldSize + 1) * sizeof(CharT));
            else
                std::memcpy(newPtr, m_storage.heap.ptr, (oldSize + 1) * sizeof(CharT));

            // release old heap if needed
            if (!m_isSmall)
                Deallocate(m_storage.heap.ptr, m_storage.heap.cap);

            // switch to heap with new buffer
            m_isSmall           = false;
            m_storage.heap.ptr  = newPtr;
            m_storage.heap.cap  = newCap;
            m_storage.heap.size = oldSize;
        }

        // Reallocate to exact capacity
        void reallocate_to_exact_(size_type newCap)
        {
            ReallocateTo(newCap);
        }

        // Append core with alias/overlap safety and strong guarantee
        void AppendView(view_type sv)
        {
            const size_type appendLen = sv.size();
            if (appendLen == 0)
                return;

            const CharT*    src     = sv.data();
            const size_type oldSize = Size();
            const size_type newSize = oldSize + appendLen;

            const CharT* oldData = Data();
            const bool   sourceAlias =
                    (src >= oldData) && (src < oldData + oldSize);

            if (newSize > Capacity())
            {
                // allocate new first
                const size_type newCap = Growth::Grow(Capacity(), newSize);
                const UIntSize  bytes  = (newCap + 1) * sizeof(CharT);
                void*           p      = m_allocator.Allocate(bytes, alignof(CharT));
                if (!p)
                    throw std::bad_alloc {};
                CharT* newPtr = reinterpret_cast<CharT*>(p);

                // copy old content
                if (m_isSmall)
                    std::memcpy(newPtr, SmallData(), oldSize * sizeof(CharT));
                else
                    std::memcpy(newPtr, m_storage.heap.ptr, oldSize * sizeof(CharT));

                // append segment; if aliasing, refer to the copied region inside newPtr
                if (sourceAlias)
                {
                    const size_type offset = static_cast<size_type>(src - oldData);
                    // src now lives at newPtr + offset
                    traits_type::move(newPtr + oldSize, newPtr + offset, appendLen);
                }
                else
                {
                    traits_type::copy(newPtr + oldSize, src, appendLen);
                }

                newPtr[newSize] = CharT(0);

                // free old heap if needed
                if (!m_isSmall)
                {
                    Deallocate(m_storage.heap.ptr, m_storage.heap.cap);
                }

                // switch to heap new buffer
                m_isSmall           = false;
                m_storage.heap.ptr  = newPtr;
                m_storage.heap.size = newSize;
                m_storage.heap.cap  = newCap;
            }
            else
            {
                // in-place append
                CharT* dst = Data();
                if (sourceAlias)
                {
                    // Use move when ranges might overlap
                    traits_type::move(dst + oldSize, dst + (src - oldData), appendLen);
                }
                else
                {
                    traits_type::copy(dst + oldSize, src, appendLen);
                }
                SetSize(newSize);
            }
        }
    };

    //--------------------------------------------------------------------------
    // Non-member operators (+)
    //--------------------------------------------------------------------------

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth>
    inline BasicString<CharT, SBOBytes, Alloc, Growth>
    operator+(const BasicString<CharT, SBOBytes, Alloc, Growth>& a,
              std::basic_string_view<CharT>                      b)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth> r(a);
        r += b;
        return r;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth>
    inline BasicString<CharT, SBOBytes, Alloc, Growth>
    operator+(std::basic_string_view<CharT>                      a,
              const BasicString<CharT, SBOBytes, Alloc, Growth>& b)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth> r(a, b.get_allocator());
        r += b;
        return r;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth>
    inline BasicString<CharT, SBOBytes, Alloc, Growth>
    operator+(const BasicString<CharT, SBOBytes, Alloc, Growth>& a,
              const BasicString<CharT, SBOBytes, Alloc, Growth>& b)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth> r(a);
        r += b;
        return r;
    }

    //--------------------------------------------------------------------------
    // Access to allocator (optional helper)
    //--------------------------------------------------------------------------

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth>
    inline const Alloc& get_allocator(const BasicString<CharT, SBOBytes, Alloc, Growth>& s) noexcept
    {
        // friend access not strictly necessary; could expose member if preferred
        struct Expose : BasicString<CharT, SBOBytes, Alloc, Growth>
        {
            using BasicString<CharT, SBOBytes, Alloc, Growth>::m_allocator;
        };
        return static_cast<const Expose&>(s).m_allocator;
    }

    //--------------------------------------------------------------------------
    // Engine-friendly aliases (same names as before, default SBO=48)
    //--------------------------------------------------------------------------

    using String      = BasicString<char, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using WString     = BasicString<wchar_t, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using AnsiString  = BasicString<char, 16, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using AsciiString = BasicString<char, 16, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;

}// namespace NGIN::Containers
