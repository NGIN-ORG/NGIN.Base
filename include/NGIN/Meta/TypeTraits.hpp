/// @file TypeTraits.hpp
/// @brief Compile-time reflection utilities for extracting type names.
/// @note Requires C++20 for `constexpr` and `std::string_view`.
#pragma once

#include <type_traits>
#include <string_view>

namespace NGIN::Meta
{
    // Name reflection helpers moved to <NGIN/Meta/TypeName.hpp>

    /// <summary>Base traits class exposing fundamental type properties.</summary>
    /// <typeparam name="T">Type to inspect.</typeparam>
    template<typename T>
    struct TypeTraits
    {
    private:
        // Self is the decayed (cv/ref removed) form of T. We intentionally DO NOT remove pointer
        // indirection for the primary trait predicates so pointer types are not misclassified based
        // on their pointee. Pointee / element oriented aliases are exposed explicitly.
        using NoRef      = std::remove_reference_t<T>;
        using Self       = std::remove_cv_t<NoRef>;        // primary inspected type (without cv/ref)
        using PointeeRaw = std::remove_pointer_t<Self>;    // single level removal
        using ElementRaw = std::remove_all_extents_t<Self>;// underlying element of array(s)

        // Helper to guard enum underlying type extraction.
        template<typename U, bool = std::is_enum_v<U>>
        struct UnderlyingHelper
        {
            using type = void;
        };
        template<typename U>
        struct UnderlyingHelper<U, true>
        {
            using type = std::underlying_type_t<U>;
        };

        template<typename U, bool = (std::is_integral_v<U> && !std::is_same_v<U, bool>)>
        struct MakeSignedHelper
        {
            using type = U;
        };
        template<typename U>
        struct MakeSignedHelper<U, true>
        {
            using type = std::make_signed_t<U>;
        };
        template<typename U, bool = (std::is_integral_v<U> && !std::is_same_v<U, bool>)>
        struct MakeUnsignedHelper
        {
            using type = U;
        };
        template<typename U>
        struct MakeUnsignedHelper<U, true>
        {
            using type = std::make_unsigned_t<U>;
        };

    public:
        // Exposed aliases for meta-programming pipelines.
        using Raw          = T;   // original template argument as-is
        using Decayed      = Self;// cv/ref stripped
        using Pointee      = std::conditional_t<std::is_pointer_v<Self>, PointeeRaw, void>;
        using Element      = ElementRaw;                             // for arrays (or Self if not an array)
        using Underlying   = typename UnderlyingHelper<Self>::type;  // enum underlying or void
        using MakeSigned   = typename MakeSignedHelper<Self>::type;  // signed counterpart or Self
        using MakeUnsigned = typename MakeUnsignedHelper<Self>::type;// unsigned counterpart or Self

        // Size / alignment queries (constexpr for clear compile-time intent)
        static constexpr std::size_t SizeOf() noexcept { return sizeof(Self); }
        static constexpr std::size_t Alignment() noexcept { return alignof(Self); }
        static constexpr std::size_t Rank() noexcept { return std::rank_v<Self>; }
        template<std::size_t N = 0>
        static constexpr std::size_t Extent() noexcept
        {
            return std::extent_v<Self, N>;
        }

        // cv/ref / indirection
        static constexpr bool IsConst() noexcept { return std::is_const_v<NoRef>; }
        static constexpr bool IsVolatile() noexcept { return std::is_volatile_v<NoRef>; }
        static constexpr bool IsPointer() noexcept { return std::is_pointer_v<Self>; }
        static constexpr bool IsReference() noexcept { return std::is_reference_v<T>; }
        static constexpr bool IsLvalueReference() noexcept { return std::is_lvalue_reference_v<T>; }
        static constexpr bool IsRvalueReference() noexcept { return std::is_rvalue_reference_v<T>; }
        static constexpr bool IsArray() noexcept { return std::is_array_v<Self>; }
        static constexpr bool IsBoundedArray() noexcept { return std::is_bounded_array_v<Self>; }
        static constexpr bool IsUnboundedArray() noexcept { return std::is_unbounded_array_v<Self>; }

        // Core classification
        static constexpr bool IsEnum() noexcept { return std::is_enum_v<Self>; }
        static constexpr bool IsScopedEnum() noexcept { return std::is_scoped_enum_v<Self>; }
        static constexpr bool IsClass() noexcept { return std::is_class_v<Self>; }
        static constexpr bool IsUnion() noexcept { return std::is_union_v<Self>; }
        static constexpr bool IsAggregate() noexcept { return std::is_aggregate_v<Self>; }
        static constexpr bool IsPolymorphic() noexcept { return std::is_polymorphic_v<Self>; }
        static constexpr bool IsAbstract() noexcept { return std::is_abstract_v<Self>; }
        static constexpr bool IsFinal() noexcept { return std::is_final_v<Self>; }
        static constexpr bool HasVirtualDestructor() noexcept { return std::has_virtual_destructor_v<Self>; }

        // Value category / numeric traits (self, not pointee)
        static constexpr bool IsIntegral() noexcept { return std::is_integral_v<Self>; }
        static constexpr bool IsFloatingPoint() noexcept { return std::is_floating_point_v<Self>; }
        static constexpr bool IsArithmetic() noexcept { return std::is_arithmetic_v<Self>; }
        static constexpr bool IsFundamental() noexcept { return std::is_fundamental_v<Self>; }
        static constexpr bool IsSigned() noexcept { return std::is_signed_v<Self>; }
        static constexpr bool IsUnsigned() noexcept { return std::is_unsigned_v<Self>; }

        // Trivial / layout / construction traits
        static constexpr bool IsTrivial() noexcept { return std::is_trivial_v<Self>; }
        static constexpr bool IsTriviallyCopyable() noexcept { return std::is_trivially_copyable_v<Self>; }
        static constexpr bool IsTriviallyConstructible() noexcept { return std::is_trivially_constructible_v<Self>; }
        static constexpr bool IsTriviallyDefaultConstructible() noexcept { return std::is_trivially_default_constructible_v<Self>; }
        static constexpr bool IsTriviallyDestructible() noexcept { return std::is_trivially_destructible_v<Self>; }
        static constexpr bool IsTriviallyMoveConstructible() noexcept { return std::is_trivially_move_constructible_v<Self>; }
        static constexpr bool IsTriviallyCopyConstructible() noexcept { return std::is_trivially_copy_constructible_v<Self>; }
        static constexpr bool IsTriviallyMoveAssignable() noexcept { return std::is_trivially_move_assignable_v<Self>; }
        static constexpr bool IsTriviallyCopyAssignable() noexcept { return std::is_trivially_copy_assignable_v<Self>; }

        // Nothrow guarantees
        static constexpr bool IsNothrowDefaultConstructible() noexcept { return std::is_nothrow_default_constructible_v<Self>; }
        static constexpr bool IsNothrowMoveConstructible() noexcept { return std::is_nothrow_move_constructible_v<Self>; }
        static constexpr bool IsNothrowCopyConstructible() noexcept { return std::is_nothrow_copy_constructible_v<Self>; }
        static constexpr bool IsNothrowMoveAssignable() noexcept { return std::is_nothrow_move_assignable_v<Self>; }
        static constexpr bool IsNothrowCopyAssignable() noexcept { return std::is_nothrow_copy_assignable_v<Self>; }
        static constexpr bool IsNothrowDestructible() noexcept { return std::is_nothrow_destructible_v<Self>; }

        static constexpr bool IsBitwiseRelocatable() noexcept
        {
            return std::is_trivially_copyable_v<Self> && !std::is_volatile_v<Self>;
        }

        // Safe to relocate by element-wise move + destroy (no memcpy implied)
        static constexpr bool IsMoveRelocatable() noexcept
        {
            return std::is_trivially_move_constructible_v<Self> &&
                   std::is_trivially_destructible_v<Self> &&
                   !std::is_volatile_v<Self>;
        }

        // Misc
        static constexpr bool IsVoid() noexcept { return std::is_void_v<Self>; }
        static constexpr bool IsEmpty() noexcept { return std::is_empty_v<Self>; }
        static constexpr bool IsStandardLayout() noexcept { return std::is_standard_layout_v<Self>; }
    };


}// namespace NGIN::Meta
