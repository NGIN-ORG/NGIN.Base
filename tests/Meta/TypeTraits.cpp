/// @file test_type_traits.cpp
/// @brief Unit tests for NGIN::Meta::TypeTraits

#include <NGIN/Meta/TypeTraits.hpp>
#include <boost/ut.hpp>
#include <string_view>
#include <vector>

using namespace boost::ut;

// Define test types in different namespaces
namespace TestTypeTraits_Foo
{
    struct Bar {};
    class  Baz {};
} // namespace TestTypeTraits_Foo

namespace TestTypeTraits_Foo::Nested
{
    struct Quux {};
} // namespace TestTypeTraits_Foo::Nested

struct TestTypeTraits_GlobalStruct {};

struct TestTypeTraits_VoidPointerType {}; // not used below, but just an example

// Construct the test suite
suite<"TypeTraitsTest"> typeTraitsTest = [] {
    // -----------------------------------------------------------------------------
    // Existing Tests for Namespaces / Classes
    // -----------------------------------------------------------------------------
    "TestTypeTraits_Foo::Bar TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<TestTypeTraits_Foo::Bar>;
        expect(eq(Traits::qualifiedName,   std::string_view("TestTypeTraits_Foo::Bar"))) 
            << "qualifiedName for TestTypeTraits_Foo::Bar should be 'TestTypeTraits_Foo::Bar'";
        expect(eq(Traits::unqualifiedName, std::string_view("Bar"))) 
            << "unqualifiedName for TestTypeTraits_Foo::Bar should be 'Bar'";
        expect(eq(Traits::namespaceName,   std::string_view("TestTypeTraits_Foo"))) 
            << "namespaceName for TestTypeTraits_Foo::Bar should be 'TestTypeTraits_Foo'";
    };

    "TestTypeTraits_Foo::Baz TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<TestTypeTraits_Foo::Baz>;
        expect(eq(Traits::qualifiedName,   std::string_view("TestTypeTraits_Foo::Baz")))
            << "qualifiedName for TestTypeTraits_Foo::Baz should be 'TestTypeTraits_Foo::Baz'";
        expect(eq(Traits::unqualifiedName, std::string_view("Baz")))
            << "unqualifiedName for TestTypeTraits_Foo::Baz should be 'Baz'";
        expect(eq(Traits::namespaceName,   std::string_view("TestTypeTraits_Foo")))
            << "namespaceName for TestTypeTraits_Foo::Baz should be 'TestTypeTraits_Foo'";
    };

    "TestTypeTraits_Foo::Nested::Quux TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<TestTypeTraits_Foo::Nested::Quux>;
        expect(eq(Traits::qualifiedName,   std::string_view("TestTypeTraits_Foo::Nested::Quux")))
            << "qualifiedName for TestTypeTraits_Foo::Nested::Quux should be 'TestTypeTraits_Foo::Nested::Quux'";
        expect(eq(Traits::unqualifiedName, std::string_view("Quux")))
            << "unqualifiedName for TestTypeTraits_Foo::Nested::Quux should be 'Quux'";
        expect(eq(Traits::namespaceName,   std::string_view("TestTypeTraits_Foo::Nested")))
            << "namespaceName for TestTypeTraits_Foo::Nested::Quux should be 'TestTypeTraits_Foo::Nested'";
    };

    "TestTypeTraits_GlobalStruct TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<TestTypeTraits_GlobalStruct>;
        expect(eq(Traits::qualifiedName,   std::string_view("TestTypeTraits_GlobalStruct")))
            << "qualifiedName for TestTypeTraits_GlobalStruct should be 'TestTypeTraits_GlobalStruct'";
        expect(eq(Traits::unqualifiedName, std::string_view("TestTypeTraits_GlobalStruct")))
            << "unqualifiedName for TestTypeTraits_GlobalStruct should be 'TestTypeTraits_GlobalStruct'";
        expect(eq(Traits::namespaceName,   std::string_view("")))
            << "namespaceName for TestTypeTraits_GlobalStruct should be empty";
    };

    // -----------------------------------------------------------------------------
    // Primitive, pointer, reference
    // -----------------------------------------------------------------------------
    "int TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<int>;
        expect(eq(Traits::qualifiedName,   std::string_view("int")))
            << "qualifiedName for int should be 'int'";
        expect(eq(Traits::unqualifiedName, std::string_view("int")))
            << "unqualifiedName for int should be 'int'";
        expect(eq(Traits::namespaceName,   std::string_view("")))
            << "namespaceName for int should be empty";
    };

    // If your reflection code is removing pointers from the type name, this test will fail.
    // You either need to STOP removing pointers from T in TypeTraits, or do a post-process fix.
    "void* TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<void*>;
        // If your code returns "void" instead of "void*", fix either in TypeTraits or do:
        auto realQN = Traits::qualifiedName;
        // If we REALLY want "void*", let's just expect that:
        expect(eq(realQN, std::string_view("void*")))
            << "qualifiedName for void* should be 'void*' (or fix reflection logic)";
        expect(eq(Traits::unqualifiedName, std::string_view("void*")))
            << "unqualifiedName for void* should be 'void*'";
        expect(eq(Traits::namespaceName,   std::string_view("")))
            << "namespaceName for void* should be empty";
    };

    // -----------------------------------------------------------------------------
    // Standard library: string_view
    // -----------------------------------------------------------------------------
    // By default, it's likely to come out as "std::basic_string_view<char, std::char_traits<char>>"
    "std::string_view TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<std::string_view>;
        auto realQN  = Traits::qualifiedName;
        auto realUNQ = Traits::unqualifiedName;

        expect(eq(realQN,  std::string_view("std::basic_string_view<char, std::char_traits<char>>")))
            << "qualifiedName for std::string_view should be 'std::basic_string_view<char, std::char_traits<char>>'";
        expect(eq(realUNQ, std::string_view("basic_string_view<char, char_traits<char>>")))
            << "unqualifiedName for std::string_view should be 'basic_string_view<char, char_traits<char>>'";
        expect(eq(Traits::namespaceName, std::string_view("std")))
            << "namespaceName for std::string_view should be 'std'";
    };

    // -----------------------------------------------------------------------------
    // References
    // -----------------------------------------------------------------------------
    "int& TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<int&>;
        // reflection might remove references, so we get "int"
        expect(eq(Traits::qualifiedName,   std::string_view("int")))
            << "qualifiedName for int& should be 'int' after decaying";
        expect(eq(Traits::unqualifiedName, std::string_view("int")))
            << "unqualifiedName for int& should be 'int'";
        expect(eq(Traits::namespaceName,   std::string_view("")))
            << "namespaceName for int& should be empty";
    };

    "const int& TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<const int&>;
        expect(eq(Traits::qualifiedName,   std::string_view("int")))
            << "qualifiedName for const int& should be 'int' after decaying";
        expect(eq(Traits::unqualifiedName, std::string_view("int")))
            << "unqualifiedName for const int& should be 'int'";
        expect(eq(Traits::namespaceName,   std::string_view("")))
            << "namespaceName for const int& should be empty";
    };

    // -----------------------------------------------------------------------------
    // std::vector<int>
    // -----------------------------------------------------------------------------
    // Likely to come out as "std::vector<int, std::allocator<int>>"
    "std::vector<int> TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<std::vector<int>>;
        auto realQN  = Traits::qualifiedName;
        auto realUNQ = Traits::unqualifiedName;

        expect(eq(realQN,  std::string_view("std::vector<int, std::allocator<int>>")))
            << "qualifiedName for std::vector<int> should be 'std::vector<int, std::allocator<int>>'";
        expect(eq(realUNQ, std::string_view("vector<int, allocator<int>>")))
            << "unqualifiedName for std::vector<int> should be 'vector<int, allocator<int>>'";
        expect(eq(Traits::namespaceName,   std::string_view("std")))
            << "namespaceName for std::vector<int> should be 'std'";
    };

    // -----------------------------------------------------------------------------
    // double*
    // -----------------------------------------------------------------------------
    "double* TypeTraits"_test = [] {
        using Traits = NGIN::Meta::TypeTraits<double*>;
        auto realQN  = Traits::qualifiedName;
        expect(eq(realQN, std::string_view("double*")))
            << "qualifiedName for double* should be 'double*' (or fix reflection logic)";
        expect(eq(Traits::unqualifiedName, std::string_view("double*")))
            << "unqualifiedName for double* should be 'double*'";
        expect(eq(Traits::namespaceName,   std::string_view("")))
            << "namespaceName for double* should be empty";
    };

    // -----------------------------------------------------------------------------
    //  New Tests for isConst, isPointer, etc. from TypeTraitsBase
    // -----------------------------------------------------------------------------
    "TypeTraitsBase: isConst, isPointer, etc."_test = [] {
        using T1 = NGIN::Meta::TypeTraits<const int&>; 
        // const int&, after ignoring references, is const int
        expect(T1::isConst)         << "const int& should set isConst = true";
        expect(not T1::isPointer)   << "const int& is not a pointer";
        expect(T1::isReference)     << "const int& is a reference";
        expect(T1::isArithmetic)    << "const int& is arithmetic (int)";

        using T2 = NGIN::Meta::TypeTraits<double*>;
        expect(not T2::isConst)     << "double* is not const";
        expect(T2::isPointer)       << "double* is a pointer";
        expect(not T2::isReference) << "double* is not a reference";
        expect(T2::isArithmetic == false) 
            << "double* is not arithmetic, even though double is arithmetic";
    };

    // Another example to test e.g. isVoid, isEnum, etc.
    "TypeTraitsBase: isVoid, isEnum, isTriviallyCopyable, etc."_test = [] {
        using T1 = NGIN::Meta::TypeTraits<void>;
        expect(T1::isVoid) << "void should set isVoid = true";

        enum class MyEnum { A, B };
        using T2 = NGIN::Meta::TypeTraits<MyEnum>;
        expect(T2::isEnum) << "MyEnum should set isEnum = true";
        expect(T2::isTriviallyCopyable) << "most enums are trivially copyable";
        expect(not T2::isClass) << "enums are not classes";

        using T3 = NGIN::Meta::TypeTraits<volatile float>;
        expect(T3::isVolatile) << "volatile float isVolatile = true";
        expect(T3::isFloatingPoint) << "float is floating point";
        expect(T3::isArithmetic)    << "float is arithmetic";
    };
};
