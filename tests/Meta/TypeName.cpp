/// @file TypeName.cpp
/// @brief Tests for type name reflection (qualified/unqualified/namespace).

#include <NGIN/Meta/TypeName.hpp>
#include <boost/ut.hpp>
#include <string_view>
#include <vector>

using namespace boost::ut;

namespace TypeNameTestNS
{
    struct Foo
    {
    };
    namespace Inner
    {
        struct Bar
        {
        };
    }// namespace Inner
}// namespace TypeNameTestNS
struct TN_Global
{
};

suite<"TypeNameReflection"> typeNameReflection = [] {
    "qualified/unqualified/namespace simple"_test = [] {
        using Traits = NGIN::Meta::TypeName<TypeNameTestNS::Foo>;
        expect(eq(Traits::qualifiedName, std::string_view("TypeNameTestNS::Foo")));
        expect(eq(Traits::unqualifiedName, std::string_view("Foo")));
        expect(eq(Traits::namespaceName, std::string_view("TypeNameTestNS")));
    };

    "nested namespace"_test = [] {
        using Traits = NGIN::Meta::TypeName<TypeNameTestNS::Inner::Bar>;
        expect(eq(Traits::qualifiedName, std::string_view("TypeNameTestNS::Inner::Bar")));
        expect(eq(Traits::unqualifiedName, std::string_view("Bar")));
        expect(eq(Traits::namespaceName, std::string_view("TypeNameTestNS::Inner")));
    };

    "global namespace"_test = [] {
        using Traits = NGIN::Meta::TypeName<TN_Global>;
        expect(eq(Traits::qualifiedName, std::string_view("TN_Global")));
        expect(eq(Traits::unqualifiedName, std::string_view("TN_Global")));
        expect(eq(Traits::namespaceName, std::string_view("")));
    };

    "pointer and reference decay"_test = [] {
        using PtrTraits = NGIN::Meta::TypeName<int*>;
        expect(eq(PtrTraits::qualifiedName, std::string_view("int*")));
        using RefTraits = NGIN::Meta::TypeName<int&>;// reflected as base
        expect(eq(RefTraits::qualifiedName, std::string_view("int")));
    };

    "std::string_view"_test = [] {
        using Traits = NGIN::Meta::TypeName<std::string_view>;
        expect(eq(Traits::qualifiedName, std::string_view("std::basic_string_view<char, std::char_traits<char>>")));
        expect(eq(Traits::unqualifiedName, std::string_view("basic_string_view<char, char_traits<char>>")));
        expect(eq(Traits::namespaceName, std::string_view("std")));
    };

    "std::vector<int>"_test = [] {
        using Traits = NGIN::Meta::TypeName<std::vector<int>>;
        expect(eq(Traits::qualifiedName, std::string_view("std::vector<int, std::allocator<int>>")));
        expect(eq(Traits::unqualifiedName, std::string_view("vector<int, allocator<int>>")));
        expect(eq(Traits::namespaceName, std::string_view("std")));
    };
};
