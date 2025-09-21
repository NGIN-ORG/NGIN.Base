/// @file test_type_traits.cpp
/// @brief Unit tests for NGIN::Meta::TypeTraits

#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

// Define test types in different namespaces
namespace TestTypeTraits_Foo
{
    struct Bar
    {
    };
    class Baz
    {
    };
}// namespace TestTypeTraits_Foo

namespace TestTypeTraits_Foo::Nested
{
    struct Quux
    {
    };
}// namespace TestTypeTraits_Foo::Nested

struct TestTypeTraits_GlobalStruct
{
};

struct TestTypeTraits_VoidPointerType
{
};// not used below, but just an example

TEST_CASE("TypeTraits identifies const/pointer/reference/arithmetic", "[Meta][TypeTraits]")
{
    using ConstRefInt = NGIN::Meta::TypeTraits<const int&>;
    CHECK(ConstRefInt::IsConst());
    CHECK_FALSE(ConstRefInt::IsPointer());
    CHECK(ConstRefInt::IsReference());
    CHECK(ConstRefInt::IsArithmetic());

    using PointerDouble = NGIN::Meta::TypeTraits<double*>;
    CHECK_FALSE(PointerDouble::IsConst());
    CHECK(PointerDouble::IsPointer());
    CHECK_FALSE(PointerDouble::IsReference());
    CHECK_FALSE(PointerDouble::IsArithmetic());
}

TEST_CASE("TypeTraits identifies void/enum/volatile cases", "[Meta][TypeTraits]")
{
    using VoidType = NGIN::Meta::TypeTraits<void>;
    CHECK(VoidType::IsVoid());

    enum class MyEnum
    {
        A,
        B
    };
    using EnumType = NGIN::Meta::TypeTraits<MyEnum>;
    CHECK(EnumType::IsEnum());
    CHECK(EnumType::IsTriviallyCopyable());
    CHECK_FALSE(EnumType::IsClass());

    using VolatileFloat = NGIN::Meta::TypeTraits<volatile float>;
    CHECK(VolatileFloat::IsVolatile());
    CHECK(VolatileFloat::IsFloatingPoint());
    CHECK(VolatileFloat::IsArithmetic());
}
