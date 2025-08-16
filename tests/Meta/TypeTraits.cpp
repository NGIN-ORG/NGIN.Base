/// @file test_type_traits.cpp
/// @brief Unit tests for NGIN::Meta::TypeTraits

#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <boost/ut.hpp>
#include <string_view>
#include <vector>

using namespace boost::ut;

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

// Construct the test suite (now focusing on boolean trait flags only)
suite<"TypeTraitsTest"> typeTraitsTest = [] {
    // Fundamental trait flags
    "TypeTraitsBase: IsConst, IsPointer, etc."_test = [] {
        using T1 = NGIN::Meta::TypeTraits<const int&>;
        // const int&, after ignoring references, is const int
        expect(T1::IsConst()) << "const int& should set IsConst = true";
        expect(not T1::IsPointer()) << "const int& is not a pointer";
        expect(T1::IsReference()) << "const int& is a reference";
        expect(T1::IsArithmetic()) << "const int& is arithmetic (int)";

        using T2 = NGIN::Meta::TypeTraits<double*>;
        expect(not T2::IsConst()) << "double* is not const";
        expect(T2::IsPointer()) << "double* is a pointer";
        expect(not T2::IsReference()) << "double* is not a reference";
        expect(T2::IsArithmetic() == false)
                << "double* is not arithmetic, even though double is arithmetic";
    };

    // Another example to test e.g. IsVoid, IsEnum, etc.
    "TypeTraitsBase: IsVoid, IsEnum, IsTriviallyCopyable, etc."_test = [] {
        using T1 = NGIN::Meta::TypeTraits<void>;
        expect(T1::IsVoid()) << "void should set IsVoid = true";

        enum class MyEnum
        {
            A,
            B
        };
        using T2 = NGIN::Meta::TypeTraits<MyEnum>;
        expect(T2::IsEnum()) << "MyEnum should set IsEnum = true";
        expect(T2::IsTriviallyCopyable()) << "most enums are trivially copyable";
        expect(not T2::IsClass()) << "enums are not classes";

        using T3 = NGIN::Meta::TypeTraits<volatile float>;
        expect(T3::IsVolatile()) << "volatile float IsVolatile = true";
        expect(T3::IsFloatingPoint()) << "float is floating point";
        expect(T3::IsArithmetic()) << "float is arithmetic";
    };
};
