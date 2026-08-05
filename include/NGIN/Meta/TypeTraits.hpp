/// @file TypeTraits.hpp
/// @brief Compile-time queries and transformations for C++ types.
#pragma once

#include <string_view>
#include <type_traits>

#include <utility>

namespace NGIN::Meta
{
    // Name reflection helpers moved to <NGIN/Meta/TypeName.hpp>

    /// @brief Exposes common compile-time properties and transformations for a type.
    /// @tparam T Type to inspect.
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
        /// @brief Original template argument, including its cv-ref qualifiers.
        using Raw = T;

        /// @brief Type with its cv-ref qualifiers removed.
        using Decayed = Self;

        /// @brief Pointed-to type, or `void` when `T` is not a pointer.
        using Pointee = std::conditional_t<std::is_pointer_v<Self>, PointeeRaw, void>;

        /// @brief Innermost array element type, or the decayed type when `T` is not an array.
        using Element = ElementRaw;

        /// @brief Enum underlying type, or `void` when `T` is not an enum.
        using Underlying = typename UnderlyingHelper<Self>::type;

        /// @brief Signed counterpart for non-boolean integral types; otherwise the decayed type.
        using MakeSigned = typename MakeSignedHelper<Self>::type;

        /// @brief Unsigned counterpart for non-boolean integral types; otherwise the decayed type.
        using MakeUnsigned = typename MakeUnsignedHelper<Self>::type;

        /// @brief Returns the size of the decayed type in bytes.
        static constexpr std::size_t SizeOf() noexcept { return sizeof(Self); }

        /// @brief Returns the alignment requirement of the decayed type in bytes.
        static constexpr std::size_t Alignment() noexcept { return alignof(Self); }

        /// @brief Returns the number of array dimensions in the decayed type.
        static constexpr std::size_t Rank() noexcept { return std::rank_v<Self>; }

        /// @brief Returns the extent of an array dimension.
        /// @tparam N Zero-based array dimension to query.
        template<std::size_t N = 0>
        static constexpr std::size_t Extent() noexcept
        {
            return std::extent_v<Self, N>;
        }

        /// @brief Returns whether the referred-to type is const-qualified.
        static constexpr bool IsConst() noexcept { return std::is_const_v<NoRef>; }

        /// @brief Returns whether the referred-to type is volatile-qualified.
        static constexpr bool IsVolatile() noexcept { return std::is_volatile_v<NoRef>; }

        /// @brief Returns whether the decayed type is a pointer.
        static constexpr bool IsPointer() noexcept { return std::is_pointer_v<Self>; }

        /// @brief Returns whether the original type is a reference.
        static constexpr bool IsReference() noexcept { return std::is_reference_v<T>; }

        /// @brief Returns whether the original type is an lvalue reference.
        static constexpr bool IsLvalueReference() noexcept { return std::is_lvalue_reference_v<T>; }

        /// @brief Returns whether the original type is an rvalue reference.
        static constexpr bool IsRvalueReference() noexcept { return std::is_rvalue_reference_v<T>; }

        /// @brief Returns whether the decayed type is an array.
        static constexpr bool IsArray() noexcept { return std::is_array_v<Self>; }

        /// @brief Returns whether the decayed type is an array with a known bound.
        static constexpr bool IsBoundedArray() noexcept { return std::is_bounded_array_v<Self>; }

        /// @brief Returns whether the decayed type is an array with an unknown bound.
        static constexpr bool IsUnboundedArray() noexcept { return std::is_unbounded_array_v<Self>; }

        /// @brief Returns whether the decayed type is an enumeration.
        static constexpr bool IsEnum() noexcept { return std::is_enum_v<Self>; }

        /// @brief Returns whether the decayed type is a scoped enumeration.
        static constexpr bool IsScopedEnum() noexcept { return std::is_scoped_enum_v<Self>; }

        /// @brief Returns whether the decayed type is a non-union class.
        static constexpr bool IsClass() noexcept { return std::is_class_v<Self>; }

        /// @brief Returns whether the decayed type is a union.
        static constexpr bool IsUnion() noexcept { return std::is_union_v<Self>; }

        /// @brief Returns whether the decayed type is an aggregate.
        static constexpr bool IsAggregate() noexcept { return std::is_aggregate_v<Self>; }

        /// @brief Returns whether the decayed type has a virtual function.
        static constexpr bool IsPolymorphic() noexcept { return std::is_polymorphic_v<Self>; }

        /// @brief Returns whether the decayed type is abstract.
        static constexpr bool IsAbstract() noexcept { return std::is_abstract_v<Self>; }

        /// @brief Returns whether the decayed class type is declared final.
        static constexpr bool IsFinal() noexcept { return std::is_final_v<Self>; }

        /// @brief Returns whether the decayed type has a virtual destructor.
        static constexpr bool HasVirtualDestructor() noexcept { return std::has_virtual_destructor_v<Self>; }

        /// @brief Returns whether the decayed type is integral.
        static constexpr bool IsIntegral() noexcept { return std::is_integral_v<Self>; }

        /// @brief Returns whether the decayed type is floating-point.
        static constexpr bool IsFloatingPoint() noexcept { return std::is_floating_point_v<Self>; }

        /// @brief Returns whether the decayed type is arithmetic.
        static constexpr bool IsArithmetic() noexcept { return std::is_arithmetic_v<Self>; }

        /// @brief Returns whether the decayed type is fundamental.
        static constexpr bool IsFundamental() noexcept { return std::is_fundamental_v<Self>; }

        /// @brief Returns whether the decayed arithmetic type is signed.
        static constexpr bool IsSigned() noexcept { return std::is_signed_v<Self>; }

        /// @brief Returns whether the decayed arithmetic type is unsigned.
        static constexpr bool IsUnsigned() noexcept { return std::is_unsigned_v<Self>; }

        /// @brief Returns whether the decayed type is trivial.
        static constexpr bool IsTrivial() noexcept { return std::is_trivial_v<Self>; }

        /// @brief Returns whether the decayed type is trivially copyable.
        static constexpr bool IsTriviallyCopyable() noexcept { return std::is_trivially_copyable_v<Self>; }

        /// @brief Returns whether the decayed type is trivially default-constructible.
        static constexpr bool IsTriviallyConstructible() noexcept { return std::is_trivially_constructible_v<Self>; }

        /// @brief Returns whether the decayed type has a trivial default constructor.
        static constexpr bool IsTriviallyDefaultConstructible() noexcept { return std::is_trivially_default_constructible_v<Self>; }

        /// @brief Returns whether the decayed type has a trivial destructor.
        static constexpr bool IsTriviallyDestructible() noexcept { return std::is_trivially_destructible_v<Self>; }

        /// @brief Returns whether the decayed type has a trivial move constructor.
        static constexpr bool IsTriviallyMoveConstructible() noexcept { return std::is_trivially_move_constructible_v<Self>; }

        /// @brief Returns whether the decayed type has a trivial copy constructor.
        static constexpr bool IsTriviallyCopyConstructible() noexcept { return std::is_trivially_copy_constructible_v<Self>; }

        /// @brief Returns whether the decayed type has a trivial move-assignment operator.
        static constexpr bool IsTriviallyMoveAssignable() noexcept { return std::is_trivially_move_assignable_v<Self>; }

        /// @brief Returns whether the decayed type has a trivial copy-assignment operator.
        static constexpr bool IsTriviallyCopyAssignable() noexcept { return std::is_trivially_copy_assignable_v<Self>; }

        /// @brief Returns whether the decayed type is copy-constructible.
        static constexpr bool IsCopyConstructible() noexcept { return std::is_copy_constructible_v<Self>; }

        /// @brief Returns whether the decayed type is move-constructible.
        static constexpr bool IsMoveConstructible() noexcept { return std::is_move_constructible_v<Self>; }

        /// @brief Returns whether the decayed type is copy-assignable.
        static constexpr bool IsCopyAssignable() noexcept { return std::is_copy_assignable_v<Self>; }

        /// @brief Returns whether the decayed type is move-assignable.
        static constexpr bool IsMoveAssignable() noexcept { return std::is_move_assignable_v<Self>; }

        /// @brief Returns whether the decayed type can be constructed from the supplied argument types.
        /// @tparam Args Constructor argument types.
        template<typename... Args>
        static constexpr bool IsConstructible() noexcept
        {
            return std::is_constructible_v<Self, Args...>;
        }

        /// @brief Returns whether construction from the supplied argument types is non-throwing.
        /// @tparam Args Constructor argument types.
        template<typename... Args>
        static constexpr bool IsNothrowConstructible() noexcept
        {
            return std::is_nothrow_constructible_v<Self, Args...>;
        }

        /// @brief Returns whether the decayed type is destructible.
        static constexpr bool IsDestructible() noexcept { return std::is_destructible_v<Self>; }

        /// @brief Returns whether default construction of the decayed type is non-throwing.
        static constexpr bool IsNothrowDefaultConstructible() noexcept { return std::is_nothrow_default_constructible_v<Self>; }

        /// @brief Returns whether move construction of the decayed type is non-throwing.
        static constexpr bool IsNothrowMoveConstructible() noexcept { return std::is_nothrow_move_constructible_v<Self>; }

        /// @brief Returns whether copy construction of the decayed type is non-throwing.
        static constexpr bool IsNothrowCopyConstructible() noexcept { return std::is_nothrow_copy_constructible_v<Self>; }

        /// @brief Returns whether move assignment of the decayed type is non-throwing.
        static constexpr bool IsNothrowMoveAssignable() noexcept { return std::is_nothrow_move_assignable_v<Self>; }

        /// @brief Returns whether copy assignment of the decayed type is non-throwing.
        static constexpr bool IsNothrowCopyAssignable() noexcept { return std::is_nothrow_copy_assignable_v<Self>; }

        /// @brief Returns whether destruction of the decayed type is non-throwing.
        static constexpr bool IsNothrowDestructible() noexcept { return std::is_nothrow_destructible_v<Self>; }

        /// @brief Returns whether values can be relocated with a bytewise copy.
        static constexpr bool IsBitwiseRelocatable() noexcept
        {
            return std::is_trivially_copyable_v<Self> && !std::is_volatile_v<Self>;
        }

        /// @brief Returns whether values can be relocated by moving and then destroying them.
        static constexpr bool IsMoveRelocatable() noexcept
        {
            return std::is_trivially_move_constructible_v<Self> &&
                   std::is_trivially_destructible_v<Self> &&
                   !std::is_volatile_v<Self>;
        }

        /// @brief Returns whether the decayed type is `void`.
        static constexpr bool IsVoid() noexcept { return std::is_void_v<Self>; }

        /// @brief Returns whether the decayed class type is empty.
        static constexpr bool IsEmpty() noexcept { return std::is_empty_v<Self>; }

        /// @brief Returns whether the decayed type has standard layout.
        static constexpr bool IsStandardLayout() noexcept { return std::is_standard_layout_v<Self>; }

        /// @brief Returns whether another type has the same decayed type as `T`.
        /// @tparam U Type to compare after removing its cv-ref qualifiers.
        template<typename U>
        static constexpr bool IsSame() noexcept
        {
            return std::is_same_v<Self, std::remove_cvref_t<U>>;
        }
    };


}// namespace NGIN::Meta
