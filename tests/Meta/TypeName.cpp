/// @file TypeName.cpp
/// @brief Tests for type name reflection (qualified/unqualified/namespace).

#include <NGIN/Meta/TypeName.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

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

TEST_CASE("TypeName reports qualified, unqualified, and namespace names", "[Meta][TypeName]")
{
    using Traits = NGIN::Meta::TypeName<TypeNameTestNS::Foo>;
    CHECK(Traits::qualifiedName == std::string_view {"TypeNameTestNS::Foo"});
    CHECK(Traits::unqualifiedName == std::string_view {"Foo"});
    CHECK(Traits::namespaceName == std::string_view {"TypeNameTestNS"});
}

TEST_CASE("TypeName handles nested namespaces", "[Meta][TypeName]")
{
    using Traits = NGIN::Meta::TypeName<TypeNameTestNS::Inner::Bar>;
    CHECK(Traits::qualifiedName == std::string_view {"TypeNameTestNS::Inner::Bar"});
    CHECK(Traits::unqualifiedName == std::string_view {"Bar"});
    CHECK(Traits::namespaceName == std::string_view {"TypeNameTestNS::Inner"});
}

TEST_CASE("TypeName handles global namespace", "[Meta][TypeName]")
{
    using Traits = NGIN::Meta::TypeName<TN_Global>;
    CHECK(Traits::qualifiedName == std::string_view {"TN_Global"});
    CHECK(Traits::unqualifiedName == std::string_view {"TN_Global"});
    CHECK(Traits::namespaceName.empty());
}

TEST_CASE("TypeName normalizes pointer and reference types", "[Meta][TypeName]")
{
    using PointerTraits = NGIN::Meta::TypeName<int*>;
    CHECK(PointerTraits::qualifiedName == std::string_view {"int*"});

    using ReferenceTraits = NGIN::Meta::TypeName<int&>;
    CHECK(ReferenceTraits::qualifiedName == std::string_view {"int"});
}

TEST_CASE("TypeName reports standard library types", "[Meta][TypeName]")
{
    using StringViewTraits = NGIN::Meta::TypeName<std::string_view>;
    CHECK(StringViewTraits::qualifiedName == std::string_view {"std::basic_string_view<char, std::char_traits<char>>"});
    CHECK(StringViewTraits::unqualifiedName == std::string_view {"basic_string_view<char, char_traits<char>>"});
    CHECK(StringViewTraits::namespaceName == std::string_view {"std"});

    using VectorTraits = NGIN::Meta::TypeName<std::vector<int>>;
    CHECK(VectorTraits::qualifiedName == std::string_view {"std::vector<int, std::allocator<int>>"});
    CHECK(VectorTraits::unqualifiedName == std::string_view {"vector<int, allocator<int>>"});
    CHECK(VectorTraits::namespaceName == std::string_view {"std"});
}
