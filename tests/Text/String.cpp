/// @file String.cpp
/// @brief Tests for NGIN::Text::BasicString and NGIN::Text::String.

#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Text/String.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

using NGIN::Text::String;

namespace
{
    bool CStrEqual(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
            return left == right;
        return std::strcmp(left, right) == 0;
    }

    struct SwapAllocator
    {
        NGIN::Memory::SystemAllocator inner {};
        int                           id {0};

        explicit SwapAllocator(int value = 0) : id(value) {}

        void* Allocate(std::size_t bytes, std::size_t align) noexcept
        {
            return inner.Allocate(bytes, align);
        }

        void Deallocate(void* ptr, std::size_t bytes, std::size_t align) noexcept
        {
            inner.Deallocate(ptr, bytes, align);
        }

        int Id() const noexcept { return id; }
    };

    struct CopyPropAllocator
    {
        NGIN::Memory::SystemAllocator inner {};
        int                           id {0};

        explicit CopyPropAllocator(int value = 0) : id(value) {}

        void* Allocate(std::size_t bytes, std::size_t align) noexcept
        {
            return inner.Allocate(bytes, align);
        }

        void Deallocate(void* ptr, std::size_t bytes, std::size_t align) noexcept
        {
            inner.Deallocate(ptr, bytes, align);
        }

        int Id() const noexcept { return id; }
    };

    struct AllocationLog
    {
        std::size_t lastAllocatedBytes {0};
        std::size_t lastDeallocatedBytes {0};
        std::size_t allocationCount {0};
        std::size_t deallocationCount {0};
    };

    struct RecordingAllocator
    {
        NGIN::Memory::SystemAllocator inner {};
        AllocationLog*                log {nullptr};
        int                           id {0};

        RecordingAllocator() = default;
        RecordingAllocator(AllocationLog& stats, int value = 0) : log(&stats), id(value) {}

        void* Allocate(std::size_t bytes, std::size_t align) noexcept
        {
            void* const memory = inner.Allocate(bytes, align);
            if (memory != nullptr && log != nullptr)
            {
                log->lastAllocatedBytes = bytes;
                log->allocationCount += 1;
            }
            return memory;
        }

        void Deallocate(void* ptr, std::size_t bytes, std::size_t align) noexcept
        {
            if (ptr != nullptr && log != nullptr)
            {
                log->lastDeallocatedBytes = bytes;
                log->deallocationCount += 1;
            }
            inner.Deallocate(ptr, bytes, align);
        }

        int Id() const noexcept { return id; }
    };

    struct TrackingRef
    {
        NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>* tracker {nullptr};

        TrackingRef() = default;
        explicit TrackingRef(NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>& ref)
            : tracker(&ref)
        {
        }

        void* Allocate(std::size_t bytes, std::size_t align) noexcept
        {
            return tracker->Allocate(bytes, align);
        }

        void Deallocate(void* ptr, std::size_t bytes, std::size_t align) noexcept
        {
            tracker->Deallocate(ptr, bytes, align);
        }
    };

    struct FoldTraits : std::char_traits<char>
    {
        static char Lower(char value) noexcept
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        }

        static int compare(const char* left, const char* right, std::size_t count) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                const char lhs = Lower(left[i]);
                const char rhs = Lower(right[i]);
                if (lhs < rhs)
                    return -1;
                if (lhs > rhs)
                    return 1;
            }
            return 0;
        }
    };
}// namespace

namespace NGIN::Memory
{
    template<>
    struct AllocatorPropagationTraits<SwapAllocator>
    {
        static constexpr bool PropagateOnCopyAssignment = false;
        static constexpr bool PropagateOnMoveAssignment = false;
        static constexpr bool PropagateOnSwap           = false;
        static constexpr bool IsAlwaysEqual             = false;
    };

    template<>
    struct AllocatorPropagationTraits<CopyPropAllocator>
    {
        static constexpr bool PropagateOnCopyAssignment = true;
        static constexpr bool PropagateOnMoveAssignment = false;
        static constexpr bool PropagateOnSwap           = false;
        static constexpr bool IsAlwaysEqual             = false;
    };

    template<>
    struct AllocatorPropagationTraits<RecordingAllocator>
    {
        static constexpr bool PropagateOnCopyAssignment = false;
        static constexpr bool PropagateOnMoveAssignment = false;
        static constexpr bool PropagateOnSwap           = false;
        static constexpr bool IsAlwaysEqual             = false;
    };
}// namespace NGIN::Memory

TEST_CASE("String default construction", "[Text][String]")
{
    String value;
    CHECK(value.GetSize() == 0U);
    CHECK(value.Empty());
    CHECK(CStrEqual(value.CStr(), ""));
    CHECK(value.View().empty());
}

TEST_CASE("String handles null pointer construction", "[Text][String]")
{
    const char* nullString = nullptr;
    String      value(nullString);
    CHECK(value.GetSize() == 0U);
    CHECK(CStrEqual(value.CStr(), ""));
}

TEST_CASE("String constructs from small and large C strings", "[Text][String]")
{
    SECTION("small")
    {
        String value("Hello");
        CHECK(value.GetSize() == 5U);
        CHECK(value.View() == std::string_view("Hello"));
    }

    SECTION("large")
    {
        std::string large(60, 'A');
        String      value(large.c_str());
        CHECK(value.GetSize() == large.size());
        CHECK(CStrEqual(value.CStr(), large.c_str()));
    }
}

TEST_CASE("String copy constructor preserves content", "[Text][String]")
{
    SECTION("small buffer")
    {
        String original("Small Test");
        String copy(original);
        CHECK(copy.GetSize() == original.GetSize());
        CHECK(copy.View() == original.View());
    }

    SECTION("heap storage")
    {
        std::string large(70, 'B');
        String      original(large.c_str());
        String      copy(original);
        CHECK(copy.GetSize() == original.GetSize());
        CHECK(CStrEqual(copy.CStr(), original.CStr()));
        CHECK(copy.CStr() != original.CStr());
    }
}

TEST_CASE("String move constructor leaves source valid and empty", "[Text][String]")
{
    SECTION("small buffer")
    {
        String      source("MoveSmall");
        const char* oldPointer = source.CStr();
        String      moved(std::move(source));
        CHECK(moved.GetSize() == 9U);
        CHECK(CStrEqual(moved.CStr(), "MoveSmall"));
        CHECK(oldPointer != moved.CStr());
        CHECK(source.Empty());
        CHECK(CStrEqual(source.CStr(), ""));
    }

    SECTION("heap storage")
    {
        std::string large(70, 'M');
        String      source(large.c_str());
        const char* oldPointer = source.CStr();
        String      moved(std::move(source));
        CHECK(moved.GetSize() == large.size());
        CHECK(CStrEqual(moved.CStr(), large.c_str()));
        CHECK(oldPointer == moved.CStr());
        CHECK(source.Empty());
        CHECK(CStrEqual(source.CStr(), ""));
    }
}

TEST_CASE("String copy assignment preserves content", "[Text][String]")
{
    SECTION("small to small")
    {
        String left("Alpha");
        String right("Beta");
        right = left;
        CHECK(right.View() == std::string_view("Alpha"));
    }

    SECTION("heap to heap")
    {
        std::string largeA(80, 'A');
        std::string largeB(90, 'B');
        String      left(largeA.c_str());
        String      right(largeB.c_str());
        right = left;
        CHECK(right.GetSize() == left.GetSize());
        CHECK(CStrEqual(right.CStr(), largeA.c_str()));
        CHECK(right.CStr() != left.CStr());
    }
}

TEST_CASE("String move assignment leaves source valid and empty", "[Text][String]")
{
    SECTION("small buffer")
    {
        String      source("Hello");
        String      target("World");
        const char* oldPointer = source.CStr();
        target                 = std::move(source);
        CHECK(target.GetSize() == 5U);
        CHECK(CStrEqual(target.CStr(), "Hello"));
        CHECK(oldPointer != target.CStr());
        CHECK(source.Empty());
        CHECK(CStrEqual(source.CStr(), ""));
    }

    SECTION("heap storage")
    {
        std::string large(75, 'Z');
        String      source(large.c_str());
        String      target("Small");
        const char* oldPointer = source.CStr();
        target                 = std::move(source);
        CHECK(target.GetSize() == large.size());
        CHECK(CStrEqual(target.CStr(), large.c_str()));
        CHECK(target.CStr() == oldPointer);
        CHECK(source.Empty());
        CHECK(CStrEqual(source.CStr(), ""));
    }
}

TEST_CASE("String non-propagating move assignment deep copies for unequal allocators", "[Text][String]")
{
    using MoveStr = NGIN::Text::BasicString<char, 16, SwapAllocator>;

    std::string large(80, 'Q');
    MoveStr     source(large.c_str(), SwapAllocator {1});
    MoveStr     target("small", SwapAllocator {2});
    const char* oldPointer = source.CStr();

    target = std::move(source);

    CHECK(target.View() == std::string_view(large));
    CHECK(target.CStr() != oldPointer);
    CHECK(target.GetAllocator().Id() == 2);
    CHECK(source.Empty());
    CHECK(CStrEqual(source.CStr(), ""));
}

TEST_CASE("String copy assignment can propagate allocator on copy", "[Text][String]")
{
    using CopyStr = NGIN::Text::BasicString<char, 16, CopyPropAllocator>;

    std::string largeLeft(80, 'L');
    std::string largeRight(72, 'R');
    CopyStr     left(largeLeft.c_str(), CopyPropAllocator {1});
    CopyStr     right(largeRight.c_str(), CopyPropAllocator {2});

    left = right;

    CHECK(left.View() == std::string_view(largeRight));
    CHECK(left.GetAllocator().Id() == 2);
}

TEST_CASE("String append operations", "[Text][String]")
{
    SECTION("SBO append")
    {
        String first("Hello");
        String second("World");
        first.Append(second);
        CHECK(first.GetSize() == 10U);
        CHECK(CStrEqual(first.CStr(), "HelloWorld"));
    }

    SECTION("append triggers heap")
    {
        String      prefix("SBO start: ");
        std::string large(60, 'X');
        String      suffix(large.c_str());
        prefix.Append(suffix);
        std::string expected = std::string("SBO start: ") + large;
        CHECK(prefix.GetSize() == expected.size());
        CHECK(prefix.View() == expected);
    }

    SECTION("operator +=")
    {
        String value("Test");
        String suffix("++");
        value += suffix;
        CHECK(value.GetSize() == 6U);
        CHECK(CStrEqual(value.CStr(), "Test++"));
    }

    SECTION("append repeated character")
    {
        String value("ab");
        value.Append(3, 'x');
        CHECK(value.View() == std::string_view("abxxx"));
    }
}

TEST_CASE("String search APIs cover forward and reverse semantics", "[Text][String]")
{
    String value("bananaban");

    SECTION("Find and RFind")
    {
        CHECK(value.Find('b') == 0U);
        CHECK(value.Find("ana") == 1U);
        CHECK(value.Find("ana", 2) == 3U);
        CHECK(value.Find("xyz") == String::npos);
        CHECK(value.Find(String::view_type {}) == 0U);
        CHECK(value.Find(String::view_type {}, value.Size() + 4) == value.Size());

        CHECK(value.RFind('b') == 6U);
        CHECK(value.RFind("ana") == 3U);
        CHECK(value.RFind("ban", 4) == 0U);
        CHECK(value.RFind("xyz") == String::npos);
        CHECK(value.RFind(String::view_type {}) == value.Size());
        CHECK(value.RFind(String::view_type {}, 4) == 4U);
    }

    SECTION("FindFirstOf and FindLastOf")
    {
        CHECK(value.FindFirstOf('n') == 2U);
        CHECK(value.FindFirstOf("xybn") == 0U);
        CHECK(value.FindFirstOf("xyz", 1) == String::npos);

        CHECK(value.FindLastOf('n') == 8U);
        CHECK(value.FindLastOf("xybn") == 8U);
        CHECK(value.FindLastOf("xyz") == String::npos);
        CHECK(value.FindFirstOf(String::view_type {}) == String::npos);
        CHECK(value.FindLastOf(String::view_type {}) == String::npos);
    }

    SECTION("FindFirstNotOf and FindLastNotOf")
    {
        String padded("   alpha  ");

        CHECK(padded.FindFirstNotOf(' ') == 3U);
        CHECK(padded.FindFirstNotOf(" alh") == 5U);
        CHECK(padded.FindFirstNotOf(String::view_type {}, 4) == 4U);
        CHECK(padded.FindFirstNotOf(" alpha") == String::npos);

        CHECK(padded.FindLastNotOf(' ') == 7U);
        CHECK(padded.FindLastNotOf(" alh") == 5U);
        CHECK(padded.FindLastNotOf(String::view_type {}) == padded.Size() - 1);
        CHECK(padded.FindLastNotOf(" alpha") == String::npos);
    }
}

TEST_CASE("String search supports heap-backed strings", "[Text][String]")
{
    std::string large = std::string(60, 'a') + "bc" + std::string(20, 'a') + "bc";
    String      value(large.c_str());

    CHECK(value.Find("bc") == 60U);
    CHECK(value.RFind("bc") == 82U);
    CHECK(value.FindFirstOf("cb", 61) == 61U);
    CHECK(value.FindLastOf("cb") == 83U);
    CHECK(value.FindFirstNotOf("a", 60) == 60U);
    CHECK(value.FindLastNotOf("abc") == String::npos);
}

TEST_CASE("String Substr returns bounded slices", "[Text][String]")
{
    String value("alphabet");

    CHECK(value.Substr(0, 5).View() == std::string_view("alpha"));
    CHECK(value.Substr(5).View() == std::string_view("bet"));
    CHECK(value.Substr(value.Size()).Empty());
    CHECK_THROWS_AS(value.Substr(value.Size() + 1), std::out_of_range);
}

TEST_CASE("String Insert supports front middle and back insertion", "[Text][String]")
{
    SECTION("front and back")
    {
        String value("beta");
        value.Insert(0, "alpha ");
        value.Insert(value.Size(), " gamma");
        CHECK(value.View() == std::string_view("alpha beta gamma"));
    }

    SECTION("middle repeated char")
    {
        String value("abef");
        value.Insert(2, 2, 'c');
        CHECK(value.View() == std::string_view("abccef"));
    }
}

TEST_CASE("String Erase removes ranges", "[Text][String]")
{
    SECTION("front")
    {
        String value("prefix-body");
        value.Erase(0, 7);
        CHECK(value.View() == std::string_view("body"));
    }

    SECTION("middle")
    {
        String value("alpha-beta-gamma");
        value.Erase(5, 5);
        CHECK(value.View() == std::string_view("alpha-gamma"));
    }

    SECTION("to end")
    {
        String value("alphabet");
        value.Erase(5);
        CHECK(value.View() == std::string_view("alpha"));
    }
}

TEST_CASE("String Replace handles shorter longer and overlapping content", "[Text][String]")
{
    SECTION("shorter replacement")
    {
        String value("alpha-beta");
        value.Replace(5, 5, "-x");
        CHECK(value.View() == std::string_view("alpha-x"));
    }

    SECTION("longer replacement")
    {
        String value("alpha-x");
        value.Replace(5, 2, "-beta-gamma");
        CHECK(value.View() == std::string_view("alpha-beta-gamma"));
    }

    SECTION("self-overlap replacement")
    {
        String            value("abcdef");
        String::view_type view = value.View();
        value.Replace(2, 3, view.substr(0, 4));
        CHECK(value.View() == std::string_view("ababcdf"));
    }
}

TEST_CASE("String RemovePrefix and RemoveSuffix trim bounded ranges", "[Text][String]")
{
    SECTION("prefix")
    {
        String value("alpha-beta");
        value.RemovePrefix(6);
        CHECK(value.View() == std::string_view("beta"));
    }

    SECTION("suffix")
    {
        String value("alpha-beta");
        value.RemoveSuffix(5);
        CHECK(value.View() == std::string_view("alpha"));
    }

    SECTION("remove all")
    {
        String value("abc");
        value.RemovePrefix(3);
        CHECK(value.Empty());
    }

    SECTION("out of range")
    {
        String value("abc");
        CHECK_THROWS_AS(value.RemovePrefix(4), std::out_of_range);
        CHECK_THROWS_AS(value.RemoveSuffix(4), std::out_of_range);
    }
}

TEST_CASE("String self assignment is safe", "[Text][String]")
{
    SECTION("operator=")
    {
        String value("Self");
        value = value;
        CHECK(value.View() == std::string_view("Self"));
    }

    SECTION("operator+=")
    {
        String value("Repeat");
        value += value;
        CHECK(value.View() == std::string_view("RepeatRepeat"));
    }
}

TEST_CASE("String clear and assignment", "[Text][String]")
{
    String value("Clear me");
    value.Clear();
    CHECK(value.GetSize() == 0U);
    CHECK(CStrEqual(value.CStr(), ""));

    value.Assign("Assigned");
    CHECK(value.GetSize() == 8U);
    CHECK(CStrEqual(value.CStr(), "Assigned"));
}

TEST_CASE("String assign handles overlapping views", "[Text][String]")
{
    SECTION("small to small overlap")
    {
        String            value("abcdef");
        String::view_type view = value.View();
        value.Assign(view.substr(2, 3));
        CHECK(value.View() == std::string_view("cde"));
    }

    SECTION("heap in-place overlap")
    {
        String            value(std::string(80, 'a').c_str());
        String::view_type view = value.View();
        value.Assign(view.substr(10, 40));
        CHECK(value.GetSize() == 40U);
        CHECK(value.View() == std::string_view(std::string(40, 'a')));
    }
}

TEST_CASE("String append handles overlapping views", "[Text][String]")
{
    SECTION("small overlap")
    {
        String            value("abcdef");
        String::view_type view = value.View();
        value.Append(view.substr(1, 4));
        CHECK(value.View() == std::string_view("abcdefbcde"));
    }

    SECTION("heap overlap with reallocation")
    {
        std::string       base = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        String            value(base.c_str());
        String::view_type view = value.View();
        value.Append(view.substr(5, 20));
        CHECK(value.View() == std::string_view(base + base.substr(5, 20)));
    }

    SECTION("heap overlap without reallocation")
    {
        String value("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        value.Reserve(128);
        String::view_type view = value.View();
        value.Append(view.substr(3, 10));
        CHECK(value.EndsWith(String::view_type {"defghijklm", 10}));
    }
}

TEST_CASE("String shrink-to-fit can release heap storage back to SBO", "[Text][String]")
{
    using Tracked  = NGIN::Memory::Tracking<NGIN::Memory::SystemAllocator>;
    using SmallStr = NGIN::Text::BasicString<char, 16, TrackingRef>;

    Tracked     tracking {NGIN::Memory::SystemAllocator {}};
    TrackingRef alloc(tracking);

    std::string large(40, 'x');
    SmallStr    value(large.c_str(), alloc);
    CHECK(tracking.GetStats().currentBytes > 0);

    value.Resize(8);
    value.ShrinkToFit();
    CHECK(tracking.GetStats().currentBytes == 0);
    CHECK(value.View() == std::string_view("xxxxxxxx"));
}

TEST_CASE("String swap respects allocator propagation traits", "[Text][String]")
{
    using SwapStr = NGIN::Text::BasicString<char, 32, SwapAllocator>;

    SwapStr left("Left", SwapAllocator {1});
    SwapStr right("Right", SwapAllocator {2});

    left.Swap(right);

    CHECK(CStrEqual(left.CStr(), "Right"));
    CHECK(CStrEqual(right.CStr(), "Left"));
    CHECK(left.GetAllocator().Id() == 1);
    CHECK(right.GetAllocator().Id() == 2);
}

TEST_CASE("String View and At provide bounded access", "[Text][String]")
{
    String value("hello");

    CHECK(value.View() == std::string_view("hello"));
    CHECK(value.At(1) == 'e');

    value.At(1) = 'a';
    CHECK(value.View() == std::string_view("hallo"));
    CHECK_THROWS_AS(value.At(10), std::out_of_range);
}

TEST_CASE("String handles zero-length operations consistently", "[Text][String]")
{
    String      value("alpha");
    const char* oldData = value.CStr();

    value.Append(String::view_type {});
    CHECK(value.View() == std::string_view("alpha"));
    CHECK(value.CStr() == oldData);

    value.Assign(String::view_type {});
    CHECK(value.Empty());
    CHECK(CStrEqual(value.CStr(), ""));

    value.Resize(0);
    CHECK(value.Empty());
}

TEST_CASE("String deallocates with matching byte counts", "[Text][String]")
{
    using LoggedStr = NGIN::Text::BasicString<char, 16, RecordingAllocator>;

    AllocationLog log {};
    {
        LoggedStr         value(std::string(40, 'q').c_str(), RecordingAllocator {log, 7});
        const std::size_t allocatedBytes = log.lastAllocatedBytes;
        REQUIRE(allocatedBytes > 0);

        value.Assign("tiny");
        CHECK(log.deallocationCount == 1U);
        CHECK(log.lastDeallocatedBytes == allocatedBytes);
    }
}

TEST_CASE("String supports traits-aware comparisons", "[Text][String]")
{
    using FoldString = NGIN::Text::BasicString<char,
                                               32,
                                               NGIN::Memory::SystemAllocator,
                                               NGIN::Text::DefaultGrowthPolicy,
                                               FoldTraits>;

    FoldString value("Hello");
    CHECK(value.Compare(FoldString::view_type {"hello", 5}) == 0);
    CHECK(value.StartsWith(FoldString::view_type {"he", 2}));
    CHECK(value.EndsWith(FoldString::view_type {"LO", 2}));
    CHECK(value.StartsWith('h'));
    CHECK(value.StartsWith("he"));
    CHECK(value.EndsWith('O'));
    CHECK(value.EndsWith("LO"));
    CHECK(value.Find('h') == 0U);
    CHECK(value.Find(FoldString::view_type {"LL", 2}) == 2U);
    CHECK(value.RFind("LO") == 3U);
    CHECK(value.FindFirstOf("xyzl") == 2U);
    CHECK(value.FindFirstNotOf("he") == 2U);
    CHECK(value.FindLastNotOf("lo") == 1U);
}

TEST_CASE("String supports wide character SBO storage", "[Text][String]")
{
    using WideStr = NGIN::Text::BasicString<char16_t, 32>;
    WideStr value(u"hi");
    CHECK(value.Size() == 2U);
    CHECK(value.Data()[0] == u'h');
    CHECK(value.Data()[1] == u'i');
    CHECK(value.Find(u'i') == 1U);
    CHECK(value.RFind(u"hi") == 0U);
    CHECK(value.FindFirstOf(u"xyzih") == 0U);
    CHECK(value.FindLastNotOf(u"i") == 0U);
}

TEST_CASE("String operator plus supports c-string and char overloads", "[Text][String]")
{
    String value("mid");

    CHECK(("pre-" + value).View() == std::string_view("pre-mid"));
    CHECK((value + "-post").View() == std::string_view("mid-post"));
    CHECK(('[' + value).View() == std::string_view("[mid"));
    CHECK((value + ']').View() == std::string_view("mid]"));
}
