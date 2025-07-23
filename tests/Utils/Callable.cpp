/// @file CallableTest.cpp
/// @brief Tests for NGIN::Utilities::Callable using boost::ut
///
/// This suite covers default construction, invocation, small‐buffer vs heap storage,
/// copy/move behavior, exception conditions, and edge cases.

#include <NGIN/Utilities/Callable.hpp>// Include your Callable header
#include <boost/ut.hpp>
#include <memory>// For std::unique_ptr

using namespace boost::ut;

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
        MoveOnlyCallable() : p(std::make_unique<int>(10)) {}

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
        bool m_moved                    = false;
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

suite<"NGIN::Utilities::Callable"> callableTests = [] {
    "DefaultConstructor"_test = [] {
        NGIN::Utilities::Callable<int(int)> c;
        expect(!c);
        expect(throws<std::bad_function_call>([&] { c(5); }));
    };

    "AssignFunctionPointer"_test = [] {
        NGIN::Utilities::Callable<int(int)> c = FreeFunction;
        expect(!!c);
        expect(c(3) == 6);
    };

    "AssignSmallLambda"_test = [] {
        auto lambda = [](int x) { return x + 7; };
        NGIN::Utilities::Callable<int(int)> c(lambda);
        expect(!!c);
        expect(c(8) == 15);
    };

    "CopyConstructor_Small"_test = [] {
        auto lambda = [](int x) { return x * x; };
        NGIN::Utilities::Callable<int(int)> original(lambda);
        NGIN::Utilities::Callable<int(int)> copy(original);
        expect(!!copy);
        expect(copy(4) == 16);
    };

    "MoveConstructor_Small"_test = [] {
        auto lambda = [](int x) { return x - 1; };
        NGIN::Utilities::Callable<int(int)> source(lambda);
        NGIN::Utilities::Callable<int(int)> moved(std::move(source));

        expect(!!moved);
        expect(moved(10) == 9);

        // Source is now in a valid but unspecified state: operator() should throw
        expect(throws<std::bad_function_call>([&] { source(5); }));
    };

    "AssignLargeFunctor"_test = [] {
        LargeFunctor lf;
        NGIN::Utilities::Callable<int(int)> c = lf;
        expect(!!c);
        // LargeFunctor adds data[0] = 5
        expect(c(10) == 15);
    };

    "CopyConstructor_Large"_test = [] {
        LargeFunctor lf;
        NGIN::Utilities::Callable<int(int)> original(lf);
        NGIN::Utilities::Callable<int(int)> copy(original);

        expect(!!copy);
        expect(copy(7) == 12);
    };

    "MoveConstructor_Large"_test = [] {
        LargeFunctor lf;
        NGIN::Utilities::Callable<int(int)> source(lf);
        // Invoke once to get a value before move
        int before = source(2);
        NGIN::Utilities::Callable<int(int)> moved(std::move(source));

        expect(!!moved);
        expect(moved(2) == before);

        // Source is empty now
        expect(throws<std::bad_function_call>([&] { source(1); }));
    };

    "CopyAssignment_Small"_test = [] {
        auto lambdaA = [](int x) { return x + 1; };
        auto lambdaB = [](int x) { return x + 2; };
        NGIN::Utilities::Callable<int(int)> a(lambdaA);
        NGIN::Utilities::Callable<int(int)> b(lambdaB);

        b = a;
        expect(!!b);
        expect(b(5) == 6);
    };

    "CopyAssignment_Large"_test = [] {
        LargeFunctor lfA;
        LargeFunctor lfB;
        // Modify lfB so they differ
        lfB.data[0] = 2;

        NGIN::Utilities::Callable<int(int)> a(lfA);
        NGIN::Utilities::Callable<int(int)> b(lfB);

        // a yields x+5, b yields x+2
        expect(a(3) == 8);
        expect(b(3) == 5);

        b = a;
        expect(!!b);
        // Now b should behave like a (x+5)
        expect(b(3) == 8);
    };

    "MoveAssignment_Small"_test = [] {
        auto lambda = [](int x) { return x * 3; };
        NGIN::Utilities::Callable<int(int)> a(lambda);
        NGIN::Utilities::Callable<int(int)> b(lambda);

        a = std::move(b);
        expect(!!a);
        expect(a(4) == 12);
        // b is empty after move
        expect(throws<std::bad_function_call>([&] { b(1); }));
    };

    "MoveAssignment_Large"_test = [] {
        LargeFunctor lfA;
        LargeFunctor lfB;
        lfB.data[0] = 2;

        NGIN::Utilities::Callable<int(int)> a(lfA);
        NGIN::Utilities::Callable<int(int)> b(lfB);

        // Before move: a→+5, b→+2
        expect(a(1) == 6);
        expect(b(1) == 3);

        a = std::move(b);
        expect(!!a);
        // Now a should behave like old b (x+2)
        expect(a(1) == 3);

        // b is empty
        expect(throws<std::bad_function_call>([&] { b(2); }));
    };

    "CopyMoveOnlyCallable_ThrowsOnCopy"_test = [] {
        MoveOnlyCallable moc;
        NGIN::Utilities::Callable<int()> c(std::move(moc));
        expect(!!c);
        expect(c() == 10);

        // Attempting to copy should throw a runtime_error
        expect(throws<std::runtime_error>([&] {
            NGIN::Utilities::Callable<int()> copy(c);
        }));
    };

    "MoveMoveOnlyCallable_Works"_test = [] {
        NGIN::Utilities::Callable<int()> c(std::move(MoveOnlyCallable {}));
        expect(!!c);
        expect(c() == 10);

        NGIN::Utilities::Callable<int()> moved(std::move(c));
        expect(!!moved);
        expect(moved() == 10);

        // Original is empty now
        expect(throws<std::bad_function_call>([&] { c(); }));
    };

    "SelfAssignment_Copy"_test = [] {
        NGIN::Utilities::Callable<int(int)> c = FreeFunction;
        c                                     = c;
        expect(!!c);
        expect(c(5) == 10);
    };

    "SelfAssignment_Move"_test = [] {
        NGIN::Utilities::Callable<int(int)> c = [](int x) { return x + 4; };
        c                                     = std::move(c);
        expect(!!c);
        expect(c(5) == 9);
    };

    "NullCallable_Assignment"_test = [] {
        NGIN::Utilities::Callable<int(int)> c;
        c = nullptr;// Should leave it empty
        expect(!c);
        expect(throws<std::bad_function_call>([&] { c(0); }));
    };

    "VoidReturnType"_test = [] {
        bool called                         = false;
        NGIN::Utilities::Callable<void()> c = [&] { called = true; };
        expect(!!c);
        c();
        expect(called);

        // Copy and move still work
        auto copy = c;
        expect(!!copy);
        copy();
        expect(called);

        auto moved = std::move(c);
        expect(!!moved);
        moved();
        expect(called);
        expect(!c);// original is empty
    };

    "MultipleArgumentsAndRefForwarding"_test = [] {
        NGIN::Utilities::Callable<std::string(const std::string&, int, char)> c =
                [](const std::string& s, int n, char ch) {
                    return s + ":" + std::to_string(n) + ch;
                };
        expect(!!c);

        std::string base   = "base";
        std::string result = c(base, 42, 'X');
        expect(result == "base:42X");
    };

    "StatefulLambdaByValueCapture"_test = [] {
        int value = 100;
        // Capture by value -> subsequent modification of 'value' does not affect call
        auto lam = [value](int x) { return x + value; };
        NGIN::Utilities::Callable<int(int)> c(lam);
        expect(c(5) == 105);
        value = 200;
        expect(c(5) == 105);// still the old captured 100
    };

    "StatefulLambdaByReferenceCapture"_test = [] {
        int value = 7;
        // Capture by reference -> changing 'value' affects call
        auto lam = [&value](int x) { return x + value; };
        NGIN::Utilities::Callable<int(int)> c(lam);
        expect(c(3) == 10);
        value = 21;
        expect(c(3) == 24);
    };

    "SwapInlineInline"_test = [] {
        NGIN::Utilities::Callable<int(int)> a = [](int x) { return x + 1; };
        NGIN::Utilities::Callable<int(int)> b = [](int x) { return x + 2; };

        expect(a(1) == 2);
        expect(b(1) == 3);
        a.Swap(b);
        expect(a(1) == 3);
        expect(b(1) == 2);
    };

    "SwapHeapHeap"_test = [] {
        LargeFunctor lfA, lfB;
        lfA.data[0] = 5;// a(x) = x+5
        lfB.data[0] = 9;// b(x) = x+9

        NGIN::Utilities::Callable<int(int)> a(lfA);
        NGIN::Utilities::Callable<int(int)> b(lfB);
        expect(a(1) == 6);
        expect(b(1) == 10);

        a.Swap(b);
        expect(a(1) == 10);
        expect(b(1) == 6);
    };

    "SwapInlineHeap"_test = [] {
        // a is small, b is large
        NGIN::Utilities::Callable<int(int)> a = [](int x) { return x * 2; };
        LargeFunctor lf;
        NGIN::Utilities::Callable<int(int)> b(lf);

        expect(a(3) == 6);
        expect(b(3) == 8);// since data[0]=5 => 3+5=8

        a.Swap(b);

        expect(a(3) == 8);
        expect(b(3) == 6);
    };

    "AssignNullToNonEmpty"_test = [] {
        NGIN::Utilities::Callable<int(int)> c = FreeFunction;
        expect(!!c);
        c = nullptr;
        expect(!c);
        expect(throws<std::bad_function_call>([&] { c(1); }));
    };

    "CopyAssignEmptyToNonEmpty"_test = [] {
        NGIN::Utilities::Callable<int(int)> nonEmpty = FreeFunction;
        NGIN::Utilities::Callable<int(int)> empty;
        expect(!!nonEmpty);
        expect(!empty);

        nonEmpty = empty;
        expect(!nonEmpty);// now empty
    };

    "MoveAssignEmptyToNonEmpty"_test = [] {
        NGIN::Utilities::Callable<int(int)> nonEmpty = FreeFunction;
        NGIN::Utilities::Callable<int(int)> empty;
        expect(!!nonEmpty);
        expect(!empty);

        nonEmpty = std::move(empty);
        expect(!nonEmpty);// should be empty
    };

    // We cannot directly "construct" from nullptr (no overload), but we can verify
    // that the template‐ctor cannot accept nullptr_t by ensuring the following line
    // does NOT compile (manually verified):
    //
    //     NGIN::Utilities::Callable<int(int)> x(nullptr);
    //
    // so instead we only check assignment-to-null.

    "BoundarySizeFunctor_ExactlyBuffer"_test = [] {
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

        expect(!!c);
        expect(c(1) == 8);

        // Copy & move still work inline:
        auto ccopy = c;
        expect(ccopy(2) == 9);

        auto cmove = std::move(c);
        expect(cmove(3) == 10);
        expect(!c);// original becomes empty after move
    };
    "ExceptionSafety_CopyThrows"_test = [] {
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
        expect(!!c);
        expect(c() == 42);
        expect(throws<std::runtime_error>([&] {
            NGIN::Utilities::Callable<int()> copy(c);
        }));
    };
    "DestructionSemantics"_test = [] {
        DtorCounter::count = 0;
        {
            NGIN::Utilities::Callable<int()> c(DtorCounter {});
            expect(!!c);
            expect(c() == 1);
        }
        expect(DtorCounter::count == 1);
    };
    "AlignmentEdgeCase"_test = [] {
        struct alignas(64) AlignedFunctor
        {
            char data[8];
            int operator()() const
            {
                return 123;
            }
        };
        NGIN::Utilities::Callable<int()> c(AlignedFunctor {});
        expect(!!c);
        expect(c() == 123);
    };
};
