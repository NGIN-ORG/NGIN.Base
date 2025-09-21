/// @file CallableTest.cpp
/// @brief Tests for NGIN::Utilities::Callable using Catch2
///
/// This suite covers default construction, invocation, small‐buffer vs heap storage,
/// copy/move behavior, exception conditions, and edge cases.

#include <NGIN/Utilities/Callable.hpp>// Include your Callable header
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"// or the specific warnings
#include <catch2/catch_test_macros.hpp>
#pragma clang diagnostic pop
#include <memory>// For std::unique_ptr
#include <stdexcept>

namespace
{
    // A simple free function for testing
    int FreeFunction(int x)
    {
        return x * 2;
    }

    // A helper to force a large‐sized functor (exceeds 32 bytes buffer)
    struct LargeFunctor
    {
        // 100 bytes of data to exceed BUFFER_SIZE (32 bytes on 64‐bit)
        alignas(std::max_align_t) char data[100];

        LargeFunctor()
        {
            // initialize first byte so operator() can use it
            data[0] = 5;
        }

        int operator()(int x) const
        {
            return x + static_cast<int>(data[0]);
        }
    };

    // A move‐only callable (not copyable)
    struct MoveOnlyCallable
    {
        std::unique_ptr<int> p;
        MoveOnlyCallable()
            : p(std::make_unique<int>(10)) {}

        MoveOnlyCallable(MoveOnlyCallable&&) noexcept            = default;
        MoveOnlyCallable& operator=(MoveOnlyCallable&&) noexcept = default;

        MoveOnlyCallable(const MoveOnlyCallable&)            = delete;
        MoveOnlyCallable& operator=(const MoveOnlyCallable&) = delete;

        int operator()() const
        {
            return *p;
        }
    };

    // For DestructionSemantics test
    struct DtorCounter
    {
        static int count;
        bool       m_moved              = false;
        DtorCounter()                   = default;
        DtorCounter(const DtorCounter&) = default;

        DtorCounter(DtorCounter&& other) noexcept
        {
            other.m_moved = true;
        }
        ~DtorCounter()
        {
            if (!m_moved)
            {
                ++count;
            }
        }
        int operator()() const
        {
            return 1;
        }
    };
    int DtorCounter::count = 0;
}// namespace

TEST_CASE("NGIN::Utilities::Callable", "[Utilities][Callable]")
{
    SECTION("DefaultConstructor")
    {
        NGIN::Utilities::Callable<int(int)> c;
        CHECK_FALSE(c);
        CHECK_THROWS_AS(c(5), std::bad_function_call);
    }

    SECTION("AssignFunctionPointer")
    {
        NGIN::Utilities::Callable<int(int)> c = FreeFunction;
        CHECK(c);
        CHECK(c(3) == 6);
    }

    SECTION("AssignSmallLambda")
    {
        auto                                lambda = [](int x) { return x + 7; };
        NGIN::Utilities::Callable<int(int)> c(lambda);
        CHECK(c);
        CHECK(c(8) == 15);
    }

    SECTION("CopyConstructor_Small")
    {
        auto                                lambda = [](int x) { return x * x; };
        NGIN::Utilities::Callable<int(int)> original(lambda);
        NGIN::Utilities::Callable<int(int)> copy(original);
        CHECK(copy);
        CHECK(copy(4) == 16);
    }

    SECTION("MoveConstructor_Small")
    {
        auto                                lambda = [](int x) { return x - 1; };
        NGIN::Utilities::Callable<int(int)> source(lambda);
        NGIN::Utilities::Callable<int(int)> moved(std::move(source));

        CHECK(moved);
        CHECK(moved(10) == 9);

        // Source is now in a valid but unspecified state: operator() should throw
        CHECK_THROWS_AS(source(5), std::bad_function_call);
    }

    SECTION("AssignLargeFunctor")
    {
        LargeFunctor                        lf;
        NGIN::Utilities::Callable<int(int)> c = lf;
        CHECK(c);
        // LargeFunctor adds data[0] = 5
        CHECK(c(10) == 15);
    }

    SECTION("CopyConstructor_Large")
    {
        LargeFunctor                        lf;
        NGIN::Utilities::Callable<int(int)> original(lf);
        NGIN::Utilities::Callable<int(int)> copy(original);

        CHECK(copy);
        CHECK(copy(7) == 12);
    }

    SECTION("MoveConstructor_Large")
    {
        LargeFunctor                        lf;
        NGIN::Utilities::Callable<int(int)> source(lf);
        // Invoke once to get a value before move
        int                                 before = source(2);
        NGIN::Utilities::Callable<int(int)> moved(std::move(source));

        CHECK(moved);
        CHECK(moved(2) == before);

        // Source is empty now
        CHECK_THROWS_AS(source(1), std::bad_function_call);
    }

    SECTION("CopyAssignment_Small")
    {
        auto                                lambdaA = [](int x) { return x + 1; };
        auto                                lambdaB = [](int x) { return x + 2; };
        NGIN::Utilities::Callable<int(int)> a(lambdaA);
        NGIN::Utilities::Callable<int(int)> b(lambdaB);

        b = a;
        CHECK(b);
        CHECK(b(5) == 6);
    }

    SECTION("CopyAssignment_Large")
    {
        LargeFunctor lfA;
        LargeFunctor lfB;
        // Modify lfB so they differ
        lfB.data[0] = 2;

        NGIN::Utilities::Callable<int(int)> a(lfA);
        NGIN::Utilities::Callable<int(int)> b(lfB);

        // a yields x+5, b yields x+2
        CHECK(a(3) == 8);
        CHECK(b(3) == 5);

        b = a;
        CHECK(b);
        // Now b should behave like a (x+5)
        CHECK(b(3) == 8);
    }

    SECTION("MoveAssignment_Small")
    {
        auto                                lambda = [](int x) { return x * 3; };
        NGIN::Utilities::Callable<int(int)> a(lambda);
        NGIN::Utilities::Callable<int(int)> b(lambda);

        a = std::move(b);
        CHECK(a);
        CHECK(a(4) == 12);
        // b is empty after move
        CHECK_THROWS_AS(b(1), std::bad_function_call);
    }

    SECTION("MoveAssignment_Large")
    {
        LargeFunctor lfA;
        LargeFunctor lfB;
        lfB.data[0] = 2;

        NGIN::Utilities::Callable<int(int)> a(lfA);
        NGIN::Utilities::Callable<int(int)> b(lfB);

        // Before move: a→+5, b→+2
        CHECK(a(1) == 6);
        CHECK(b(1) == 3);

        a = std::move(b);
        CHECK(a);
        // Now a should behave like old b (x+2)
        CHECK(a(1) == 3);

        // b is empty
        CHECK_THROWS_AS(b(2), std::bad_function_call);
    }

    SECTION("CopyMoveOnlyCallable_ThrowsOnCopy")
    {
        MoveOnlyCallable                 moc;
        NGIN::Utilities::Callable<int()> c(std::move(moc));
        CHECK(c);
        CHECK(c() == 10);

        // Attempting to copy should throw a runtime_error
        CHECK_THROWS_AS([&] { NGIN::Utilities::Callable<int()> copy(c); }(), std::runtime_error);
    }

    SECTION("MoveMoveOnlyCallable_Works")
    {
        NGIN::Utilities::Callable<int()> c(std::move(MoveOnlyCallable {}));
        CHECK(c);
        CHECK(c() == 10);

        NGIN::Utilities::Callable<int()> moved(std::move(c));
        CHECK(moved);
        CHECK(moved() == 10);

        // Original is empty now
        CHECK_THROWS_AS(c(), std::bad_function_call);
    }

    SECTION("SelfAssignment_Copy")
    {
        NGIN::Utilities::Callable<int(int)> c = FreeFunction;
        c                                     = c;
        CHECK(c);
        CHECK(c(5) == 10);
    }

    SECTION("SelfAssignment_Move")
    {
        NGIN::Utilities::Callable<int(int)> c = [](int x) { return x + 4; };
        c                                     = std::move(c);
        CHECK(c);
        CHECK(c(5) == 9);
    }

    SECTION("NullCallable_Assignment")
    {
        NGIN::Utilities::Callable<int(int)> c;
        c = nullptr;// Should leave it empty
        CHECK_FALSE(c);
        CHECK_THROWS_AS(c(0), std::bad_function_call);
    }

    SECTION("VoidReturnType")
    {
        bool                              called = false;
        NGIN::Utilities::Callable<void()> c      = [&] { called = true; };
        CHECK(c);
        c();
        CHECK(called);

        // Copy and move still work
        auto copy = c;
        CHECK(copy);
        copy();
        CHECK(called);

        auto moved = std::move(c);
        CHECK(moved);
        moved();
        CHECK(called);
        CHECK_FALSE(c);// original is empty
    }

    SECTION("MultipleArgumentsAndRefForwarding")
    {
        NGIN::Utilities::Callable<std::string(const std::string&, int, char)> c =
                [](const std::string& s, int n, char ch) {
                    return s + ":" + std::to_string(n) + ch;
                };
        CHECK(c);

        std::string base   = "base";
        std::string result = c(base, 42, 'X');
        CHECK(result == "base:42X");
    }

    SECTION("StatefulLambdaByValueCapture")
    {
        int value = 100;
        // Capture by value -> subsequent modification of 'value' does not affect call
        auto                                lam = [value](int x) { return x + value; };
        NGIN::Utilities::Callable<int(int)> c(lam);
        CHECK(c(5) == 105);
        value = 200;
        CHECK(c(5) == 105);// still the old captured 100
    }

    SECTION("StatefulLambdaByReferenceCapture")
    {
        int value = 7;
        // Capture by reference -> changing 'value' affects call
        auto                                lam = [&value](int x) { return x + value; };
        NGIN::Utilities::Callable<int(int)> c(lam);
        CHECK(c(3) == 10);
        value = 21;
        CHECK(c(3) == 24);
    }

    SECTION("SwapInlineInline")
    {
        NGIN::Utilities::Callable<int(int)> a = [](int x) { return x + 1; };
        NGIN::Utilities::Callable<int(int)> b = [](int x) { return x + 2; };

        CHECK(a(1) == 2);
        CHECK(b(1) == 3);
        a.Swap(b);
        CHECK(a(1) == 3);
        CHECK(b(1) == 2);
    }

    SECTION("SwapHeapHeap")
    {
        LargeFunctor lfA, lfB;
        lfA.data[0] = 5;// a(x) = x+5
        lfB.data[0] = 9;// b(x) = x+9

        NGIN::Utilities::Callable<int(int)> a(lfA);
        NGIN::Utilities::Callable<int(int)> b(lfB);
        CHECK(a(1) == 6);
        CHECK(b(1) == 10);

        a.Swap(b);
        CHECK(a(1) == 10);
        CHECK(b(1) == 6);
    }

    SECTION("SwapInlineHeap")
    {
        // a is small, b is large
        NGIN::Utilities::Callable<int(int)> a = [](int x) { return x * 2; };
        LargeFunctor                        lf;
        NGIN::Utilities::Callable<int(int)> b(lf);

        CHECK(a(3) == 6);
        CHECK(b(3) == 8);// since data[0]=5 => 3+5=8

        a.Swap(b);

        CHECK(a(3) == 8);
        CHECK(b(3) == 6);
    }

    SECTION("AssignNullToNonEmpty")
    {
        NGIN::Utilities::Callable<int(int)> c = FreeFunction;
        CHECK(c);
        c = nullptr;
        CHECK_FALSE(c);
        CHECK_THROWS_AS(c(1), std::bad_function_call);
    }

    SECTION("CopyAssignEmptyToNonEmpty")
    {
        NGIN::Utilities::Callable<int(int)> nonEmpty = FreeFunction;
        NGIN::Utilities::Callable<int(int)> empty;
        CHECK(nonEmpty);
        CHECK_FALSE(empty);

        nonEmpty = empty;
        CHECK_FALSE(nonEmpty);// now empty
    }

    SECTION("MoveAssignEmptyToNonEmpty")
    {
        NGIN::Utilities::Callable<int(int)> nonEmpty = FreeFunction;
        NGIN::Utilities::Callable<int(int)> empty;
        CHECK(nonEmpty);
        CHECK_FALSE(empty);

        nonEmpty = std::move(empty);
        CHECK_FALSE(nonEmpty);// should be empty
    };

    // We cannot directly "construct" from nullptr (no overload), but we can verify
    // that the template‐ctor cannot accept nullptr_t by ensuring the following line
    // does NOT compile (manually verified):
    //
    //     NGIN::Utilities::Callable<int(int)> x(nullptr);
    //
    // so instead we only check assignment-to-null.

    SECTION("BoundarySizeFunctor_ExactlyBuffer")
    {
        // Create a functor whose sizeof == BUFFER_SIZE exactly (32 bytes on 64-bit)
        struct AlignToMax
        {
            alignas(std::max_align_t) char data[sizeof(void*) * 4];
            int operator()(int x) const
            {
                return x + (int) data[0];
            }
        };

        // Because this is exactly BUFFER_SIZE and alignof(...) ≤ ALIGNMENT,
        // it should use inline storage (SBO).
        AlignToMax f;
        f.data[0] = 7;
        NGIN::Utilities::Callable<int(int)> c(f);

        CHECK(c);
        CHECK(c(1) == 8);

        // Copy & move still work inline:
        auto ccopy = c;
        CHECK(ccopy(2) == 9);

        auto cmove = std::move(c);
        CHECK(cmove(3) == 10);
        CHECK_FALSE(c);// original becomes empty after move
    };
    SECTION("ExceptionSafety_CopyThrows")
    {
        struct ThrowOnCopy
        {
            ThrowOnCopy() = default;
            ThrowOnCopy(const ThrowOnCopy&)
            {
                throw std::runtime_error("copy fail");
            }
            ThrowOnCopy(ThrowOnCopy&&) noexcept = default;
            int operator()() const
            {
                return 42;
            }
        };
        NGIN::Utilities::Callable<int()> c(ThrowOnCopy {});
        CHECK(c);
        CHECK(c() == 42);
        CHECK_THROWS_AS([&] { NGIN::Utilities::Callable<int()> copy(c); }(), std::runtime_error);
    };
    SECTION("DestructionSemantics")
    {
        DtorCounter::count = 0;
        {
            NGIN::Utilities::Callable<int()> c(DtorCounter {});
            CHECK(c);
            CHECK(c() == 1);
        }
        CHECK(DtorCounter::count == 1);
    };
    SECTION("AlignmentEdgeCase")
    {
        struct alignas(64) AlignedFunctor
        {
            char data[8];
            int  operator()() const
            {
                return 123;
            }
        };
        NGIN::Utilities::Callable<int()> c(AlignedFunctor {});
        CHECK(c);
        CHECK(c() == 123);
    }
}
