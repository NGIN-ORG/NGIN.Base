/// @file DynamicLibrary.cpp
/// @brief Focused tests for dynamic library loading primitives.

#include <NGIN/IO/DynamicLibrary.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#ifndef NGIN_BASE_TEST_DYNAMIC_LIBRARY_FILENAME
#error "NGIN_BASE_TEST_DYNAMIC_LIBRARY_FILENAME must be defined for DynamicLibrary tests."
#endif

namespace
{
    using FixtureFunction = int (*)();
    using FixtureData     = int* (*) ();

    [[nodiscard]] NGIN::IO::Path GetFixtureLibraryPath()
    {
        return NGIN::IO::Path(std::string("./") + NGIN_BASE_TEST_DYNAMIC_LIBRARY_FILENAME);
    }
}// namespace

TEST_CASE("IO.DynamicLibrary rejects an empty path", "[IO][DynamicLibrary]")
{
    CHECK_THROWS_AS(NGIN::IO::DynamicLibrary(""), std::invalid_argument);
}

TEST_CASE("IO.DynamicLibrary reports missing libraries", "[IO][DynamicLibrary]")
{
    CHECK_THROWS_AS(NGIN::IO::DynamicLibrary("__ngin_base_missing_library__"), NGIN::IO::DynamicLibraryError);
}

TEST_CASE("IO.DynamicLibrary supports deferred loading when explicitly requested", "[IO][DynamicLibrary]")
{
    NGIN::IO::DynamicLibrary library(GetFixtureLibraryPath(), NGIN::IO::DynamicLibrary::LoadMode::Lazy);
    CHECK_FALSE(library.IsLoaded());

    library.Load();
    CHECK(library.IsLoaded());
}

TEST_CASE("IO.DynamicLibrary loads and resolves required symbols", "[IO][DynamicLibrary]")
{
    NGIN::IO::DynamicLibrary library = NGIN::IO::DynamicLibrary::Open(GetFixtureLibraryPath());

    REQUIRE(library.IsLoaded());
    CHECK(library.GetPath().View() == GetFixtureLibraryPath().View());

    const auto getValue = library.Resolve<FixtureFunction>("NginBaseDynamicLibraryFixtureValue");
    REQUIRE(getValue != nullptr);
    CHECK(getValue() == 42);

    const auto getData = library.Resolve<FixtureData>("NginBaseDynamicLibraryFixtureData");
    REQUIRE(getData != nullptr);
    CHECK(*getData() == 7);
}

TEST_CASE("IO.DynamicLibrary TryResolve supports optional symbols", "[IO][DynamicLibrary]")
{
    NGIN::IO::DynamicLibrary library = NGIN::IO::DynamicLibrary::Open(GetFixtureLibraryPath());

    const auto present = library.TryResolve<FixtureFunction>("NginBaseDynamicLibraryFixtureValue");
    REQUIRE(present.has_value());
    CHECK((*present)() == 42);

    const auto missing = library.TryResolve<FixtureFunction>("NginBaseDynamicLibraryFixtureMissing");
    CHECK_FALSE(missing.has_value());
}

TEST_CASE("IO.DynamicLibrary move semantics transfer ownership", "[IO][DynamicLibrary]")
{
    NGIN::IO::DynamicLibrary library = NGIN::IO::DynamicLibrary::Open(GetFixtureLibraryPath());
    REQUIRE(library.IsLoaded());

    NGIN::IO::DynamicLibrary moved(std::move(library));
    CHECK_FALSE(library.IsLoaded());
    REQUIRE(moved.IsLoaded());

    const auto getValue = moved.Resolve<FixtureFunction>("NginBaseDynamicLibraryFixtureValue");
    REQUIRE(getValue != nullptr);
    CHECK(getValue() == 42);

    NGIN::IO::DynamicLibrary assigned("__ngin_base_other_missing_library__", NGIN::IO::DynamicLibrary::LoadMode::Lazy);
    assigned = std::move(moved);
    CHECK_FALSE(moved.IsLoaded());
    REQUIRE(assigned.IsLoaded());

    const auto getData = assigned.Resolve<FixtureData>("NginBaseDynamicLibraryFixtureData");
    REQUIRE(getData != nullptr);
    CHECK(*getData() == 7);
}

TEST_CASE("IO.DynamicLibrary Unload is idempotent and invalidates the loaded state", "[IO][DynamicLibrary]")
{
    NGIN::IO::DynamicLibrary library = NGIN::IO::DynamicLibrary::Open(GetFixtureLibraryPath());
    REQUIRE(library.IsLoaded());

    library.Unload();
    CHECK_FALSE(library.IsLoaded());
    CHECK_THROWS_AS(library.Resolve<FixtureFunction>("NginBaseDynamicLibraryFixtureValue"),
                    NGIN::IO::DynamicLibraryError);

    CHECK_NOTHROW(library.Unload());
}
