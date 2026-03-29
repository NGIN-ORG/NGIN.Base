/// @file BasicString.hpp
/// @brief Traits-aware allocator-backed string with small-buffer optimization.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#ifdef small
#undef small
#endif

namespace NGIN::Text
{
    struct DefaultGrowthPolicy
    {
        static constexpr UIntSize smallCapThreshold = 64;

        static constexpr UIntSize NextPow2(UIntSize value) noexcept
        {
            if (value <= 1)
                return 1;

            value -= 1;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
#if INTPTR_MAX == INT64_MAX
            value |= value >> 32;
#endif
            return value + 1;
        }

        static constexpr UIntSize Grow(UIntSize oldCap, UIntSize required) noexcept
        {
            if (oldCap < smallCapThreshold)
                return NextPow2(required);

            UIntSize grown = oldCap + oldCap / 2;
            if (grown < required)
                grown = required;
            return grown;
        }
    };

    template<class CharT,
             UIntSize                       SBOBytes,
             NGIN::Memory::AllocatorConcept Alloc = NGIN::Memory::SystemAllocator,
             class Growth                         = DefaultGrowthPolicy,
             class Traits                         = std::char_traits<CharT>>
    class BasicString
    {
        static_assert(std::is_trivial_v<CharT>, "BasicString requires a trivial character type.");
        static_assert(SBOBytes >= sizeof(CharT), "SBOBytes must be large enough for a terminator.");
        static_assert(std::same_as<typename Traits::char_type, CharT>,
                      "Traits::char_type must match CharT.");
        static_assert(requires(UIntSize oldCap, UIntSize required) {
            { Growth::Grow(oldCap, required) } noexcept -> std::convertible_to<UIntSize>; }, "Growth must provide static constexpr UIntSize Grow(UIntSize, UIntSize) noexcept.");
        static_assert(requires(CharT* dst, CharT& dstRef, const CharT* src, const CharT& ch, UIntSize count) {
            { Traits::length(src) } -> std::convertible_to<std::size_t>;
            { Traits::compare(src, src, count) } -> std::convertible_to<int>;
            { Traits::move(dst, src, count) } -> std::same_as<CharT*>;
            { Traits::copy(dst, src, count) } -> std::same_as<CharT*>;
            Traits::assign(dstRef, ch); }, "Traits must provide basic char_traits-compatible operations.");

    public:
        using ThisType    = BasicString<CharT, SBOBytes, Alloc, Growth, Traits>;
        using value_type  = CharT;
        using size_type   = UIntSize;
        using traits_type = Traits;
        using view_type   = std::basic_string_view<CharT, Traits>;

        static constexpr size_type npos = static_cast<size_type>(view_type::npos);

    private:
        static constexpr size_type ComputeSboChars() noexcept
        {
            const size_type totalChars = SBOBytes / sizeof(CharT);
            return totalChars == 0 ? 0 : totalChars - 1;
        }

        static constexpr size_type sbo_chars = ComputeSboChars();

        using SmallStorage = std::array<CharT, sbo_chars + 1>;

        static_assert(sizeof(SmallStorage) <= SBOBytes,
                      "SBOBytes is too small for the requested small buffer.");

    public:
        BasicString() noexcept(std::is_nothrow_default_constructible_v<Alloc>)
            : m_allocator()
        {
            ResetToEmptySmall();
        }

        explicit BasicString(const Alloc& alloc) noexcept
            : m_allocator(alloc)
        {
            ResetToEmptySmall();
        }

        explicit BasicString(Alloc&& alloc) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
            : m_allocator(std::move(alloc))
        {
            ResetToEmptySmall();
        }

        BasicString(const CharT* cstr, const Alloc& alloc = Alloc())
            : m_allocator(alloc)
        {
            ResetToEmptySmall();
            if (cstr != nullptr)
                InitFromView(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))});
        }

        BasicString(view_type sv, const Alloc& alloc = Alloc())
            : m_allocator(alloc)
        {
            ResetToEmptySmall();
            InitFromView(sv);
        }

        BasicString(size_type count, CharT ch, const Alloc& alloc = Alloc())
            : m_allocator(alloc)
        {
            ResetToEmptySmall();
            InitFilled(count, ch);
        }

        BasicString(const ThisType& other)
            : m_allocator(other.m_allocator)
        {
            ResetToEmptySmall();
            CopyConstructFrom(other);
        }

        BasicString(ThisType&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
            : m_allocator(std::move(other.m_allocator))
        {
            ResetToEmptySmall();
            MoveConstructFrom(std::move(other));
        }

        ~BasicString()
        {
            DestroyHeapIfAny();
        }

        ThisType& operator=(const ThisType& other)
        {
            if (this == &other)
                return *this;

            CopyAssignFrom(other);
            return *this;
        }

        ThisType& operator=(ThisType&& other) noexcept(
                (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment &&
                 std::is_nothrow_move_assignable_v<Alloc>) ||
                NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual)
        {
            if (this == &other)
                return *this;

            MoveAssignFrom(std::move(other));
            return *this;
        }

        ThisType& operator=(view_type sv)
        {
            Assign(sv);
            return *this;
        }

        ThisType& operator=(const CharT* cstr)
        {
            Assign(cstr != nullptr ? view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}
                                   : view_type {});
            return *this;
        }

        [[nodiscard]] size_type Size() const noexcept { return m_size; }
        [[nodiscard]] bool      Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] size_type Capacity() const noexcept { return m_isSmall ? sbo_chars : m_heapCapacity; }

        [[nodiscard]] const CharT* c_str() const noexcept { return Data(); }
        [[nodiscard]] const CharT* CStr() const noexcept { return c_str(); }
        [[nodiscard]] const Alloc& GetAllocator() const noexcept { return m_allocator; }
        [[nodiscard]] view_type    View() const noexcept { return view_type {Data(), Size()}; }

        [[nodiscard]] const CharT* Data() const noexcept
        {
            return m_isSmall ? m_smallData.data() : m_heapData;
        }

        [[nodiscard]] CharT* Data() noexcept
        {
            return m_isSmall ? m_smallData.data() : m_heapData;
        }

        [[nodiscard]] UIntSize GetSize() const noexcept { return Size(); }
        [[nodiscard]] UIntSize GetCapacity() const noexcept { return Capacity(); }

        const CharT& operator[](size_type index) const noexcept { return Data()[index]; }
        CharT&       operator[](size_type index) noexcept { return Data()[index]; }

        const CharT& At(size_type index) const
        {
            if (index >= Size())
                throw std::out_of_range("BasicString::At");
            return Data()[index];
        }

        CharT& At(size_type index)
        {
            if (index >= Size())
                throw std::out_of_range("BasicString::At");
            return Data()[index];
        }

        const CharT& Front() const { return At(0); }
        CharT&       Front() { return At(0); }

        const CharT& Back() const { return At(Size() - 1); }
        CharT&       Back() { return At(Size() - 1); }

        operator view_type() const noexcept { return View(); }

        void Clear() noexcept
        {
            m_size    = 0;
            Data()[0] = CharT(0);
        }

        void Reserve(size_type newCap)
        {
            if (newCap <= Capacity())
                return;
            ReallocateTo(CheckedGrow(Capacity(), newCap));
        }

        void ReserveExact(size_type newCap)
        {
            if (newCap <= Capacity())
                return;
            ReallocateTo(newCap);
        }

        void ShrinkToFit()
        {
            if (m_isSmall)
                return;

            if (m_size <= sbo_chars)
            {
                SmallStorage buffer {};
                CopyChars(buffer.data(), m_heapData, m_size);
                buffer[m_size] = CharT(0);
                CommitSmall(buffer.data(), m_size);
                return;
            }

            if (m_size < m_heapCapacity)
                ReallocateTo(m_size);
        }

        void Resize(size_type count)
        {
            Resize(count, CharT());
        }

        void Resize(size_type count, CharT ch)
        {
            if (count <= m_size)
            {
                SetSize(count);
                return;
            }

            if (count > Capacity())
                ReallocateTo(CheckedGrow(Capacity(), count));

            CharT* const destination = Data();
            FillChars(destination + m_size, count - m_size, ch);
            SetSize(count);
        }

        void PushBack(CharT ch)
        {
            if (m_size == Capacity())
                ReallocateTo(CheckedGrow(Capacity(), m_size + 1));

            CharT* const destination = Data();
            destination[m_size]      = ch;
            SetSize(m_size + 1);
        }

        void PopBack()
        {
            if (m_size == 0)
                return;
            SetSize(m_size - 1);
        }

        void Assign(view_type sv)
        {
            const size_type count  = sv.size();
            const CharT*    source = sv.data();

            if (count <= sbo_chars)
            {
                SmallStorage temp {};
                CopyChars(temp.data(), source, count);
                temp[count] = CharT(0);
                CommitSmall(temp.data(), count);
                return;
            }

            if (!m_isSmall && count <= m_heapCapacity)
            {
                MoveChars(m_heapData, source, count);
                m_heapData[count] = CharT(0);
                m_size            = count;
                return;
            }

            const size_type newCapacity = CheckedGrow(Capacity(), count);
            CharT* const    newData     = AllocateWith(m_allocator, newCapacity);
            CopyChars(newData, source, count);
            newData[count] = CharT(0);
            CommitHeap(newData, newCapacity, count);
        }

        void Append(const ThisType& other) { AppendView(other.View()); }
        void Append(view_type sv) { AppendView(sv); }
        void Append(const CharT* cstr)
        {
            AppendView(cstr != nullptr ? view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}
                                       : view_type {});
        }
        void Append(size_type count, CharT ch)
        {
            if (count == 0)
                return;

            const size_type oldSize = Size();
            const size_type newSize = CheckedAppendSize(oldSize, count);
            if (newSize > Capacity())
                ReallocateTo(CheckedGrow(Capacity(), newSize));

            FillChars(Data() + oldSize, count, ch);
            SetSize(newSize);
        }
        void Append(CharT ch) { PushBack(ch); }

        [[nodiscard]] ThisType Substr(size_type pos = 0, size_type count = npos) const
        {
            if (pos > Size())
                throw std::out_of_range("BasicString::Substr");

            const size_type actualCount = std::min(count, Size() - pos);
            return ThisType(view_type {Data() + pos, actualCount}, m_allocator);
        }

        ThisType& Insert(size_type pos, view_type value)
        {
            Replace(pos, 0, value);
            return *this;
        }

        ThisType& Insert(size_type pos, const CharT* cstr)
        {
            Replace(pos,
                    0,
                    cstr != nullptr ? view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}
                                    : view_type {});
            return *this;
        }

        ThisType& Insert(size_type pos, size_type count, CharT ch)
        {
            if (count == 0)
                return *this;

            ThisType fill(count, ch, m_allocator);
            Replace(pos, 0, fill.View());
            return *this;
        }

        ThisType& Erase(size_type pos, size_type count = npos)
        {
            Replace(pos, count, view_type {});
            return *this;
        }

        ThisType& RemovePrefix(size_type count)
        {
            if (count > Size())
                throw std::out_of_range("BasicString::RemovePrefix");

            Erase(0, count);
            return *this;
        }

        ThisType& RemoveSuffix(size_type count)
        {
            if (count > Size())
                throw std::out_of_range("BasicString::RemoveSuffix");

            Erase(Size() - count, count);
            return *this;
        }

        ThisType& Replace(size_type pos, size_type count, view_type replacement)
        {
            if (pos > Size())
                throw std::out_of_range("BasicString::Replace");

            const size_type eraseCount = std::min(count, Size() - pos);

            ThisType  replacementStorage(m_allocator);
            view_type replacementView = replacement;
            if (OverlapsSelf(replacement.data(), replacement.size()))
            {
                replacementStorage.Assign(replacement);
                replacementView = replacementStorage.View();
            }

            ReplaceRange(pos, eraseCount, replacementView);
            return *this;
        }

        ThisType& Replace(size_type pos, size_type count, const CharT* cstr)
        {
            Replace(pos,
                    count,
                    cstr != nullptr ? view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}
                                    : view_type {});
            return *this;
        }

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

        void Swap(ThisType& other) noexcept(
                NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual ||
                (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnSwap &&
                 std::is_nothrow_swappable_v<Alloc>) )
        {
            if (this == &other)
                return;

            using propagation = NGIN::Memory::AllocatorPropagationTraits<Alloc>;

            if constexpr (propagation::PropagateOnSwap || propagation::IsAlwaysEqual)
            {
                using std::swap;
                swap(m_isSmall, other.m_isSmall);
                swap(m_size, other.m_size);
                swap(m_heapData, other.m_heapData);
                swap(m_heapCapacity, other.m_heapCapacity);
                swap(m_smallData, other.m_smallData);

                if constexpr (propagation::PropagateOnSwap)
                    swap(m_allocator, other.m_allocator);
            }
            else
            {
                ThisType thisValue(View(), m_allocator);
                ThisType otherValue(other.View(), other.m_allocator);

                DestroyHeapIfAny();
                other.DestroyHeapIfAny();
                ResetToEmptySmall();
                other.ResetToEmptySmall();
                SwapValueState(otherValue);
                other.SwapValueState(thisValue);
            }
        }

        [[nodiscard]] int Compare(view_type rhs) const noexcept
        {
            const size_type common = std::min(Size(), rhs.size());
            if (common != 0)
            {
                const int cmp = traits_type::compare(Data(), rhs.data(), common);
                if (cmp != 0)
                    return cmp;
            }

            if (Size() < rhs.size())
                return -1;
            if (Size() > rhs.size())
                return 1;
            return 0;
        }

        [[nodiscard]] bool StartsWith(view_type prefix) const noexcept
        {
            return prefix.size() <= Size() &&
                   (prefix.empty() || traits_type::compare(Data(), prefix.data(), prefix.size()) == 0);
        }

        [[nodiscard]] bool StartsWith(const CharT* cstr) const noexcept
        {
            return cstr != nullptr && StartsWith(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))});
        }

        [[nodiscard]] bool StartsWith(CharT ch) const noexcept
        {
            return !Empty() && traits_type::compare(Data(), &ch, 1) == 0;
        }

        [[nodiscard]] bool EndsWith(view_type suffix) const noexcept
        {
            if (suffix.size() > Size())
                return false;

            if (suffix.empty())
                return true;

            return traits_type::compare(Data() + (Size() - suffix.size()), suffix.data(), suffix.size()) == 0;
        }

        [[nodiscard]] bool EndsWith(const CharT* cstr) const noexcept
        {
            return cstr != nullptr && EndsWith(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))});
        }

        [[nodiscard]] bool EndsWith(CharT ch) const noexcept
        {
            return !Empty() && traits_type::compare(Data() + (Size() - 1), &ch, 1) == 0;
        }

        [[nodiscard]] bool Contains(view_type value) const noexcept
        {
            return Find(value) != npos;
        }

        [[nodiscard]] size_type Find(CharT ch, size_type pos = 0) const noexcept
        {
            return Find(view_type {std::addressof(ch), 1}, pos);
        }

        [[nodiscard]] size_type Find(const CharT* cstr, size_type pos = 0) const noexcept
        {
            return cstr != nullptr ? Find(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}, pos)
                                   : npos;
        }

        [[nodiscard]] size_type Find(view_type value, size_type pos = 0) const noexcept
        {
            const view_type haystack = View();
            if (value.empty())
                return std::min(pos, haystack.size());

            if (value.size() > haystack.size())
                return npos;

            if (pos > haystack.size() - value.size())
                return npos;

            if constexpr (CanUseRawCharSearch())
            {
                if (value.size() == 1)
                    return FindSingleRawChar(haystack, value[0], pos);

                // Two-byte needles are common enough in token parsing to justify a memchr-led fast path.
                if (value.size() == 2)
                    return FindTwoRawChars(haystack, value[0], value[1], pos);
            }

            for (size_type index = pos; index <= haystack.size() - value.size(); ++index)
            {
                if (traits_type::compare(haystack.data() + index, value.data(), value.size()) == 0)
                    return index;
            }

            return npos;
        }

        [[nodiscard]] size_type RFind(CharT ch, size_type pos = npos) const noexcept
        {
            return RFind(view_type {std::addressof(ch), 1}, pos);
        }

        [[nodiscard]] size_type RFind(const CharT* cstr, size_type pos = npos) const noexcept
        {
            return cstr != nullptr ? RFind(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}, pos)
                                   : npos;
        }

        [[nodiscard]] size_type RFind(view_type value, size_type pos = npos) const noexcept
        {
            const view_type haystack = View();
            if (value.empty())
                return std::min(pos, haystack.size());

            if (value.size() > haystack.size())
                return npos;

            size_type index = LastNeedleStart(haystack.size(), value.size(), pos);
            if constexpr (CanUseRawCharSearch())
            {
                if (value.size() == 1)
                    return RFindSingleRawChar(haystack, value[0], index);

                // Mirror the short-needle fast path for reverse scans before falling back to Traits::compare.
                if (value.size() == 2)
                    return RFindTwoRawChars(haystack, value[0], value[1], index);
            }

            while (true)
            {
                if (traits_type::compare(haystack.data() + index, value.data(), value.size()) == 0)
                    return index;

                if (index == 0)
                    break;
                --index;
            }

            return npos;
        }

        [[nodiscard]] size_type FindFirstOf(CharT ch, size_type pos = 0) const noexcept
        {
            return Find(ch, pos);
        }

        [[nodiscard]] size_type FindFirstOf(const CharT* cstr, size_type pos = 0) const noexcept
        {
            return cstr != nullptr
                           ? FindFirstOf(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}, pos)
                           : npos;
        }

        [[nodiscard]] size_type FindFirstOf(view_type values, size_type pos = 0) const noexcept
        {
            const view_type haystack = View();
            if (pos >= haystack.size() || values.empty())
                return npos;

            if constexpr (CanUseRawCharSearch())
            {
                if (values.size() == 1)
                    return FindSingleRawChar(haystack, values[0], pos);

                // Small sets are cheaper to scan linearly; larger sets amortize the 256-byte lookup table.
                if (values.size() > SmallCharSetLinearThreshold)
                {
                    const auto lookup = BuildRawCharLookup(values);
                    for (size_type index = pos; index < haystack.size(); ++index)
                    {
                        if (lookup[ToRawByte(haystack[index])])
                            return index;
                    }
                    return npos;
                }
            }

            for (size_type index = pos; index < haystack.size(); ++index)
            {
                if (ContainsChar(values, haystack[index]))
                    return index;
            }

            return npos;
        }

        [[nodiscard]] size_type FindLastOf(CharT ch, size_type pos = npos) const noexcept
        {
            return RFind(ch, pos);
        }

        [[nodiscard]] size_type FindLastOf(const CharT* cstr, size_type pos = npos) const noexcept
        {
            return cstr != nullptr
                           ? FindLastOf(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}, pos)
                           : npos;
        }

        [[nodiscard]] size_type FindLastOf(view_type values, size_type pos = npos) const noexcept
        {
            const view_type haystack = View();
            if (haystack.empty() || values.empty())
                return npos;

            size_type index = std::min(pos, haystack.size() - 1);
            if constexpr (CanUseRawCharSearch())
            {
                if (values.size() == 1)
                    return RFindSingleRawChar(haystack, values[0], index);

                // Small sets are cheaper to scan linearly; larger sets amortize the 256-byte lookup table.
                if (values.size() > SmallCharSetLinearThreshold)
                {
                    const auto lookup = BuildRawCharLookup(values);
                    while (true)
                    {
                        if (lookup[ToRawByte(haystack[index])])
                            return index;

                        if (index == 0)
                            break;
                        --index;
                    }
                    return npos;
                }
            }

            while (true)
            {
                if (ContainsChar(values, haystack[index]))
                    return index;

                if (index == 0)
                    break;
                --index;
            }

            return npos;
        }

        [[nodiscard]] size_type FindFirstNotOf(CharT ch, size_type pos = 0) const noexcept
        {
            return FindFirstNotOf(view_type {std::addressof(ch), 1}, pos);
        }

        [[nodiscard]] size_type FindFirstNotOf(const CharT* cstr, size_type pos = 0) const noexcept
        {
            return cstr != nullptr
                           ? FindFirstNotOf(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}, pos)
                           : npos;
        }

        [[nodiscard]] size_type FindFirstNotOf(view_type values, size_type pos = 0) const noexcept
        {
            const view_type haystack = View();
            if (pos >= haystack.size())
                return npos;

            if (values.empty())
                return pos;

            if constexpr (CanUseRawCharSearch())
            {
                // Small sets are cheaper to scan linearly; larger sets amortize the 256-byte lookup table.
                if (values.size() > SmallCharSetLinearThreshold)
                {
                    const auto lookup = BuildRawCharLookup(values);
                    for (size_type index = pos; index < haystack.size(); ++index)
                    {
                        if (!lookup[ToRawByte(haystack[index])])
                            return index;
                    }
                    return npos;
                }
            }

            for (size_type index = pos; index < haystack.size(); ++index)
            {
                if (!ContainsChar(values, haystack[index]))
                    return index;
            }

            return npos;
        }

        [[nodiscard]] size_type FindLastNotOf(CharT ch, size_type pos = npos) const noexcept
        {
            return FindLastNotOf(view_type {std::addressof(ch), 1}, pos);
        }

        [[nodiscard]] size_type FindLastNotOf(const CharT* cstr, size_type pos = npos) const noexcept
        {
            return cstr != nullptr
                           ? FindLastNotOf(view_type {cstr, static_cast<size_type>(traits_type::length(cstr))}, pos)
                           : npos;
        }

        [[nodiscard]] size_type FindLastNotOf(view_type values, size_type pos = npos) const noexcept
        {
            const view_type haystack = View();
            if (haystack.empty())
                return npos;

            size_type index = std::min(pos, haystack.size() - 1);
            if (values.empty())
                return index;

            if constexpr (CanUseRawCharSearch())
            {
                // Small sets are cheaper to scan linearly; larger sets amortize the 256-byte lookup table.
                if (values.size() > SmallCharSetLinearThreshold)
                {
                    const auto lookup = BuildRawCharLookup(values);
                    while (true)
                    {
                        if (!lookup[ToRawByte(haystack[index])])
                            return index;

                        if (index == 0)
                            break;
                        --index;
                    }
                    return npos;
                }
            }

            while (true)
            {
                if (!ContainsChar(values, haystack[index]))
                    return index;

                if (index == 0)
                    break;
                --index;
            }

            return npos;
        }

        friend bool operator==(const ThisType& left, view_type right) noexcept
        {
            return left.Size() == right.size() &&
                   (left.Size() == 0 || traits_type::compare(left.Data(), right.data(), left.Size()) == 0);
        }

        friend bool operator==(view_type left, const ThisType& right) noexcept
        {
            return right == left;
        }

        friend bool operator==(const ThisType& left, const ThisType& right) noexcept
        {
            return left == right.View();
        }

        friend bool operator!=(const ThisType& left, const ThisType& right) noexcept
        {
            return !(left == right);
        }

    private:
        static constexpr size_type SmallCharSetLinearThreshold = 4;

        [[nodiscard]] static constexpr size_type MaxCapacity() noexcept
        {
            return (std::numeric_limits<size_type>::max() / sizeof(CharT)) - 1;
        }

        [[nodiscard]] static constexpr bool CanUseRawCharSearch() noexcept
        {
            return std::same_as<CharT, char> && std::same_as<Traits, std::char_traits<char>>;
        }

        static void ValidateCapacity(size_type capacity)
        {
            if (capacity > MaxCapacity())
                throw std::length_error("BasicString capacity overflow");
        }

        [[nodiscard]] static size_type CheckedGrow(size_type oldCap, size_type required)
        {
            if (required > MaxCapacity())
                throw std::length_error("BasicString capacity overflow");

            size_type grown = Growth::Grow(oldCap, required);
            if (grown < required)
                grown = required;
            if (grown > MaxCapacity())
                grown = MaxCapacity();
            return grown;
        }

        [[nodiscard]] static size_type CheckedAppendSize(size_type lhs, size_type rhs)
        {
            if (rhs > MaxCapacity() - lhs)
                throw std::length_error("BasicString capacity overflow");
            return lhs + rhs;
        }

        static void CopyChars(CharT* destination, const CharT* source, size_type count)
        {
            if (count != 0)
                traits_type::copy(destination, source, count);
        }

        static void MoveChars(CharT* destination, const CharT* source, size_type count)
        {
            if (count != 0)
                traits_type::move(destination, source, count);
        }

        static void FillChars(CharT* destination, size_type count, CharT value)
        {
            for (size_type index = 0; index < count; ++index)
                traits_type::assign(destination[index], value);
        }

        [[nodiscard]] static size_type LastNeedleStart(size_type haystackSize, size_type needleSize, size_type pos) noexcept
        {
            const size_type lastStart = haystackSize - needleSize;
            return std::min(pos, lastStart);
        }

        [[nodiscard]] static bool ContainsChar(view_type values, CharT ch) noexcept
        {
            for (size_type index = 0; index < values.size(); ++index)
            {
                if (traits_type::compare(values.data() + index, std::addressof(ch), 1) == 0)
                    return true;
            }
            return false;
        }

        [[nodiscard]] static unsigned char ToRawByte(char ch) noexcept
        {
            return static_cast<unsigned char>(ch);
        }

        [[nodiscard]] static std::array<bool, 256> BuildRawCharLookup(view_type values) noexcept
        {
            std::array<bool, 256> lookup {};
            for (size_type index = 0; index < values.size(); ++index)
                lookup[ToRawByte(values[index])] = true;
            return lookup;
        }

        [[nodiscard]] static size_type FindSingleRawChar(view_type haystack, CharT ch, size_type pos) noexcept
        {
            const void* const hit =
                    std::memchr(haystack.data() + pos, ToRawByte(ch), static_cast<std::size_t>(haystack.size() - pos));
            if (hit == nullptr)
                return npos;

            return static_cast<size_type>(static_cast<const char*>(hit) - haystack.data());
        }

        [[nodiscard]] static size_type FindTwoRawChars(view_type haystack, CharT first, CharT second, size_type pos) noexcept
        {
            const CharT* search    = haystack.data() + pos;
            size_type    remaining = haystack.size() - pos;
            while (remaining >= 2)
            {
                const void* const hit =
                        std::memchr(search, ToRawByte(first), static_cast<std::size_t>(remaining - 1));
                if (hit == nullptr)
                    return npos;

                const CharT* const candidate = static_cast<const CharT*>(hit);
                if (candidate[1] == second)
                    return static_cast<size_type>(candidate - haystack.data());

                remaining = haystack.size() - static_cast<size_type>((candidate - haystack.data()) + 1);
                search    = candidate + 1;
            }

            return npos;
        }

        [[nodiscard]] static size_type RFindSingleRawChar(view_type haystack, CharT ch, size_type pos) noexcept
        {
            size_type index = pos;
            while (true)
            {
                if (haystack[index] == ch)
                    return index;

                if (index == 0)
                    break;
                --index;
            }

            return npos;
        }

        [[nodiscard]] static size_type RFindTwoRawChars(view_type haystack, CharT first, CharT second, size_type pos) noexcept
        {
            size_type index = pos;
            while (true)
            {
                if (haystack[index] == first && haystack[index + 1] == second)
                    return index;

                if (index == 0)
                    break;
                --index;
            }

            return npos;
        }

        [[nodiscard]] static CharT* AllocateWith(Alloc& allocator, size_type capacity)
        {
            ValidateCapacity(capacity);
            const UIntSize bytes = (capacity + 1) * sizeof(CharT);
            void* const    raw   = allocator.Allocate(bytes, alignof(CharT));
            if (raw == nullptr)
                throw std::bad_alloc {};
            return static_cast<CharT*>(raw);
        }

        static void DeallocateWith(Alloc& allocator, CharT* data, size_type capacity) noexcept
        {
            if (data == nullptr)
                return;

            const UIntSize bytes = (capacity + 1) * sizeof(CharT);
            allocator.Deallocate(static_cast<void*>(data), bytes, alignof(CharT));
        }

        void DestroyHeapIfAny() noexcept
        {
            if (!m_isSmall)
                DeallocateWith(m_allocator, m_heapData, m_heapCapacity);

            m_heapData     = nullptr;
            m_heapCapacity = 0;
        }

        void ResetToEmptySmall() noexcept
        {
            m_isSmall      = true;
            m_size         = 0;
            m_heapData     = nullptr;
            m_heapCapacity = 0;
            m_smallData[0] = CharT(0);
        }

        void SwapValueState(ThisType& other) noexcept
        {
            using std::swap;
            swap(m_isSmall, other.m_isSmall);
            swap(m_size, other.m_size);
            swap(m_heapData, other.m_heapData);
            swap(m_heapCapacity, other.m_heapCapacity);
            swap(m_smallData, other.m_smallData);
        }

        void CommitSmall(const CharT* source, size_type count)
        {
            DestroyHeapIfAny();
            ResetToEmptySmall();
            CopyChars(m_smallData.data(), source, count);
            m_smallData[count] = CharT(0);
            m_size             = count;
        }

        void CommitHeap(CharT* data, size_type capacity, size_type size) noexcept
        {
            DestroyHeapIfAny();
            m_isSmall        = false;
            m_heapData       = data;
            m_heapCapacity   = capacity;
            m_size           = size;
            m_heapData[size] = CharT(0);
        }

        void SetSize(size_type size) noexcept
        {
            m_size       = size;
            Data()[size] = CharT(0);
        }

        void InitFilled(size_type count, CharT ch)
        {
            if (count > Capacity())
                ReallocateTo(CheckedGrow(0, count));

            FillChars(Data(), count, ch);
            SetSize(count);
        }

        void InitFromView(view_type sv)
        {
            if (!sv.empty())
                Assign(sv);
        }

        void CopyConstructFrom(const ThisType& other)
        {
            if (other.m_isSmall)
            {
                CopyChars(m_smallData.data(), other.m_smallData.data(), other.m_size + 1);
                m_isSmall      = true;
                m_size         = other.m_size;
                m_heapData     = nullptr;
                m_heapCapacity = 0;
                return;
            }

            m_heapData     = AllocateWith(m_allocator, other.m_heapCapacity);
            m_heapCapacity = other.m_heapCapacity;
            m_isSmall      = false;
            m_size         = other.m_size;
            CopyChars(m_heapData, other.m_heapData, other.m_size + 1);
        }

        void MoveConstructFrom(ThisType&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
        {
            if (other.m_isSmall)
            {
                CopyChars(m_smallData.data(), other.m_smallData.data(), other.m_size + 1);
                m_isSmall      = true;
                m_size         = other.m_size;
                m_heapData     = nullptr;
                m_heapCapacity = 0;
                other.ResetToEmptySmall();
                return;
            }

            m_isSmall      = false;
            m_size         = other.m_size;
            m_heapData     = other.m_heapData;
            m_heapCapacity = other.m_heapCapacity;
            other.ResetToEmptySmall();
        }

        void CopyAssignFrom(const ThisType& other)
        {
            if constexpr (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnCopyAssignment)
            {
                if constexpr (std::is_nothrow_copy_assignable_v<Alloc>)
                {
                    ThisType temp(other);
                    DestroyHeapIfAny();
                    m_allocator = other.m_allocator;
                    ResetToEmptySmall();
                    SwapValueState(temp);
                }
                else
                {
                    DestroyHeapIfAny();
                    ResetToEmptySmall();
                    m_allocator = other.m_allocator;
                    CopyConstructFrom(other);
                }
            }
            else
            {
                Assign(other.View());
            }
        }

        void MoveAssignFrom(ThisType&& other) noexcept(
                (NGIN::Memory::AllocatorPropagationTraits<Alloc>::PropagateOnMoveAssignment &&
                 std::is_nothrow_move_assignable_v<Alloc>) ||
                NGIN::Memory::AllocatorPropagationTraits<Alloc>::IsAlwaysEqual)
        {
            using propagation = NGIN::Memory::AllocatorPropagationTraits<Alloc>;

            if constexpr (propagation::PropagateOnMoveAssignment)
            {
                if constexpr (std::is_nothrow_move_assignable_v<Alloc>)
                {
                    ThisType temp(std::move(other));
                    DestroyHeapIfAny();
                    m_allocator = std::move(temp.m_allocator);
                    ResetToEmptySmall();
                    SwapValueState(temp);
                }
                else
                {
                    DestroyHeapIfAny();
                    ResetToEmptySmall();
                    m_allocator = std::move(other.m_allocator);
                    MoveConstructFrom(std::move(other));
                }
            }
            else if constexpr (propagation::IsAlwaysEqual)
            {
                DestroyHeapIfAny();
                ResetToEmptySmall();
                SwapValueState(other);
            }
            else
            {
                Assign(other.View());
                other.Clear();
            }
        }

        void ReallocateTo(size_type newCapacity)
        {
            ValidateCapacity(newCapacity);

            if (newCapacity <= sbo_chars)
            {
                if (!m_isSmall)
                {
                    SmallStorage buffer {};
                    CopyChars(buffer.data(), m_heapData, m_size);
                    buffer[m_size] = CharT(0);
                    CommitSmall(buffer.data(), m_size);
                }
                return;
            }

            CharT* const newData = AllocateWith(m_allocator, newCapacity);
            CopyChars(newData, Data(), m_size + 1);
            CommitHeap(newData, newCapacity, m_size);
        }

        void AppendView(view_type sv)
        {
            const size_type appendCount = sv.size();
            if (appendCount == 0)
                return;

            const size_type oldSize = Size();
            const size_type newSize = CheckedAppendSize(oldSize, appendCount);
            const CharT*    source  = sv.data();

            const CharT* const dataStart = Data();
            const CharT* const dataEnd   = dataStart + oldSize;
            const CharT* const srcEnd    = source + appendCount;
            const auto         less      = std::less<const CharT*> {};
            const bool         overlaps =
                    less(source, dataEnd) && less(dataStart, srcEnd);

            if (newSize > Capacity())
            {
                const size_type newCapacity = CheckedGrow(Capacity(), newSize);
                CharT* const    newData     = AllocateWith(m_allocator, newCapacity);

                CopyChars(newData, dataStart, oldSize);
                CopyChars(newData + oldSize, source, appendCount);
                newData[newSize] = CharT(0);

                if (!m_isSmall)
                    DeallocateWith(m_allocator, m_heapData, m_heapCapacity);

                m_isSmall      = false;
                m_heapData     = newData;
                m_heapCapacity = newCapacity;
                m_size         = newSize;
                return;
            }

            CharT* const destination = Data();
            if (overlaps)
                MoveChars(destination + oldSize, source, appendCount);
            else
                CopyChars(destination + oldSize, source, appendCount);

            SetSize(newSize);
        }

        [[nodiscard]] bool OverlapsSelf(const CharT* source, size_type count) const noexcept
        {
            if (count == 0)
                return false;

            const CharT* const dataStart = Data();
            const CharT* const dataEnd   = dataStart + Size();
            const CharT* const sourceEnd = source + count;
            const auto         less      = std::less<const CharT*> {};
            return less(source, dataEnd) && less(dataStart, sourceEnd);
        }

        void ReplaceRange(size_type pos, size_type eraseCount, view_type replacement)
        {
            const size_type oldSize     = Size();
            const size_type suffixCount = oldSize - pos - eraseCount;
            const size_type newSize     = CheckedAppendSize(oldSize - eraseCount, replacement.size());

            if (newSize <= sbo_chars)
            {
                SmallStorage buffer {};

                CopyChars(buffer.data(), Data(), pos);
                CopyChars(buffer.data() + pos, replacement.data(), replacement.size());
                CopyChars(buffer.data() + pos + replacement.size(),
                          Data() + pos + eraseCount,
                          suffixCount);
                buffer[newSize] = CharT(0);
                CommitSmall(buffer.data(), newSize);
                return;
            }

            if (!m_isSmall && newSize <= m_heapCapacity)
            {
                CharT* const destination = m_heapData;
                if (replacement.size() != eraseCount)
                {
                    MoveChars(destination + pos + replacement.size(),
                              destination + pos + eraseCount,
                              suffixCount);
                }

                CopyChars(destination + pos, replacement.data(), replacement.size());
                SetSize(newSize);
                return;
            }

            const size_type newCapacity = CheckedGrow(Capacity(), newSize);
            CharT* const    newData     = AllocateWith(m_allocator, newCapacity);

            CopyChars(newData, Data(), pos);
            CopyChars(newData + pos, replacement.data(), replacement.size());
            CopyChars(newData + pos + replacement.size(), Data() + pos + eraseCount, suffixCount);
            newData[newSize] = CharT(0);
            CommitHeap(newData, newCapacity, newSize);
        }

        bool                        m_isSmall {true};
        size_type                   m_size {0};
        CharT*                      m_heapData {nullptr};
        size_type                   m_heapCapacity {0};
        SmallStorage                m_smallData {};
        [[no_unique_address]] Alloc m_allocator {};
    };

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& left,
              std::basic_string_view<CharT, Traits>                      right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(left);
        result += right;
        return result;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(std::basic_string_view<CharT, Traits>                      left,
              const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(left, right.GetAllocator());
        result += right;
        return result;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& left,
              const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(left);
        result += right;
        return result;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& left,
              const CharT*                                               right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(left);
        result += right;
        return result;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(const CharT*                                               left,
              const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(
                left != nullptr ? std::basic_string_view<CharT, Traits> {left, static_cast<UIntSize>(Traits::length(left))}
                                : std::basic_string_view<CharT, Traits> {},
                right.GetAllocator());
        result += right;
        return result;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& left,
              CharT                                                      right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(left);
        result += right;
        return result;
    }

    template<class CharT, UIntSize SBOBytes, class Alloc, class Growth, class Traits>
    inline BasicString<CharT, SBOBytes, Alloc, Growth, Traits>
    operator+(CharT                                                      left,
              const BasicString<CharT, SBOBytes, Alloc, Growth, Traits>& right)
    {
        BasicString<CharT, SBOBytes, Alloc, Growth, Traits> result(1, left, right.GetAllocator());
        result += right;
        return result;
    }
}// namespace NGIN::Text
