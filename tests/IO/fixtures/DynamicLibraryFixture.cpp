/// @file DynamicLibraryFixture.cpp
/// @brief Test shared library used by the DynamicLibrary unit tests.

#if defined(_WIN32)
#define NGIN_BASE_DYNAMIC_LIBRARY_FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define NGIN_BASE_DYNAMIC_LIBRARY_FIXTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

NGIN_BASE_DYNAMIC_LIBRARY_FIXTURE_EXPORT int NginBaseDynamicLibraryFixtureValue()
{
    return 42;
}

NGIN_BASE_DYNAMIC_LIBRARY_FIXTURE_EXPORT int* NginBaseDynamicLibraryFixtureData()
{
    static int value = 7;
    return &value;
}
