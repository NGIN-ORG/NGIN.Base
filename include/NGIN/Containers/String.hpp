#pragma once

#include <NGIN/Defines.hpp>// For UIntSize, UInt8 definitions
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/LSBFlag.hpp>
#include <NGIN/Utilities/MSBFlag.hpp>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace NGIN::Containers
{


    /// @class BasicString
    /// @brief A string class with small buffer optimization.
    /// @tparam CharT   Character type.
    /// @tparam SBOSize Size in bytes of the internal buffer union.
    template<typename CharT, UIntSize SBOSize>
    class BasicString
    {
    public:
        /// @brief Public alias for convenience.
        using ThisType = BasicString<CharT, SBOSize>;

        /// @brief Default constructor: creates an empty SBO string.
        BasicString() noexcept;

        /// @brief Construct from a C-style null-terminated string.
        /// @param str May be nullptr => empty.
        BasicString(const CharT* str);

        /// @brief Copy constructor. Deep-copies the heap data if not in SBO mode.
        /// @param other Source BasicString
        BasicString(const ThisType& other);

        /// @brief Move constructor. Transfers ownership if heap-based.
        /// @param other Source BasicString (rvalue)
        BasicString(ThisType&& other) noexcept;

        /// @brief Destructor. Frees heap data if not in SBO mode.
        ~BasicString();

        /// @brief Copy assignment.
        /// @param other Source BasicString
        /// @return Reference to *this
        BasicString& operator=(const ThisType& other);

        /// @brief Move assignment.
        /// @param other Source BasicString (rvalue)
        /// @return Reference to *this
        BasicString& operator=(ThisType&& other) noexcept;

        /// @brief Returns the current size (number of characters, excluding null terminator).
        /// @return The size
        UIntSize GetSize() const noexcept;

        /// @brief Returns a pointer to the C-style string data (always null-terminated).
        /// @return const CharT* pointer
        const CharT* CStr() const noexcept;

        /// @brief Append from another BasicString.
        /// @param other Source
        void Append(const ThisType& other);

        /// @brief Append from a C-style null-terminated string.
        /// @param str Source
        void Append(const CharT* str);

        /// @brief Append a single character.
        /// @param ch The character to append
        void Append(CharT ch);

        /// @brief Operator += to append another BasicString.
        ThisType& operator+=(const ThisType& other)
        {
            Append(other);
            return *this;
        }

        /// @brief Operator += to append C-string.
        ThisType& operator+=(const CharT* str)
        {
            Append(str);
            return *this;
        }

        /// @brief Operator += to append a single character.
        ThisType& operator+=(CharT ch)
        {
            Append(ch);
            return *this;
        }

        /// @brief Reserves capacity (if necessary) to hold at least newCapacity chars + 1 for terminator.
        /// @param newCapacity Desired capacity
        void Reserve(UIntSize newCapacity);

        /// @brief Convenience method: returns true if currently in SBO mode.
        bool IsSmall() const noexcept;

        /// @brief Returns the total capacity (not counting the null terminator).
        /// @return capacity in characters
        UIntSize GetCapacity() const noexcept;

        /// @brief Helper to get a modifiable pointer to the string data. (Not guaranteed unique!)
        CharT* Data();

    private:
        /// @brief Allocate a new buffer of `newCapacity + 1`, copy old data, and switch to normal storage.
        /// @param newCapacity   The capacity to allocate
        /// @param currentSize   How many characters to copy from old
        void ReallocateAndCopy(UIntSize newCapacity, UIntSize currentSize);

        /// @brief Internal method to get the number of SBO characters used, for IsSmall() case.
        ///        This is computed from the `remainingSizeFlag`.
        /// @return SBO size in chars
        UIntSize GetSBOSize() const noexcept;

        /// @brief Set the SBO size by adjusting the `remainingSizeFlag`.
        /// @param newSize New size for small storage
        void SetSBOSize(UIntSize newSize) noexcept;

        /// @brief Set the normal (heap) size in the flag.
        ///        This sets the "heap bit" to true, and sets the size in the leftover bits.
        /// @param newSize The new size
        void SetNormalSize(UIntSize newSize) noexcept;

        /// @brief Get the normal (heap) size from sizeFlag in normal storage.
        UIntSize GetNormalSize() const noexcept;

    public:
        /// @brief The compile-time size of the SBO union in bytes.
        static constexpr UIntSize sboSize = SBOSize;

        //--------------------------------------------------------------------------
        //  Internal Structures
        //--------------------------------------------------------------------------

        /// @struct NormalStorage
        /// @brief Structure for heap-based storage with potential padding.
        struct NormalStorage
        {
            CharT* data;      ///< Pointer to the heap
            UIntSize capacity;///< Allocated capacity

            static constexpr UIntSize overhead =
                    sizeof(CharT*) + sizeof(UIntSize) + sizeof(NGIN::Utilities::LSBFlag<UIntSize>);

            static_assert(overhead <= sboSize, "SBOSize is too small for NormalStorage overhead!");

            static constexpr UIntSize paddingBytes = sboSize - overhead;

            // We define a small template to store the padding
            template<UIntSize N>
            struct ByteArray
            {
                std::byte data[N];
            };
            struct EmptyStruct
            {
            };

            using PaddingType = std::conditional_t<
                    (paddingBytes > 0),
                    ByteArray<paddingBytes>,
                    EmptyStruct>;

            /// @brief Potentially zero-sized or small placeholder
            PaddingType padding;

#if defined(IS_BIG_ENDIAN)
            /// If big-endian, we store size with an LSBFlag
            NGIN::Utilities::LSBFlag<UIntSize> sizeFlag;
#else
            /// If little-endian, we store size with an MSBFlag
            NGIN::Utilities::MSBFlag<UIntSize> sizeFlag;
#endif
        };

        /// @struct NormalStorageNoPadding
        /// @brief If overhead == sboSize, we skip the padding field entirely.
        struct NormalStorageNoPadding
        {
            CharT* data;
            UIntSize capacity;
#if defined(IS_BIG_ENDIAN)
            NGIN::Utilities::LSBFlag<UIntSize> sizeFlag;
#else
            NGIN::Utilities::MSBFlag<UIntSize> sizeFlag;
#endif
        };

        /// @struct SmallStorage
        /// @brief Structure for SBO.
        ///
        /// We have exactly SBOSize bytes total. The last byte is an LSB/MSB flag storing
        /// `(remainingSize + 'isSmall' bit)`.
        struct SmallStorage
        {
            /// @brief We store characters in `sboBuffer`. We can store up to `sboSize - 1` chars,
            /// plus we need one byte for the `remainingSizeFlag`.
            CharT sboBuffer[sboSize - 1];

#if defined(IS_BIG_ENDIAN)
            NGIN::Utilities::LSBFlag<UInt8> remainingSizeFlag;
#else
            NGIN::Utilities::MSBFlag<UInt8> remainingSizeFlag;
#endif
        };

        /// @union StorageUnion
        /// @brief Union of either small or normal storage.
        union StorageUnion
        {
            /// @brief Choose between NormalStorage or NormalStorageNoPadding based on overhead < sboSize
            using NormalStorageType = std::conditional_t<
                    (NormalStorage::overhead < sboSize),
                    NormalStorage,
                    NormalStorageNoPadding>;

            NormalStorageType normal;
            SmallStorage small;
            /// @brief Raw byte access for easy copying, if needed.
            std::byte data[sboSize];
        };

    private:
        /// @brief The union storing either small or normal data.
        StorageUnion buffer {};
    };

    //-----------------------------------------------------------------------------
    //                          IMPLEMENTATION
    //-----------------------------------------------------------------------------

    //----------------------------------
    //  Private Helpers
    //----------------------------------

    /// @brief Check if we are in SBO mode.
    template<typename CharT, UIntSize SBOSize>
    inline bool BasicString<CharT, SBOSize>::IsSmall() const noexcept
    {
        // If "GetFlag()" is false => we're in SBO mode
        // or equivalently: !buffer.small.remainingSizeFlag.GetFlag()
        return !buffer.small.remainingSizeFlag.GetFlag();
    }

    /// @brief Get the current size if in normal (heap) mode.
    template<typename CharT, UIntSize SBOSize>
    inline UIntSize BasicString<CharT, SBOSize>::GetNormalSize() const noexcept
    {
        return buffer.normal.sizeFlag.GetValue();
    }

    /// @brief Set the normal (heap) size in the sizeFlag, marking heap bit to true.
    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::SetNormalSize(UIntSize newSize) noexcept
    {
        // second param is `true` => meaning "heap bit" = 1
        buffer.normal.sizeFlag.Set(newSize, true);
    }

    /// @brief Get the current size if in SBO mode: (sboSize - 1) - remaining
    template<typename CharT, UIntSize SBOSize>
    inline UIntSize BasicString<CharT, SBOSize>::GetSBOSize() const noexcept
    {
        UInt8 remaining = buffer.small.remainingSizeFlag.GetValue();
        return (sboSize - 1) - static_cast<UIntSize>(remaining);
    }

    /// @brief Set the SBO size by rewriting the "remainingSizeFlag".
    /// @param newSize The new size for the small buffer
    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::SetSBOSize(UIntSize newSize) noexcept
    {
        // remaining = (sboSize - 1) - newSize
        UIntSize remaining = (sboSize - 1) - newSize;
        buffer.small.remainingSizeFlag.Set(static_cast<UInt8>(remaining), false);// false => isSmall
    }

    //----------------------------------
    //  Constructors / Destructor
    //----------------------------------

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>::BasicString() noexcept
    {
        // Start empty in SBO
        SetSBOSize(0);
        // no need to null the entire union
        if constexpr (SBOSize > 1)
        {
            buffer.small.sboBuffer[0] = CharT('\0');
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>::BasicString(const CharT* str)
    {
        if (!str)
        {
            // same as default
            SetSBOSize(0);
            if constexpr (SBOSize > 1)
            {
                buffer.small.sboBuffer[0] = CharT('\0');
            }
            return;
        }

        // measure length
        UIntSize len = std::char_traits<CharT>::length(str);

        if (len <= (SBOSize - 1))
        {
            // fits in SBO
            std::memcpy(buffer.small.sboBuffer, str, len * sizeof(CharT));
            buffer.small.sboBuffer[len] = CharT('\0');
            SetSBOSize(len);
        }
        else
        {
            // allocate heap
            ReallocateAndCopy(len, 0);
            std::memcpy(buffer.normal.data, str, len * sizeof(CharT));
            buffer.normal.data[len] = CharT('\0');
            SetNormalSize(len);
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>::BasicString(const ThisType& other)
    {
        if (other.IsSmall())
        {
            // copy the entire union? simpler: just do:
            std::memcpy(&buffer, &other.buffer, sizeof(buffer));
        }
        else
        {
            // deep copy
            UIntSize sz = other.GetNormalSize();
            ReallocateAndCopy(sz, 0);// sets us to heap mode
            std::memcpy(buffer.normal.data, other.buffer.normal.data, sz * sizeof(CharT));
            buffer.normal.data[sz] = CharT('\0');
            SetNormalSize(sz);
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>::BasicString(ThisType&& other) noexcept
    {
        // Move: copy bits
        std::memcpy(&buffer, &other.buffer, sizeof(buffer));

        // Nullify `other`:
        if (other.IsSmall())
        {
            // set size=0 in `other`
            other.SetSBOSize(0);
            if constexpr (SBOSize > 1)
            {
                other.buffer.small.sboBuffer[0] = CharT('\0');
            }
        }
        else
        {
            // we stole the pointer, so make other empty small
            other.buffer.normal.data     = nullptr;
            other.buffer.normal.capacity = 0;
            other.SetNormalSize(0);// sets the "heap" bit but size=0
            // also optional: revert it to small if you prefer.
            // But that's more complicated. Let's just keep it consistent with normal=0
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>::~BasicString()
    {
        if (!IsSmall() && buffer.normal.data)
        {
            delete[] buffer.normal.data;
            buffer.normal.data = nullptr;
        }
    }

    //----------------------------------
    //  Assignment
    //----------------------------------

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>& BasicString<CharT, SBOSize>::operator=(const ThisType& other)
    {
        if (this == &other)
            return *this;

        // free current if not small
        if (!IsSmall() && buffer.normal.data)
        {
            delete[] buffer.normal.data;
            buffer.normal.data = nullptr;
        }

        // copy
        if (other.IsSmall())
        {
            // copy entire union
            std::memcpy(&buffer, &other.buffer, sizeof(buffer));
        }
        else
        {
            UIntSize sz = other.GetNormalSize();
            ReallocateAndCopy(sz, 0);
            std::memcpy(buffer.normal.data, other.buffer.normal.data, sz * sizeof(CharT));
            buffer.normal.data[sz] = CharT('\0');
            SetNormalSize(sz);
        }
        return *this;
    }

    template<typename CharT, UIntSize SBOSize>
    inline BasicString<CharT, SBOSize>& BasicString<CharT, SBOSize>::operator=(ThisType&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        // 1. Free our old data if we're currently normal
        if (!IsSmall() && buffer.normal.data)
        {
            delete[] buffer.normal.data;
            buffer.normal.data = nullptr;
        }

        // 2. Transfer bits from other
        std::memcpy(&buffer, &other.buffer, sizeof(buffer));

        // 3. Nullify the source so it doesn't free or reference the data again
        if (other.IsSmall())
        {
            // If other was small, just set it to an empty SBO
            other.SetSBOSize(0);
            if constexpr (SBOSize > 1)
            {
                other.buffer.small.sboBuffer[0] = CharT('\0');
            }
        }
        else
        {
            // If other was normal, we've "stolen" its pointer,
            // so set other to an empty normal, or convert to small with size=0.
            other.buffer.normal.data     = nullptr;
            other.buffer.normal.capacity = 0;
            other.SetNormalSize(0);
        }

        return *this;
    }


    //----------------------------------
    //   Observers
    //----------------------------------

    template<typename CharT, UIntSize SBOSize>
    inline UIntSize BasicString<CharT, SBOSize>::GetSize() const noexcept
    {
        if (IsSmall())
        {
            return GetSBOSize();
        }
        else
        {
            return GetNormalSize();
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline const CharT* BasicString<CharT, SBOSize>::CStr() const noexcept
    {
        if (IsSmall())
        {
            return buffer.small.sboBuffer;
        }
        else
        {
            return buffer.normal.data;
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline CharT* BasicString<CharT, SBOSize>::Data()
    {
        // Return modifiable pointer to data
        if (IsSmall())
        {
            return buffer.small.sboBuffer;
        }
        else
        {
            return buffer.normal.data;
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline UIntSize BasicString<CharT, SBOSize>::GetCapacity() const noexcept
    {
        if (IsSmall())
        {
            return (SBOSize > 0) ? (SBOSize - 1) : 0;
        }
        else
        {
            return buffer.normal.capacity;
        }
    }

    //----------------------------------
    //   Reserve
    //----------------------------------

    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::Reserve(UIntSize newCapacity)
    {
        UIntSize oldCap = GetCapacity();
        if (newCapacity <= oldCap)
        {
            return;// nothing to do
        }

        UIntSize sz = GetSize();
        ReallocateAndCopy(newCapacity, sz);
    }

    /// @brief Allocate new buffer with `newCapacity + 1`, copy existing data, and become normal storage.
    /// @param newCapacity The capacity in chars
    /// @param currentSize The current length to copy
    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::ReallocateAndCopy(UIntSize newCapacity, UIntSize currentSize)
    {
        // allocate new
        CharT* newData = new CharT[newCapacity + 1];// +1 for null terminator

        // copy
        if (currentSize > 0)
        {
            if (IsSmall())
            {
                // from SBO
                std::memcpy(newData, buffer.small.sboBuffer, currentSize * sizeof(CharT));
            }
            else
            {
                // from existing heap
                std::memcpy(newData, buffer.normal.data, currentSize * sizeof(CharT));
                // free old
                delete[] buffer.normal.data;
                buffer.normal.data = nullptr;
            }
        }
        else
        {
            // no old data
            if (!IsSmall() && buffer.normal.data)
            {
                delete[] buffer.normal.data;
                buffer.normal.data = nullptr;
            }
        }

        newData[currentSize] = CharT('\0');

        // fill normal storage
        buffer.normal.data     = newData;
        buffer.normal.capacity = newCapacity;
        SetNormalSize(currentSize);// sets heap mode
    }

    //----------------------------------
    //   Append
    //----------------------------------

    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::Append(const ThisType& other)
    {
        Append(other.CStr());
    }

    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::Append(const CharT* str)
    {
        if (!str)
        {
            return;// or handle differently if nullptr not expected
        }

        // Use char_traits for length
        const UIntSize appendLen = std::char_traits<CharT>::length(str);
        if (appendLen == 0)
        {
            return;// Nothing to do
        }

        // Cache old size/cap in local variables
        const UIntSize oldSize = GetSize();
        const UIntSize oldCap  = GetCapacity();
        const UIntSize newSize = oldSize + appendLen;

        // Check capacity
        if (newSize > oldCap)
        {
            const UIntSize newCap = std::max<UIntSize>(newSize, oldCap + (oldCap / 2));
            ReallocateAndCopy(newCap, oldSize);
        }

        // Append
        CharT* dst = Data();
        std::memcpy(dst + oldSize, str, appendLen * sizeof(CharT));
        dst[newSize] = CharT('\0');

        // Update size
        if (IsSmall())
        {
            SetSBOSize(newSize);
        }
        else
        {
            SetNormalSize(newSize);
        }
    }

    template<typename CharT, UIntSize SBOSize>
    inline void BasicString<CharT, SBOSize>::Append(CharT ch)
    {
        // build a tiny buffer [ch, '\0'] and reuse the C-string Append
        CharT tmp[2] = {ch, CharT('\0')};
        Append(tmp);
    }


    //------------------------------------------------------------------------------

    /// @brief A convenient alias for `BasicString<char, 48>`.
    using String      = BasicString<char, 48>;
    using WString     = BasicString<wchar_t, 48>;
    using AnsiString  = BasicString<char, 16>;// ANSI string with smaller buffer
    using AsciiString = BasicString<char, 16>;// ASCII string with smaller buffer


}// namespace NGIN::Containers
