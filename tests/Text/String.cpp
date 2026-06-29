/// @file String.cpp
/// @brief Tests for NGIN::Text::BasicString and NGIN::Text::String.

#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Text/String.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using NGIN::Text::String;

static_assert(sizeof(NGIN::Text::String) <= 40);
static_assert(sizeof(NGIN::Text::UTF8String) <= 40);
static_assert(sizeof(NGIN::Text::WString) <= 40);
static_assert(sizeof(NGIN::Text::UTF16String) <= 40);
static_assert(sizeof(NGIN::Text::UTF32String) <= 40);
static_assert(sizeof(NGIN::Text::AnsiString) <= 24);
static_assert(sizeof(NGIN::Text::AsciiString) <= 24);

namespace
{
    bool CStrEqual(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
            return left == right;
        return std::strcmp(left, right) == 0;
    }

    template<class TString>
    void CheckStringState(const TString& value, std::string_view expected)
    {
        CHECK(value.Size() == expected.size());
        CHECK(value.View() == typename TString::view_type {expected.data(), expected.size()});
        REQUIRE(value.Data() != nullptr);
        CHECK(value.Capacity() >= value.Size());
        CHECK(value.CStr()[value.Size()] == '\0');
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

    struct TaggedAllocationLog
    {
        std::size_t allocationCount {0};
        std::size_t deallocationCount {0};
        std::size_t mismatchedDeallocationCount {0};
    };

    struct StrictSwapAllocator
    {
        struct Header
        {
            int         ownerId {0};
            std::size_t totalBytes {0};
            std::size_t alignment {0};
        };

        NGIN::Memory::SystemAllocator inner {};
        TaggedAllocationLog*          log {nullptr};
        int                           id {0};

        StrictSwapAllocator() = default;
        StrictSwapAllocator(TaggedAllocationLog& stats, int value) : log(&stats), id(value) {}

        void* Allocate(std::size_t bytes, std::size_t align) noexcept
        {
            const std::size_t actualAlign = align > alignof(Header) ? align : alignof(Header);
            const std::size_t totalBytes  = sizeof(Header) + bytes;
            void* const       raw         = inner.Allocate(totalBytes, actualAlign);
            if (raw == nullptr)
                return nullptr;

            auto* const header = static_cast<Header*>(raw);
            header->ownerId    = id;
            header->totalBytes = totalBytes;
            header->alignment  = actualAlign;
            if (log != nullptr)
                log->allocationCount += 1;

            return static_cast<void*>(header + 1);
        }

        void Deallocate(void* ptr, std::size_t, std::size_t) noexcept
        {
            if (ptr == nullptr)
                return;

            auto* const header = static_cast<Header*>(ptr) - 1;
            if (log != nullptr)
            {
                log->deallocationCount += 1;
                if (header->ownerId != id)
                    log->mismatchedDeallocationCount += 1;
            }

            inner.Deallocate(static_cast<void*>(header), header->totalBytes, header->alignment);
        }

        int Id() const noexcept { return id; }
    };

    struct ThrowingSwapAllocator
    {
        NGIN::Memory::SystemAllocator inner {};
        int                           id {0};
        bool                          throwOnSwap {false};

        ThrowingSwapAllocator() = default;
        ThrowingSwapAllocator(int value, bool shouldThrow = false) : id(value), throwOnSwap(shouldThrow) {}

        void* Allocate(std::size_t bytes, std::size_t align) noexcept
        {
            return inner.Allocate(bytes, align);
        }

        void Deallocate(void* ptr, std::size_t bytes, std::size_t align) noexcept
        {
            inner.Deallocate(ptr, bytes, align);
        }

        int Id() const noexcept { return id; }

        friend void swap(ThrowingSwapAllocator& left, ThrowingSwapAllocator& right)
        {
            if (left.throwOnSwap || right.throwOnSwap)
                throw std::runtime_error("allocator swap failed");

            using std::swap;
            swap(left.id, right.id);
            swap(left.throwOnSwap, right.throwOnSwap);
        }
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

    struct InstrumentedTraits : std::char_traits<char>
    {
        inline static std::size_t copyCount {0};
        inline static std::size_t moveCount {0};
        inline static std::size_t assignCount {0};

        static void Reset() noexcept
        {
            copyCount   = 0;
            moveCount   = 0;
            assignCount = 0;
        }

        static char* copy(char* destination, const char* source, std::size_t count) noexcept
        {
            copyCount += count;
            return std::char_traits<char>::copy(destination, source, count);
        }

        static char* move(char* destination, const char* source, std::size_t count) noexcept
        {
            moveCount += count;
            return std::char_traits<char>::move(destination, source, count);
        }

        static void assign(char& destination, const char& source) noexcept
        {
            assignCount += 1;
            std::char_traits<char>::assign(destination, source);
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

    template<>
    struct AllocatorPropagationTraits<StrictSwapAllocator>
    {
        static constexpr bool PropagateOnCopyAssignment = false;
        static constexpr bool PropagateOnMoveAssignment = false;
        static constexpr bool PropagateOnSwap           = false;
        static constexpr bool IsAlwaysEqual             = false;
    };

    template<>
    struct AllocatorPropagationTraits<ThrowingSwapAllocator>
    {
        static constexpr bool PropagateOnCopyAssignment = false;
        static constexpr bool PropagateOnMoveAssignment = false;
        static constexpr bool PropagateOnSwap           = true;
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

TEST_CASE("String aliases keep heap metadata overlapped with SBO storage", "[Text][String]")
{
    CHECK(sizeof(NGIN::Text::String) <= 40U);
    CHECK(sizeof(NGIN::Text::UTF8String) <= 40U);
    CHECK(sizeof(NGIN::Text::WString) <= 40U);
    CHECK(sizeof(NGIN::Text::UTF16String) <= 40U);
    CHECK(sizeof(NGIN::Text::UTF32String) <= 40U);
    CHECK(sizeof(NGIN::Text::AnsiString) <= 24U);
    CHECK(sizeof(NGIN::Text::AsciiString) <= 24U);
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

TEST_CASE("String constructs from pointer-length ranges", "[Text][String]")
{
    const char* text = "alphabet soup";

    SECTION("empty range accepts null")
    {
        String value(nullptr, 0);
        CHECK(value.Empty());
    }

    SECTION("small bounded range")
    {
        String value(text, 5);
        CHECK(value.View() == std::string_view("alpha"));
    }

    SECTION("heap bounded range")
    {
        std::string large(80, 'x');
        String      value(large.data(), large.size());
        CHECK(value.View() == std::string_view(large));
    }

    SECTION("null pointer with non-zero count throws")
    {
        CHECK_THROWS_AS((String(nullptr, 1)), std::invalid_argument);
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

TEST_CASE("String pointer-length assign and append preserve overlap handling", "[Text][String]")
{
    SECTION("Assign overlap")
    {
        String value("abcdef");
        value.Assign(value.Data() + 2, 3);
        CHECK(value.View() == std::string_view("cde"));
    }

    SECTION("Append overlap")
    {
        String value("abcdef");
        value.Append(value.Data() + 1, 4);
        CHECK(value.View() == std::string_view("abcdefbcde"));
    }

    SECTION("null pointer with non-zero count throws")
    {
        String value("abcdef");
        CHECK_THROWS_AS(value.Assign(nullptr, 1), std::invalid_argument);
        CHECK_THROWS_AS(value.Append(nullptr, 1), std::invalid_argument);
    }
}

TEST_CASE("String overwrite APIs support direct builders", "[Text][String]")
{
    SECTION("ResizeAndOverwrite exact and shorter writes")
    {
        String value("alpha");

        value.ResizeAndOverwrite(5, [](char* buffer, String::size_type writableCount) {
            REQUIRE(writableCount == 5U);
            std::memcpy(buffer, "omega", 5);
            return 5U;
        });
        CHECK(value.View() == std::string_view("omega"));

        value.ResizeAndOverwrite(8, [](char* buffer, String::size_type writableCount) {
            REQUIRE(writableCount == 8U);
            std::memcpy(buffer, "hi", 2);
            return 2U;
        });
        CHECK(value.View() == std::string_view("hi"));
    }

    SECTION("ResizeAndOverwrite restores size on throw and on oversized result")
    {
        String value("alpha");

        CHECK_THROWS_AS(
                value.ResizeAndOverwrite(10, [](char* buffer, String::size_type) -> String::size_type {
                    buffer[0] = 'z';
                    throw std::runtime_error("boom");
                }),
                std::runtime_error);
        CHECK(value.Size() == 5U);
        CHECK(value.Data()[value.Size()] == '\0');

        CHECK_THROWS_AS(
                value.ResizeAndOverwrite(3, [](char*, String::size_type) { return 4U; }),
                std::length_error);
        CHECK(value.Size() == 5U);
        CHECK(value.Data()[value.Size()] == '\0');
    }

    SECTION("AppendAndOverwrite exact shorter and throwing writes")
    {
        String value("pre");

        value.AppendAndOverwrite(5, [](char* buffer, String::size_type writableCount) {
            REQUIRE(writableCount == 5U);
            std::memcpy(buffer, "hello", 5);
            return 5U;
        });
        CHECK(value.View() == std::string_view("prehello"));

        value.AppendAndOverwrite(4, [](char* buffer, String::size_type writableCount) {
            REQUIRE(writableCount == 4U);
            std::memcpy(buffer, "xy", 2);
            return 2U;
        });
        CHECK(value.View() == std::string_view("prehelloxy"));

        CHECK_THROWS_AS(
                value.AppendAndOverwrite(6, [](char* buffer, String::size_type) -> String::size_type {
                    buffer[0] = '!';
                    throw std::runtime_error("boom");
                }),
                std::runtime_error);
        CHECK(value.View() == std::string_view("prehelloxy"));

        CHECK_THROWS_AS(
                value.AppendAndOverwrite(3, [](char*, String::size_type) { return 5U; }),
                std::length_error);
        CHECK(value.View() == std::string_view("prehelloxy"));
    }

    SECTION("Overwrite APIs can force heap growth and explicit view conversion remains available")
    {
        String value("tiny");

        value.ResizeAndOverwrite(80, [](char* buffer, String::size_type writableCount) {
            for (String::size_type i = 0; i < writableCount; ++i)
                buffer[i] = 'a';
            return writableCount;
        });
        CHECK(value.Size() == 80U);
        CHECK(value.Capacity() >= 80U);

        const String::view_type view = static_cast<String::view_type>(value);
        CHECK(view.size() == value.Size());
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
        CHECK(value.Find(String::view_type {}, value.Size()) == value.Size());
        CHECK(value.Find(String::view_type {}, value.Size() + 4) == String::npos);

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

TEST_CASE("String Find empty needle follows string_view position semantics", "[Text][String]")
{
    String value("abc");

    CHECK(value.Find(String::view_type {}, 0) == 0U);
    CHECK(value.Find(String::view_type {}, 2) == 2U);
    CHECK(value.Find(String::view_type {}, value.Size()) == value.Size());
    CHECK(value.Find(String::view_type {}, value.Size() + 1) == String::npos);
    CHECK(value.RFind(String::view_type {}, value.Size() + 1) == value.Size());
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

TEST_CASE("String packed storage handles representation transitions", "[Text][String]")
{
    using SmallStr = NGIN::Text::BasicString<char, 16>;

    SECTION("small to heap through append")
    {
        SmallStr value("small");
        value.Append("0123456789abcdef");
        CheckStringState(value, "small0123456789abcdef");
    }

    SECTION("heap to small through assign")
    {
        SmallStr value(std::string(80, 'h').c_str());
        value.Assign("tiny");
        CheckStringState(value, "tiny");
        CHECK(value.Capacity() == 15U);
    }

    SECTION("heap to small through shrink")
    {
        SmallStr value(std::string(80, 's').c_str());
        value.Resize(8);
        value.ShrinkToFit();
        CheckStringState(value, "ssssssss");
        CHECK(value.Capacity() == 15U);
    }

    SECTION("heap-backed empty remains valid")
    {
        SmallStr value("seed");
        value.ReserveExact(80);
        value.Clear();
        CheckStringState(value, "");
        CHECK(value.Capacity() == 80U);
    }

    SECTION("self append crosses into heap")
    {
        SmallStr value("abcdefghi");
        value.Append(value.View());
        CheckStringState(value, "abcdefghiabcdefghi");
    }

    SECTION("self replace can shrink heap to small")
    {
        SmallStr   value("abcdefghijklmnopqrstuvwxyz");
        const auto view = value.View();
        value.Replace(0, value.Size(), view.substr(3, 10));
        CheckStringState(value, "defghijklm");
        CHECK(value.Capacity() == 15U);
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

TEST_CASE("String Reserve and ReserveExact expose distinct capacity semantics", "[Text][String]")
{
    using SmallStr = NGIN::Text::BasicString<char, 16>;

    SmallStr grown("abc");
    grown.Reserve(20);
    CHECK(grown.Capacity() >= 20U);
    CHECK(grown.Capacity() > 20U);

    SmallStr exact("abc");
    exact.ReserveExact(20);
    CHECK(exact.Capacity() == 20U);
}

TEST_CASE("String swap respects allocator propagation traits", "[Text][String]")
{
    using SwapStr = NGIN::Text::BasicString<char, 32, SwapAllocator>;

    SECTION("small strings")
    {
        SwapStr left("Left", SwapAllocator {1});
        SwapStr right("Right", SwapAllocator {2});

        left.Swap(right);

        CHECK(CStrEqual(left.CStr(), "Right"));
        CHECK(CStrEqual(right.CStr(), "Left"));
        CHECK(left.GetAllocator().Id() == 1);
        CHECK(right.GetAllocator().Id() == 2);
    }

    SECTION("heap strings")
    {
        std::string leftValue(80, 'L');
        std::string rightValue(72, 'R');
        SwapStr     left(leftValue.c_str(), SwapAllocator {1});
        SwapStr     right(rightValue.c_str(), SwapAllocator {2});

        left.Swap(right);

        CHECK(left.View() == std::string_view(rightValue));
        CHECK(right.View() == std::string_view(leftValue));
        CHECK(left.GetAllocator().Id() == 1);
        CHECK(right.GetAllocator().Id() == 2);
    }
}

TEST_CASE("String swap covers small and heap representation pairs", "[Text][String]")
{
    using SmallStr = NGIN::Text::BasicString<char, 16>;

    SECTION("small swaps with heap")
    {
        SmallStr small("tiny");
        SmallStr heap(std::string(80, 'H').c_str());

        small.Swap(heap);

        CheckStringState(small, std::string(80, 'H'));
        CheckStringState(heap, "tiny");
    }

    SECTION("heap swaps with small")
    {
        SmallStr heap(std::string(72, 'Q').c_str());
        SmallStr small("abc");

        heap.Swap(small);

        CheckStringState(heap, "abc");
        CheckStringState(small, std::string(72, 'Q'));
    }

    SECTION("empty small swaps with heap")
    {
        SmallStr small;
        SmallStr heap(std::string(64, 'E').c_str());

        small.Swap(heap);

        CheckStringState(small, std::string(64, 'E'));
        CheckStringState(heap, "");
    }

    SECTION("full small swaps with heap")
    {
        SmallStr small("123456789012345");
        SmallStr heap(std::string(64, 'F').c_str());

        small.Swap(heap);

        CheckStringState(small, std::string(64, 'F'));
        CheckStringState(heap, "123456789012345");
    }

    SECTION("heap-backed empty swaps with small")
    {
        SmallStr heap(std::string(64, 'Z').c_str());
        heap.Clear();
        SmallStr small("abc");

        heap.Swap(small);

        CheckStringState(heap, "abc");
        CheckStringState(small, "");
        CHECK(small.Capacity() >= 64U);
    }
}

TEST_CASE("String non-propagating swap deallocates with destination allocators", "[Text][String]")
{
    using StrictStr = NGIN::Text::BasicString<char, 16, StrictSwapAllocator>;

    SECTION("heap strings")
    {
        TaggedAllocationLog log {};
        {
            std::string leftValue(80, 'L');
            std::string rightValue(72, 'R');
            StrictStr   left(leftValue.c_str(), StrictSwapAllocator {log, 1});
            StrictStr   right(rightValue.c_str(), StrictSwapAllocator {log, 2});

            left.Swap(right);

            CHECK(left.View() == std::string_view(rightValue));
            CHECK(right.View() == std::string_view(leftValue));
            CHECK(left.GetAllocator().Id() == 1);
            CHECK(right.GetAllocator().Id() == 2);
        }

        CHECK(log.allocationCount == log.deallocationCount);
        CHECK(log.mismatchedDeallocationCount == 0U);
    }

    SECTION("small and heap strings")
    {
        TaggedAllocationLog log {};
        {
            std::string heapValue(72, 'H');
            StrictStr   small("tiny", StrictSwapAllocator {log, 1});
            StrictStr   heap(heapValue.c_str(), StrictSwapAllocator {log, 2});

            small.Swap(heap);

            CheckStringState(small, heapValue);
            CheckStringState(heap, "tiny");
            CHECK(small.GetAllocator().Id() == 1);
            CHECK(heap.GetAllocator().Id() == 2);
        }

        CHECK(log.allocationCount == log.deallocationCount);
        CHECK(log.mismatchedDeallocationCount == 0U);
    }
}

TEST_CASE("String propagating swap preserves state when allocator swap throws", "[Text][String]")
{
    using ThrowStr = NGIN::Text::BasicString<char, 16, ThrowingSwapAllocator>;

    std::string leftValue(80, 'A');
    std::string rightValue(72, 'B');
    ThrowStr    left(leftValue.c_str(), ThrowingSwapAllocator {1, true});
    ThrowStr    right(rightValue.c_str(), ThrowingSwapAllocator {2, false});

    CHECK_THROWS_AS(left.Swap(right), std::runtime_error);
    CHECK(left.View() == std::string_view(leftValue));
    CHECK(right.View() == std::string_view(rightValue));
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

TEST_CASE("String raw memory fast paths do not bypass custom traits", "[Text][String]")
{
    using InstrumentedString = NGIN::Text::BasicString<char,
                                                       16,
                                                       NGIN::Memory::SystemAllocator,
                                                       NGIN::Text::DefaultGrowthPolicy,
                                                       InstrumentedTraits>;

    InstrumentedTraits::Reset();
    InstrumentedString value("abcdef");
    CHECK(value.View() == InstrumentedString::view_type {"abcdef", 6});
    CHECK(InstrumentedTraits::copyCount > 0U);

    InstrumentedTraits::Reset();
    value.Append(3, 'x');
    CHECK(value.View() == InstrumentedString::view_type {"abcdefxxx", 9});
    CHECK(InstrumentedTraits::assignCount == 3U);

    std::string        large(80, 'q');
    InstrumentedString heapValue(large.c_str());
    InstrumentedTraits::Reset();
    heapValue.Assign(heapValue.Data() + 1, 40);
    CHECK(heapValue.Size() == 40U);
    CHECK(InstrumentedTraits::moveCount == 40U);
}

TEST_CASE("String relational operators are routed through Compare", "[Text][String]")
{
    String alpha("alpha");
    String beta("beta");

    CHECK(alpha < beta);
    CHECK(alpha <= beta);
    CHECK(beta > alpha);
    CHECK(beta >= alpha);
    CHECK(alpha <= String::view_type {"alpha", 5});
    CHECK(alpha >= String::view_type {"alpha", 5});
    CHECK(String::view_type {"aardvark", 8} < beta);
    CHECK(String::view_type {"gamma", 5} > beta);
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

TEST_CASE("String exposes forward and reverse iterators across storage modes", "[Text][String]")
{
    SECTION("small")
    {
        String      value("abc");
        std::string forward;
        for (char ch: value)
            forward.push_back(ch);
        CHECK(forward == "abc");

        value.begin()[1] = 'Z';
        CHECK(value.View() == std::string_view("aZc"));

        std::string reverse;
        for (auto it = value.rbegin(); it != value.rend(); ++it)
            reverse.push_back(*it);
        CHECK(reverse == "cZa");
    }

    SECTION("heap")
    {
        String value(std::string(80, 'x').c_str());
        REQUIRE(value.begin() != value.end());
        *value.begin() = 'y';
        CHECK(value.Front() == 'y');
        CHECK(*value.rbegin() == 'x');
    }
}

TEST_CASE("String UTF-8 operations stay code-unit based", "[Text][String]")
{
    String value("\xC3\xA5\xC3\xA4\xC3\xB6");

    CHECK(value.Size() == 6U);
    CHECK(static_cast<unsigned char>(value[0]) == 0xC3u);
    CHECK(static_cast<unsigned char>(value[1]) == 0xA5u);
    CHECK(value.Substr(0, 2).View() == std::string_view("\xC3\xA5", 2));
    CHECK(value.Find(std::string_view("\xC3\xA4", 2)) == 2U);
}

TEST_CASE("String reverse two-byte search is bounds-safe at the tail", "[Text][String]")
{
    String value("abcab");

    CHECK(value.RFind("ab") == 3U);
    CHECK(value.RFind("bc", 4) == 1U);
    CHECK(value.RFind("zz") == String::npos);

    String single("a");
    CHECK(single.RFind("ab") == String::npos);
}

TEST_CASE("String deterministic mutations match std::string", "[Text][String]")
{
    using SmallStr = NGIN::Text::BasicString<char, 16>;

    SmallStr      actual("seed");
    std::string   expected("seed");
    std::uint32_t state = 0xC0FFEEu;

    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    auto toStringFind = [](std::size_t value) {
        return value == std::string::npos ? SmallStr::npos : static_cast<SmallStr::size_type>(value);
    };

    auto assertMatches = [&]() {
        CHECK(actual.Size() == expected.size());
        CHECK(actual.View() == std::string_view(expected.data(), expected.size()));
        REQUIRE(actual.Data() != nullptr);
        CHECK(actual.Data()[actual.Size()] == '\0');

        const std::string_view needles[] = {
                std::string_view {},
                std::string_view {"a", 1},
                std::string_view {"bc", 2},
                std::string_view {"xyz", 3},
        };
        const std::size_t positions[] = {
                0,
                expected.size() / 2,
                expected.size(),
                expected.size() + 3,
        };

        for (std::string_view needle: needles)
        {
            for (std::size_t pos: positions)
            {
                CHECK(actual.Find(SmallStr::view_type {needle.data(), needle.size()}, pos) ==
                      toStringFind(expected.find(needle, pos)));
            }
        }
    };

    assertMatches();
    for (std::size_t step = 0; step < 160; ++step)
    {
        switch (next() % 4U)
        {
            case 0: {
                const std::size_t count = next() % 12U;
                const char        ch    = static_cast<char>('a' + (next() % 26U));
                actual.Append(count, ch);
                expected.append(count, ch);
                break;
            }
            case 1: {
                const std::size_t pos   = next() % (expected.size() + 1U);
                const std::size_t count = next() % 8U;
                const char        ch    = static_cast<char>('a' + (next() % 26U));
                actual.Insert(pos, count, ch);
                expected.insert(pos, count, ch);
                break;
            }
            case 2: {
                if (expected.empty())
                    break;

                const std::size_t pos   = next() % expected.size();
                const std::size_t count = next() % (expected.size() - pos + 5U);
                actual.Erase(pos, count);
                expected.erase(pos, count);
                break;
            }
            default: {
                const std::size_t pos        = next() % (expected.size() + 1U);
                const std::size_t eraseCount = pos == expected.size() ? 0U : next() % (expected.size() - pos + 4U);
                const std::size_t fillCount  = next() % 14U;
                const char        ch         = static_cast<char>('a' + (next() % 26U));
                const std::string replacement(fillCount, ch);

                actual.Replace(pos, eraseCount, SmallStr::view_type {replacement.data(), replacement.size()});
                expected.replace(pos, eraseCount, replacement);
                break;
            }
        }

        assertMatches();
    }
}
